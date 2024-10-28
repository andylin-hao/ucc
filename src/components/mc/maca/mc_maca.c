#include "mc_maca.h"
#include "ucc/api/ucc_status.h"
#include "utils/ucc_compiler_def.h"
#include "utils/ucc_malloc.h"
#include "utils/arch/cpu.h"
#include "utils/ucc_parser.h"

static ucc_config_field_t ucc_mc_maca_config_table[] = {
    {"", "", NULL, ucc_offsetof(ucc_mc_maca_config_t, super),
     UCC_CONFIG_TYPE_TABLE(ucc_mc_config_table)},

    {"MPOOL_ELEM_SIZE", "1Mb", "The size of each element in mc maca mpool",
     ucc_offsetof(ucc_mc_maca_config_t, mpool_elem_size),
     UCC_CONFIG_TYPE_MEMUNITS},

    {"MPOOL_MAX_ELEMS", "8", "The max amount of elements in mc maca mpool",
     ucc_offsetof(ucc_mc_maca_config_t, mpool_max_elems), UCC_CONFIG_TYPE_UINT},

    {NULL}};

static ucc_status_t ucc_mc_maca_init(const ucc_mc_params_t *mc_params)
{
    // CUDA additionally init the flush memory op based on the CUDA version (use cuFlushGPUDirectRDMAWrites for CUDA > 11.3)
    int       num_devices;
    mcError_t maca_st;

    ucc_mc_maca.stream             = NULL;
    ucc_mc_maca.stream_initialized = 0;
    ucc_strncpy_safe(ucc_mc_maca.super.config->log_component.name,
                     ucc_mc_maca.super.super.name,
                     sizeof(ucc_mc_maca.super.config->log_component.name));
    ucc_mc_maca.thread_mode = mc_params->thread_mode;
    maca_st                 = mcGetDeviceCount(&num_devices); // DEVICE OP
    if ((maca_st != mcSuccess) || (num_devices == 0)) {
        mc_debug(&ucc_mc_maca.super, "maca devices are not found");
        return maca_error_to_ucc_status(maca_st);
    }

    // lock assures single mpool initiation when multiple threads concurrently execute
    // different collective operations thus concurrently entering init function.
    ucc_spinlock_init(&ucc_mc_maca.init_spinlock, 0);

    return UCC_OK;
}

static ucc_status_t ucc_mc_maca_get_attr(ucc_mc_attr_t *mc_attr)
{
    if (mc_attr->field_mask & UCC_MC_ATTR_FIELD_THREAD_MODE) {
        mc_attr->thread_mode = ucc_mc_maca.thread_mode;
    }
    if (mc_attr->field_mask & UCC_MC_ATTR_FIELD_FAST_ALLOC_SIZE) {
        if (MC_MACA_CONFIG->mpool_max_elems > 0) {
            mc_attr->fast_alloc_size = MC_MACA_CONFIG->mpool_elem_size;
        } else {
            mc_attr->fast_alloc_size = 0;
        }
    }
    return UCC_OK;
}

// Alloc device memory
static ucc_status_t ucc_mc_maca_mem_alloc(ucc_mc_buffer_header_t **h_ptr,
                                          size_t size, ucc_memory_type_t mt)
{
    mcError_t               st;
    ucc_mc_buffer_header_t *h =
        ucc_malloc(sizeof(ucc_mc_buffer_header_t), "mc_maca");
    if (ucc_unlikely(!h)) {
        mc_error(&ucc_mc_maca.super, "failed to allocate %zd bytes",
                 sizeof(ucc_mc_buffer_header_t));
    }
    st = mcMalloc(&h->addr, size); // DEVICE OP
    if (ucc_unlikely(st != mcSuccess)) {
        mc_error(&ucc_mc_maca.super,
                 "failed to allocate %zd bytes, maca error %d(%s)", size, st,
                 mcGetErrorString(st)); // DEVICE OP
        ucc_free(h);
        return maca_error_to_ucc_status(st);
    }

    h->from_pool = 0;
    h->mt        = mt;
    *h_ptr       = h;
    mc_trace(&ucc_mc_maca.super, "allocated %ld bytes from maca", size);
    return UCC_OK;
}

