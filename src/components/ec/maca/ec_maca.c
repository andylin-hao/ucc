#include "ec_maca.h"
#include "components/ec/base/ucc_ec_base.h"
#include "components/ec/ucc_ec_log.h"
#include "ec_maca_executor.h"
#include "ucc/api/ucc.h"
#include "ucc/api/ucc_status.h"
#include "utils/ucc_malloc.h"
#include "utils/arch/cpu.h"
#include "utils/ucc_mpool.h"
#include "utils/ucc_parser.h"
#include <limits.h>
#include <mc_runtime.h>

static const char *stream_task_modes[] = {[UCC_EC_MACA_TASK_KERNEL]  = "kernel",
                                          [UCC_EC_MACA_TASK_MEM_OPS] = "driver",
                                          [UCC_EC_MACA_TASK_AUTO]    = "auto",
                                          [UCC_EC_MACA_TASK_LAST]    = NULL};

// Should align with ucc_ec_maca_config_t
static ucc_config_field_t ucc_ec_maca_config_table[] = {
    {"", "", NULL, ucc_offsetof(ucc_ec_maca_config_t, super),
     UCC_CONFIG_TYPE_TABLE(ucc_ec_config_table)},

    {"STREAM_TASK_MODE", "auto",
     "Mechanism to create stream dependency\n"
     "kernel - use waiting kernel\n"
     "driver - use driver MEM_OPS\n"
     "auto   - runtime automatically chooses best one",
     ucc_offsetof(ucc_ec_maca_config_t, strm_task_mode),
     UCC_CONFIG_TYPE_ENUM(stream_task_modes)},

    {"EXEC_NUM_WORKERS", "1",
     "Number of thread blocks to use for maca executor",
     ucc_offsetof(ucc_ec_maca_config_t, exec_num_workers),
     UCC_CONFIG_TYPE_ULUNITS},

    {"EXEC_NUM_THREADS", "512",
     "Number of thread per block to use for maca executor",
     ucc_offsetof(ucc_ec_maca_config_t, exec_num_threads),
     UCC_CONFIG_TYPE_ULUNITS},

    {"EXEC_MAX_TASKS", "128",
     "Maximum number of outstanding tasks per executor",
     ucc_offsetof(ucc_ec_maca_config_t, exec_max_tasks),
     UCC_CONFIG_TYPE_ULUNITS},

    {"EXEC_NUM_STREAMS", "16",
     "Number of streams used by interruptible executor",
     ucc_offsetof(ucc_ec_maca_config_t, exec_num_streams),
     UCC_CONFIG_TYPE_ULUNITS},

    {"EXEC_COPY_LARGE_THRESH", "1M",
     "Single memcopy size to switch from kernel copy to cudaMemcpy",
     ucc_offsetof(ucc_ec_maca_config_t, exec_copy_thresh),
     UCC_CONFIG_TYPE_MEMUNITS},

    {"REDUCE_NUM_BLOCKS", "auto",
     "Number of thread blocks to use for reduction in interruptible mode",
     ucc_offsetof(ucc_ec_maca_config_t, reduce_num_blocks),
     UCC_CONFIG_TYPE_ULUNITS},

    {"REDUCE_NUM_THREADS", "auto",
     "Number of threads per block to use for reduction in interruptible mode",
     ucc_offsetof(ucc_ec_maca_config_t, reduce_num_threads),
     UCC_CONFIG_TYPE_ULUNITS},

    {"USE_COOPERATIVE_LAUNCH", "0",
     "whether to use cooperative launch in persistent kernel executor",
     ucc_offsetof(ucc_ec_maca_config_t, use_cooperative_launch),
     UCC_CONFIG_TYPE_BOOL},

    {NULL}};

/*Executor memory pool------------------------------------------------------------*/

static ucc_status_t ucc_ec_maca_ee_executor_mpool_chunk_malloc(
    ucc_mpool_t *mp, //NOLINT: mp is unused
    size_t *size_p, void **chunk_p)
{
    return MACA_FUNC(
        mcMallocHost((void **)chunk_p, *size_p, mcMallocHostMapped));
}

