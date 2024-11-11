#ifndef UCC_MC_MACA_H_
#define UCC_MC_MACA_H_

#include "components/mc/base/ucc_mc_base.h"
#include "components/mc/ucc_mc_log.h"
#include "utils/ucc_mpool.h"
#include "utils/arch/maca_def.h"
#include <common/mcComplex.h>
#include <mc_runtime.h>

typedef struct ucc_mc_maca_config {
    ucc_mc_config_t super;
    size_t          mpool_elem_size;
    int             mpool_max_elems;
} ucc_mc_maca_config_t;

typedef struct ucc_mc_maca {
    ucc_mc_base_t super;
    int           stream_initialized;
    mcStream_t    stream;
    ucc_mpool_t   events;
    ucc_mpool_t   strm_reqs;
    ucc_mpool_t   mpool;
    ucc_mpool_t
        mpool_managed; // DEVICE SPECIFIC: Only for device with managed memory
    int mpool_init_flag;
    ucc_spinlock_t    init_spinlock;
    ucc_thread_mode_t thread_mode;
} ucc_mc_maca_t;

extern ucc_mc_maca_t ucc_mc_maca;

#define MC_MACA_CONFIG                                                         \
    (ucc_derived_of(ucc_mc_maca.super.config, ucc_mc_maca_config_t))

#define UCC_MC_MACA_INIT_STREAM()                                              \
    do {                                                                       \
        if (!ucc_mc_maca.stream_initialized) {                                 \
            mcError_t mc_st = mcSuccess;                                       \
            ucs_spin_lock(&ucc_mc_maca.init_spinlock);                         \
            if (!ucc_mc_maca.stream_initialized) {                             \
                mc_st = mcStreamCreateWithFlags(&ucc_mc_maca.stream,           \
                                                mcStreamNonBlocking);          \
                ucc_mc_maca.stream_initialized = 1;                            \
            }                                                                  \
            ucs_spin_unlock(&ucc_mc_maca.init_spinlock);                       \
            if (__builtin_expect(mcSuccess != mc_st, 0)) {                     \
                return maca_error_to_ucc_status(mc_st);                        \
            }                                                                  \
        }                                                                      \
    } while (0)

#endif // UCC_MC_MACA_H_