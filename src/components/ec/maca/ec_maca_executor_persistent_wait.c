#include "ec_maca_executor.h"
#include "ucc/api/ucc_status.h"
#include "utils/arch/x86_64/cpu.h"

ucc_status_t
ucc_ec_maca_post_kernel_stream_task(ucc_ec_maca_executor_state_t *state,
                                    mcStream_t                    stream);
                                    
static ucc_status_t
ucc_ec_maca_post_driver_stream_task(ucc_ec_maca_executor_state_t *state,
                                    mcStream_t                    stream)
{
    mcDeviceptr_t state_ptr = (mcDeviceptr_t)state;

    MACA_FUNC(mcStreamWriteValue32(stream, state_ptr,
                                   UCC_EC_MACA_EXECUTOR_STARTED, 0));
    MACA_FUNC(mcStreamWaitValue32(stream, state_ptr,
                                  UCC_EC_MACA_EXECUTOR_SHUTDOWN,
                                  MC_STREAM_WAIT_VALUE_EQ));
    MACA_FUNC(mcStreamWriteValue32(stream, state_ptr,
                                   UCC_EC_MACA_EXECUTOR_SHUTDOWN_ACK, 0));
    return UCC_OK;
}

// This is only used for stream synchronization before checking CC progress, see https://github.com/openucx/ucc/pull/691
ucc_status_t
ucc_maca_executor_persistent_wait_start(ucc_ee_executor_t *executor,
                                        void *             ee_context)
{
    ucc_ec_maca_executor_t *eee =
        ucc_derived_of(executor, ucc_ec_maca_executor_t);
    mcStream_t stream = (mcStream_t)ee_context;

    eee->super.ee_context = ee_context;
    eee->state            = UCC_EC_MACA_EXECUTOR_POSTED;
    eee->mode             = UCC_EC_MACA_EXECUTOR_MODE_PERSISTENT;

    ucc_memory_cpu_store_fence();
    if (ucc_ec_maca.strm_task_mode == UCC_EC_MACA_TASK_KERNEL) {
        return ucc_ec_maca_post_kernel_stream_task(eee->dev_state, stream);
    } else {
        return ucc_ec_maca_post_driver_stream_task(eee->dev_state, stream);
    }
}

ucc_status_t ucc_maca_executor_persistent_wait_stop(ucc_ee_executor_t *executor)
{
    ucc_ec_maca_executor_t *eee =
        ucc_derived_of(executor, ucc_ec_maca_executor_t);
    volatile ucc_ec_maca_executor_state_t *st = &eee->state;

    ec_debug(&ucc_ec_maca.super, "executor stop, eee: %p", eee);
    ucc_assert((*st != UCC_EC_MACA_EXECUTOR_POSTED) &&
               (*st != UCC_EC_MACA_EXECUTOR_SHUTDOWN));
    *st = UCC_EC_MACA_EXECUTOR_SHUTDOWN;
    while (*st != UCC_EC_MACA_EXECUTOR_SHUTDOWN_ACK) {
    }
    eee->super.ee_context = NULL;
    eee->state            = UCC_EC_MACA_EXECUTOR_INITIALIZED;

    return UCC_OK;
}