static void
ucc_ec_maca_ee_executor_mpool_chunk_free(ucc_mpool_t *mp, //NOLINT: mp is unused
                                         void *       chunk)
{
    MACA_FUNC(mcFreeHost(chunk));
}

static void ucc_ec_maca_executor_init(ucc_mpool_t *mp, void *obj, //NOLINT
                                      void *chunk)                //NOLINT
{
    ucc_ec_maca_executor_t *eee       = (ucc_ec_maca_executor_t *)obj;
    int                     max_tasks = EC_MACA_CONFIG->exec_max_tasks;

    MACA_FUNC(mcHostGetDevicePointer((void **)(&eee->dev_state),
                                     (void *)&eee->state, 0));
    MACA_FUNC(mcHostGetDevicePointer((void **)(&eee->dev_pidx),
                                     (void *)&eee->pidx, 0));
    MACA_FUNC(mcMalloc((void **)&eee->dev_cidx, sizeof(*eee->dev_cidx)));
    MACA_FUNC(mcMallocHost((void **)&eee->tasks,
                           max_tasks * MAX_SUBTASKS *
                               sizeof(ucc_ee_executor_task_args_t),
                           mcMallocHostMapped));
    MACA_FUNC(mcHostGetDevicePointer((void **)(&eee->dev_tasks),
                                     (void *)eee->tasks, 0));
    if (ucc_ec_maca.thread_mode == UCC_THREAD_MULTIPLE) {
        ucc_spinlock_init(&eee->tasks_lock, 0);
    }
}

static void ucc_ec_maca_executor_cleanup(ucc_mpool_t *mp, //NOLINT: mp is unused
                                         void *       obj)
{
    ucc_ec_maca_executor_t *eee = (ucc_ec_maca_executor_t *)obj;

    MACA_FUNC(mcFree((void *)eee->dev_cidx));
    MACA_FUNC(mcFreeHost((void *)eee->tasks));
    if (ucc_ec_maca.thread_mode == UCC_THREAD_MULTIPLE) {
        ucc_spinlock_destroy(&eee->tasks_lock);
    }
}

static ucc_mpool_ops_t ucc_ec_maca_ee_executor_mpool_ops = {
    .chunk_alloc   = ucc_ec_maca_ee_executor_mpool_chunk_malloc,
    .chunk_release = ucc_ec_maca_ee_executor_mpool_chunk_free,
    .obj_init      = ucc_ec_maca_executor_init,
    .obj_cleanup   = ucc_ec_maca_executor_cleanup,
};

/*MACA Event memory pool------------------------------------------------------------*/

static void ucc_ec_maca_event_init(ucc_mpool_t *mp, void *obj, //NOLINT: mp is unused
                                   void *chunk) //NOLINT
{
    ucc_ec_maca_event_t *base = (ucc_ec_maca_event_t *)obj;

    MACA_FUNC(mcEventCreateWithFlags(&base->event, mcEventDisableTiming));
}

static void ucc_ec_maca_event_cleanup(ucc_mpool_t *mp, //NOLINT: mp is unused
                                      void *       obj)
{
    ucc_ec_maca_event_t *base = (ucc_ec_maca_event_t *)obj;

    MACA_FUNC(mcEventDestroy(base->event));
}

static ucc_mpool_ops_t ucc_ec_maca_event_mpool_ops = {
    .chunk_alloc   = ucc_mpool_hugetlb_malloc,
    .chunk_release = ucc_mpool_hugetlb_free,
    .obj_init      = ucc_ec_maca_event_init,
    .obj_cleanup   = ucc_ec_maca_event_cleanup,
};

/*MACA Interruptible Task memory pool------------------------------------------------------------*/