// Free device memory
static ucc_status_t ucc_mc_maca_mem_free(ucc_mc_buffer_header_t *h_ptr)
{
    mcError_t st = mcFree(h_ptr->addr); // DEVICE OP
    if (ucc_unlikely(st != mcSuccess)) {
        mc_error(&ucc_mc_maca.super,
                 "failed to free mem at %p, maca error %d(%s)", h_ptr->addr, st,
                 mcGetErrorString(st)); // DEVICE OP
        return maca_error_to_ucc_status(st);
    }
    ucc_free(h_ptr);
    return UCC_OK;
}

// Alloc the chunk itself
static ucc_status_t ucc_mc_maca_chunk_alloc(ucc_mpool_t *mp, //NOLINT
                                            size_t *size_p, void **chunk_p)
{
    *chunk_p = ucc_malloc(*size_p, "mc maca");
    if (!*chunk_p) {
        mc_error(&ucc_mc_maca.super, "failed to allocate %zd bytes", *size_p);
        return UCC_ERR_NO_MEMORY;
    }

    return UCC_OK;
}

// Releasing the chunk itself
static void ucc_mc_maca_chunk_release(ucc_mpool_t *mp, //NOLINT: mp is unused
                                      void *       chunk)
{
    ucc_free(chunk);
}

// Init device resources in the chunk
// Corresponds to the obj_init memory op
static void ucc_mc_maca_obj_init(ucc_mpool_t *mp,        //NOLINT
                                 void *obj, void *chunk) //NOLINT
{
    ucc_mc_buffer_header_t *h = (ucc_mc_buffer_header_t *)obj;
    mcError_t               st =
        mcMalloc(&h->addr, MC_MACA_CONFIG->mpool_elem_size); // DEVICE OP

    if (st != mcSuccess) {
        // ROCm will set h->addr to NULL but CUDA won't
        // On the safe side, set h->addr to NULL
        h->addr = NULL;
        mcGetLastError(); // DEVICE OP
        mc_error(&ucc_mc_maca.super,
                 "failed to allocate %zd bytes, maca error %d(%s)",
                 MC_MACA_CONFIG->mpool_elem_size, st,
                 mcGetErrorString(st)); // DEVICE OP
    }
    h->from_pool = 1;
    h->mt        = UCC_MEMORY_TYPE_MACA;
}

// Init device resources in the chunk with managed memory
static void ucc_mc_maca_obj_init_managed(ucc_mpool_t *mp,        //NOLINT
                                         void *obj, void *chunk) //NOLINT
{
    ucc_mc_buffer_header_t *h = (ucc_mc_buffer_header_t *)obj;
    mcError_t               st;

    st = mcMallocManaged(h->addr, MC_MACA_CONFIG->mpool_elem_size,
                         mcMemAttachGlobal); // DEVICE OP
    if (st != mcSuccess) {
        h->addr = NULL;
        mcGetLastError(); // DEVICE OP
        mc_error(&ucc_mc_maca.super,
                 "failed to allocate %zd bytes, maca error %d(%s)",
                 MC_MACA_CONFIG->mpool_elem_size, st,
                 mcGetErrorString(st)); // DEVICE OP
    }
    h->from_pool = 1;
    h->mt        = UCC_MEMORY_TYPE_MACA_MANAGED;
}

// Cleaup device resources in the chunk before releasing
// Corresponds to the obj_cleanup memory op
static void ucc_mc_maca_obj_cleanup(ucc_mpool_t *mp, //NOLINT: mp is unused
                                    void *       obj)
{
    ucc_mc_buffer_header_t *h = (ucc_mc_buffer_header_t *)obj;
    mcError_t               st;
    st = mcFree(h->addr); // DEVICE OP
    if (st != mcSuccess) {
        mcGetLastError(); // DEVICE OP
        mc_error(&ucc_mc_maca.super,
                 "failed to free mem at %p, maca error %d(%s)", h->addr, st,
                 mcGetErrorString(st)); // DEVICE OP
    }
}

static ucc_mpool_ops_t ucc_mc_ops = {.chunk_alloc   = ucc_mc_maca_chunk_alloc,
                                     .chunk_release = ucc_mc_maca_chunk_release,
                                     .obj_init      = ucc_mc_maca_obj_init,
                                     .obj_cleanup   = ucc_mc_maca_obj_cleanup};

static ucc_mpool_ops_t ucc_mc_managed_ops = {
    .chunk_alloc   = ucc_mc_maca_chunk_alloc,
    .chunk_release = ucc_mc_maca_chunk_release,
    .obj_init      = ucc_mc_maca_obj_init_managed,
    .obj_cleanup   = ucc_mc_maca_obj_cleanup};

static ucc_status_t ucc_mc_maca_mem_pool_alloc(ucc_mc_buffer_header_t **h_ptr,
                                               size_t                   size,
                                               ucc_memory_type_t        mt)
{
    ucc_mc_buffer_header_t *h = NULL;

    if (size <= MC_MACA_CONFIG->mpool_elem_size) {
        // DEVICE MEMORY SPECIFIC BEGIN
        if (mt == UCC_MEMORY_TYPE_MACA) {
            h = (ucc_mc_buffer_header_t *)ucc_mpool_get(&ucc_mc_maca.mpool);
        } else if (mt == UCC_MEMORY_TYPE_MACA_MANAGED) {
            h = (ucc_mc_buffer_header_t *)ucc_mpool_get(
                &ucc_mc_maca.mpool_managed);
        }
        // DEVICE MEMORY SPECIFIC END
        else {
            return UCC_ERR_INVALID_PARAM;
        }
    }

    if (!h) {
        // Slow path
        return ucc_mc_maca_mem_alloc(h_ptr, size, mt);
    }
    if (ucc_unlikely(!h->addr)) {
        return UCC_ERR_NO_MEMORY;
    }
    *h_ptr = h;
    mc_trace(&ucc_mc_maca.super, "allocated %ld bytes from maca mpool", size);
    return UCC_OK;
}

// Alloc device memory from memory pool when memory pool is not initialized
static ucc_status_t
ucc_mc_maca_mem_pool_alloc_with_init(ucc_mc_buffer_header_t **h_ptr,
                                     size_t size, ucc_memory_type_t mt)
{
    // lock assures single mpool initiation when multiple threads concurrently execute
    // different collective operations thus concurrently entering init function.
    ucc_spin_lock(&ucc_mc_maca.init_spinlock);

    if (MC_MACA_CONFIG->mpool_max_elems == 0) {
        // No memory pool, use the slow path default memory ops
        ucc_mc_maca.super.ops.mem_alloc = ucc_mc_maca_mem_alloc;
        ucc_mc_maca.super.ops.mem_free  = ucc_mc_maca_mem_free;
        ucc_spin_unlock(&ucc_mc_maca.init_spinlock);
        return ucc_mc_maca_mem_alloc(h_ptr, size, mt);
    }

    if (!ucc_mc_maca.mpool_init_flag) {
        ucc_status_t status = ucc_mpool_init(
            &ucc_mc_maca.mpool, 0, sizeof(ucc_mc_buffer_header_t), 0,
            UCC_CACHE_LINE_SIZE, 1, MC_MACA_CONFIG->mpool_max_elems,
            &ucc_mc_ops, ucc_mc_maca.thread_mode, "mc maca mpool buffers");
        if (status != UCC_OK) {
            ucc_spin_unlock(&ucc_mc_maca.init_spinlock);
            return status;
        }

        status = ucc_mpool_init(
            &ucc_mc_maca.mpool_managed, 0, sizeof(ucc_mc_buffer_header_t), 0,
            UCC_CACHE_LINE_SIZE, 1, MC_MACA_CONFIG->mpool_max_elems,
            &ucc_mc_managed_ops, ucc_mc_maca.thread_mode,
            "mc maca mpool buffers");
        if (status != UCC_OK) {
            ucc_spin_unlock(&ucc_mc_maca.init_spinlock);
            return status;
        }

        ucc_mc_maca.super.ops.mem_alloc = ucc_mc_maca_mem_pool_alloc;
        ucc_mc_maca.mpool_init_flag     = 1;
    }
    ucc_spin_unlock(&ucc_mc_maca.init_spinlock);
    return ucc_mc_maca_mem_pool_alloc(h_ptr, size, mt);
}