static void
ucc_ec_maca_interruptible_task_init(ucc_mpool_t *mp, void *obj, //NOLINT: mp is unused
                                    void *chunk) //NOLINT
{
    ucc_ec_maca_executor_interruptible_task_t *task =
        (ucc_ec_maca_executor_interruptible_task_t *)obj;
    mcGraphNode_t memcpy_node;
    int           i;

    MACA_FUNC(mcGraphCreate(&task->graph, 0));
    for (i = 0; i < MAX_SUBTASKS; i++) {
        MACA_FUNC(mcGraphAddMemcpyNode1D(&memcpy_node, task->graph, NULL, 0,
                                         (void *)1, (void *)1, 1,
                                         mcMemcpyDefault));
    }

    MACA_FUNC(mcGraphInstantiateWithFlags(&task->graph_exec, task->graph, 0));
}

static void
ucc_ec_maca_interruptible_task_cleanup(ucc_mpool_t *mp, //NOLINT: mp is unused
                                       void *       obj)
{
    ucc_ec_maca_executor_interruptible_task_t *task =
        (ucc_ec_maca_executor_interruptible_task_t *)obj;

    MACA_FUNC(mcGraphExecDestroy(task->graph_exec));
    MACA_FUNC(mcGraphDestroy(task->graph));
}

static ucc_mpool_ops_t ucc_ec_maca_interruptible_task_mpool_ops = {
    .chunk_alloc   = ucc_mpool_hugetlb_malloc,
    .chunk_release = ucc_mpool_hugetlb_free,
    .obj_init      = ucc_ec_maca_interruptible_task_init,
    .obj_cleanup   = ucc_ec_maca_interruptible_task_cleanup,
};

static inline void ucc_ec_maca_set_threads_nbr(int *nt, int maxThreadsPerBlock,
                                               int warpSize)
{
    if (*nt == UCC_ULUNITS_AUTO) {
        if (maxThreadsPerBlock < *nt) {
            ec_warn(&ucc_ec_maca.super,
                    "number of threads (%d) is too small, min supported %d",
                    *nt, maxThreadsPerBlock);
        } else if ((*nt % warpSize) != 0) {
            ec_warn(&ucc_ec_maca.super,
                    "number of threads (%d) should be multiple of %d", *nt, warpSize);
        } else {
            return;
        }
    }

    *nt = (maxThreadsPerBlock / warpSize) * warpSize;
}