// Free device memory from the pool
static ucc_status_t ucc_mc_maca_mem_pool_free(ucc_mc_buffer_header_t *h_ptr)
{
    if (!h_ptr->from_pool) {
        return ucc_mc_maca_mem_free(h_ptr);
    }
    ucc_mpool_put(h_ptr);
    return UCC_OK;
}

static ucc_status_t ucc_mc_maca_memcpy(void *dst, const void *src, size_t len,
                                       ucc_memory_type_t dst_mem,
                                       ucc_memory_type_t src_mem)
{
    mcError_t st;

    ucc_assert(dst_mem == UCC_MEMORY_TYPE_MACA ||
               src_mem == UCC_MEMORY_TYPE_MACA ||
               dst_mem == UCC_MEMORY_TYPE_MACA_MANAGED ||
               src_mem == UCC_MEMORY_TYPE_MACA_MANAGED);

    UCC_MC_MACA_INIT_STREAM();
    st = mcMemcpyAsync(dst, src, len, mcMemcpyDefault,
                       ucc_mc_maca.stream); // DEVICE OP
    if (ucc_unlikely(st != mcSuccess)) {
        mcGetLastError(); // DEVICE OP
        mc_error(&ucc_mc_maca.super,
                 "failed to copy %ld bytes, maca error %d(%s)", len, st,
                 mcGetErrorString(st)); // DEVICE OP
        return maca_error_to_ucc_status(st);
    }

    st = mcStreamSynchronize(ucc_mc_maca.stream); // DEVICE OP
    if (ucc_unlikely(st != mcSuccess)) {
        mcGetLastError(); // DEVICE OP
        mc_error(&ucc_mc_maca.super,
                 "failed to synchronize mc_maca.stream, maca error %d(%s)", st,
                 mcGetErrorString(st)); // DEVICE OP
        return maca_error_to_ucc_status(st);
    }
    return UCC_OK;
}

static ucc_status_t ucc_mc_maca_memset(void *ptr, int val, size_t len)
{
    mcError_t st;

    UCC_MC_MACA_INIT_STREAM();
    st = mcMemsetAsync(ptr, val, len, ucc_mc_maca.stream); // DEVICE OP
    if (ucc_unlikely(st != mcSuccess)) {
        mcGetLastError(); // DEVICE OP
        mc_error(&ucc_mc_maca.super,
                 "failed to set %ld bytes, maca error %d(%s)", len, st,
                 mcGetErrorString(st)); // DEVICE OP
        return maca_error_to_ucc_status(st);
    }
    st = mcStreamSynchronize(ucc_mc_maca.stream); // DEVICE OP
    if (ucc_unlikely(st != mcSuccess)) {
        mcGetLastError(); // DEVICE OP
        mc_error(&ucc_mc_maca.super,
                 "failed to synchronize mc_maca.stream, maca error %d(%s)", st,
                 mcGetErrorString(st)); // DEVICE OP
        return maca_error_to_ucc_status(st);
    }
    return UCC_OK;
}