// Might need to clean up mpool if allocation fails
static ucc_status_t ucc_ec_maca_init(const ucc_ec_params_t *ec_params)
{
    ucc_ec_maca_config_t *cfg = EC_MACA_CONFIG;
    ucc_status_t          status;
    int                   device, num_devices;
    mcError_t             maca_st;
    mcDeviceProp_t        prop;
    int supports_coop_launch = 0; // DEVICE SPECIFIC, CUDA only

    ucc_ec_maca.stream                   = NULL;
    ucc_ec_maca.stream_initialized       = 0;
    ucc_ec_maca.exec_streams_initialized = 0;
    ucc_strncpy_safe(ucc_ec_maca.super.config->log_component.name,
                     ucc_ec_maca.super.super.name,
                     sizeof(ucc_ec_maca.super.config->log_component.name));
    ucc_ec_maca.thread_mode = ec_params->thread_mode;
    maca_st                 = mcGetDeviceCount(&num_devices);
    if ((maca_st != mcSuccess) || (num_devices == 0)) {
        ec_debug(&ucc_ec_maca.super, "MACA devices are not found");
        return UCC_ERR_NO_RESOURCE;
    }
    MACA_CHECK(mcGetDevice(&device));

    MACA_CHECK(mcGetDeviceProperties(&prop, device));

    ucc_ec_maca_set_threads_nbr((int *)&cfg->exec_num_threads,
                                prop.maxThreadsPerBlock, prop.warpSize);
    ucc_ec_maca_set_threads_nbr(&cfg->reduce_num_threads,
                                prop.maxThreadsPerBlock, prop.warpSize);

    if (cfg->reduce_num_threads != UCC_ULUNITS_AUTO) {
        if (prop.maxGridSize[0] < cfg->reduce_num_blocks) {
            ec_warn(&ucc_ec_maca.super,
                    "number of blocks is too large, max supported is %d",
                    prop.maxGridSize[0]);
            cfg->reduce_num_blocks = prop.maxGridSize[0];
        }
    } else {
        cfg->reduce_num_blocks = prop.maxGridSize[0];
    }

    if (cfg->exec_num_streams < 1) {
        ec_warn(&ucc_ec_maca.super,
                "number of streams is too small, min supported 1");
        cfg->exec_num_streams = 1;
    }

    /*create event pool */
    ucc_ec_maca.exec_streams = ucc_calloc(
        cfg->exec_num_streams, sizeof(mcStream_t), "ec_maca_exec_streams");
    if (!ucc_ec_maca.exec_streams) {
        ec_error(&ucc_ec_maca.super, "failed to allocate streams array");
        return UCC_ERR_NO_MEMORY;
    }
    status = ucc_mpool_init(&ucc_ec_maca.events, 0, sizeof(ucc_ec_maca_event_t),
                            0, UCC_CACHE_LINE_SIZE, 16, UINT_MAX,
                            &ucc_ec_maca_event_mpool_ops, UCC_THREAD_MULTIPLE,
                            "MACA Event Objects");
    if (status != UCC_OK) {
        ec_error(&ucc_ec_maca.super, "failed to create event pool");
        return status;
    }

    status = ucc_mpool_init(
        &ucc_ec_maca.executors, 0, sizeof(ucc_ec_maca_executor_t), 0,
        UCC_CACHE_LINE_SIZE, 16, UINT_MAX, &ucc_ec_maca_ee_executor_mpool_ops,
        UCC_THREAD_MULTIPLE, "EE Executor Objects");
    if (status != UCC_OK) {
        ec_error(&ucc_ec_maca.super, "failed to create executors pool");
        return status;
    }

    status =
        ucc_mpool_init(&ucc_ec_maca.executor_interruptible_tasks, 0,
                       sizeof(ucc_ec_maca_executor_interruptible_task_t), 0,
                       UCC_CACHE_LINE_SIZE, 16, UINT_MAX,
                       &ucc_ec_maca_interruptible_task_mpool_ops,
                       UCC_THREAD_MULTIPLE, "interruptible executor tasks");
    if (status != UCC_OK) {
        ec_error(&ucc_ec_maca.super,
                 "failed to create interruptible tasks pool");
        return status;
    }

    status = ucc_mpool_init(&ucc_ec_maca.executor_persistent_tasks, 0,
                            sizeof(ucc_ec_maca_executor_persistent_task_t), 0,
                            UCC_CACHE_LINE_SIZE, 16, UINT_MAX, NULL,
                            UCC_THREAD_MULTIPLE, "persistent executor tasks");
    if (status != UCC_OK) {
        ec_error(&ucc_ec_maca.super, "failed to create persistent tasks pool");
        return status;
    }

    // DEVICE SPECIFIC BEGIN
    // CUDA only
    if (cfg->strm_task_mode == UCC_EC_MACA_TASK_KERNEL) {
        ucc_ec_maca.strm_task_mode = UCC_EC_MACA_TASK_KERNEL;
    } else {
        ucc_ec_maca.strm_task_mode = UCC_EC_MACA_TASK_MEM_OPS;
    }

    if (cfg->use_cooperative_launch == 1) {
        mcDeviceGetAttribute(&supports_coop_launch,
                             mcDeviceAttributeCooperativeLaunch, device);
        if (!supports_coop_launch) {
            cfg->use_cooperative_launch = 0;
            ec_warn(&ucc_ec_maca.super,
                    "MACA cooperative groups are not supported. "
                    "Fall back to non cooperative launch.");
        }
    }
    // DEVICE SPECIFIC END

    ucc_spinlock_init(&ucc_ec_maca.init_spinlock, 0);
    return UCC_OK;
}

static ucc_status_t ucc_ec_maca_get_attr(ucc_ec_attr_t *ec_attr)
{
    if (ec_attr->field_mask & UCC_EC_ATTR_FIELD_THREAD_MODE) {
        ec_attr->thread_mode = ucc_ec_maca.thread_mode;
    }
    return UCC_OK;
}