static ucc_status_t ucc_mc_maca_mem_query(const void *    ptr,
                                          ucc_mem_attr_t *mem_attr)
{
    mcPointerAttribute_t attr;
    mcError_t            st;
    ucc_memory_type_t    mem_type;
    void *               base_address;
    size_t               alloc_length;

    if (!(mem_attr->field_mask &
          (UCC_MEM_ATTR_FIELD_MEM_TYPE | UCC_MEM_ATTR_FIELD_ALLOC_LENGTH |
           UCC_MEM_ATTR_FIELD_BASE_ADDRESS))) {
        return UCC_OK;
    }

    if (mem_attr->field_mask & UCC_MEM_ATTR_FIELD_MEM_TYPE) {
        st = mcPointerGetAttributes(&attr, ptr); // DEVICE OP
        if (st != mcSuccess) {
            mcGetLastError(); // DEVICE OP
            mc_debug(&ucc_mc_maca.super, "mcPointerGetAttributes(%p) error: %d",
                     ptr, st);
            return UCC_ERR_NOT_SUPPORTED;
        }

        switch (attr.memoryType) {
        case mcMemoryTypeHost:
            mem_type = UCC_MEMORY_TYPE_HOST;
            break;
        case mcMemoryTypeDevice:
            mem_type = UCC_MEMORY_TYPE_MACA;
            break;
        case mcMemoryTypeManaged:
            mem_type = UCC_MEMORY_TYPE_MACA_MANAGED;
            break;
        default:
            return UCC_ERR_NOT_SUPPORTED;
        }
        mem_attr->mem_type = mem_type;
    }

    if (mem_attr->field_mask &
        (UCC_MEM_ATTR_FIELD_ALLOC_LENGTH | UCC_MEM_ATTR_FIELD_BASE_ADDRESS)) {
        st = mcMemGetAddressRange((mcDeviceptr_t *)&base_address, &alloc_length,
                                  (mcDeviceptr_t)ptr); // DEVICE OP
        if (st != mcSuccess) {
            mc_debug(&ucc_mc_maca.super, "mcMemGetAddressRange(%p) error: %d",
                     ptr, st);
            return maca_error_to_ucc_status(st);
        }

        mem_attr->base_address = base_address;
        mem_attr->alloc_length = alloc_length;
    }

    return UCC_OK;
}

static ucc_status_t ucc_mc_maca_finalize()
{
    if (ucc_mc_maca.stream != NULL) {
        MACA_CHECK(mcStreamDestroy(ucc_mc_maca.stream));
        ucc_mc_maca.stream = NULL;
    }
    if (ucc_mc_maca.mpool_init_flag) {
        ucc_mpool_cleanup(&ucc_mc_maca.mpool, 1);
        ucc_mpool_cleanup(&ucc_mc_maca.mpool_managed, 1);
        ucc_mc_maca.mpool_init_flag     = 0;
        ucc_mc_maca.super.ops.mem_alloc = ucc_mc_maca_mem_pool_alloc_with_init;
    }
    ucc_spinlock_destroy(&ucc_mc_maca.init_spinlock);
    return UCC_OK;
}

ucc_mc_maca_t ucc_mc_maca = {
    .super.super.name    = "maca mc",
    .super.ref_cnt       = 0,
    .super.ee_type       = UCC_EE_MACA_STREAM,
    .super.type          = UCC_MEMORY_TYPE_MACA,
    .super.init          = ucc_mc_maca_init,
    .super.get_attr      = ucc_mc_maca_get_attr,
    .super.finalize      = ucc_mc_maca_finalize,
    .super.ops.mem_query = ucc_mc_maca_mem_query,
    .super.ops.mem_alloc = ucc_mc_maca_mem_pool_alloc_with_init,
    .super.ops.mem_free  = ucc_mc_maca_mem_pool_free,
    .super.ops.memcpy    = ucc_mc_maca_memcpy,
    .super.ops.memset    = ucc_mc_maca_memset,
    .super.config_table =
        {
            .name   = "MACA memory component",
            .prefix = "MC_MACA_",
            .table  = ucc_mc_maca_config_table,
            .size   = sizeof(ucc_mc_maca_config_t),
        },
    .mpool_init_flag = 0,
};

UCC_CONFIG_REGISTER_TABLE_ENTRY(&ucc_mc_maca.super.config_table,
                                &ucc_config_global_list);