ucc_status_t ucc_ec_maca_event_create(void **event)
{
    ucc_ec_maca_event_t *maca_event;

    maca_event = ucc_mpool_get(&ucc_ec_maca.events);
    if (ucc_unlikely(!maca_event)) {
        ec_error(&ucc_ec_maca.super, "Failed to get event from mpool");
        return UCC_ERR_NO_MEMORY;
    }

    *event = maca_event;
    return UCC_OK;
}

ucc_status_t ucc_ec_maca_event_destroy(void *event)
{
    ucc_ec_maca_event_t *maca_event = event;

    ucc_mpool_put(maca_event);
    return UCC_OK;
}

ucc_status_t ucc_ec_maca_event_post(void *ee_context, void *event)
{
    mcStream_t           stream     = (mcStream_t)ee_context;
    ucc_ec_maca_event_t *maca_event = event;

    MACA_CHECK(mcEventRecord(maca_event->event, stream));
    return UCC_OK;
}

ucc_status_t ucc_ec_maca_event_test(void *event)
{
    mcError_t            maca_st;
    ucc_ec_maca_event_t *maca_event = event;

    maca_st = mcEventQuery(maca_event->event);

    if (ucc_unlikely((maca_st != mcSuccess) && (maca_st != mcErrorNotReady))) {
        MACA_CHECK(maca_st);
    }
    return maca_error_to_ucc_status(maca_st);
}

ucc_status_t ucc_ec_maca_finalize()
{
    int i;

    if (ucc_ec_maca.stream_initialized) {
        MACA_CHECK(mcStreamDestroy(ucc_ec_maca.stream));
        ucc_ec_maca.stream             = NULL;
        ucc_ec_maca.stream_initialized = 0;
    }

    if (ucc_ec_maca.exec_streams_initialized) {
        for (i = 0; i < EC_MACA_CONFIG->exec_num_streams; i++) {
            MACA_CHECK(mcStreamDestroy(ucc_ec_maca.exec_streams[i]));
        }
        ucc_ec_maca.exec_streams_initialized = 0;
    }

    ucc_mpool_cleanup(&ucc_ec_maca.events, 1);
    ucc_mpool_cleanup(&ucc_ec_maca.executors, 1);
    ucc_mpool_cleanup(&ucc_ec_maca.executor_interruptible_tasks, 1);
    ucc_mpool_cleanup(&ucc_ec_maca.executor_persistent_tasks, 1);
    ucc_free(ucc_ec_maca.exec_streams);

    return UCC_OK;
}

ucc_ec_maca_t ucc_ec_maca = {
    .super.super.name                 = "maca ec",
    .super.ref_cnt                    = 0,
    .super.type                       = UCC_EE_MACA_STREAM,
    .super.init                       = ucc_ec_maca_init,
    .super.get_attr                   = ucc_ec_maca_get_attr,
    .super.finalize                   = ucc_ec_maca_finalize,
    .super.config_table               = {.name   = "MACA execution component",
                           .prefix = "EC_MACA_",
                           .table  = ucc_ec_maca_config_table,
                           .size   = sizeof(ucc_ec_maca_config_t)},
    .super.ops.create_event           = ucc_ec_maca_event_create,
    .super.ops.destroy_event          = ucc_ec_maca_event_destroy,
    .super.ops.event_post             = ucc_ec_maca_event_post,
    .super.ops.event_test             = ucc_ec_maca_event_test,
    .super.executor_ops.init          = ucc_maca_executor_init,
    .super.executor_ops.start         = ucc_maca_executor_start,
    .super.executor_ops.status        = ucc_maca_executor_status,
    .super.executor_ops.stop          = ucc_maca_executor_stop,
    .super.executor_ops.task_post     = ucc_maca_executor_task_post,
    .super.executor_ops.task_test     = ucc_maca_executor_task_test,
    .super.executor_ops.task_finalize = ucc_maca_executor_task_finalize,
    .super.executor_ops.finalize      = ucc_maca_executor_finalize,
};

UCC_CONFIG_REGISTER_TABLE_ENTRY(&ucc_ec_maca.super.config_table,
                                &ucc_config_global_list);