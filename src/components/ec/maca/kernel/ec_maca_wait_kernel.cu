#ifndef UINT32_MAX
#define __STDC_LIMIT_MACROS
#include <stdint.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif
#include "../ec_maca.h"
#ifdef __cplusplus
}
#endif

__global__ void wait_kernel(volatile ucc_ec_maca_executor_state_t *state) {
    ucc_ec_maca_executor_state_t st;

    *state = UCC_EC_MACA_EXECUTOR_STARTED;
    do {
        st = *state;
    } while (st != UCC_EC_MACA_EXECUTOR_SHUTDOWN);
    *state = UCC_EC_MACA_EXECUTOR_SHUTDOWN_ACK;
    return;
}

#ifdef __cplusplus
extern "C" {
#endif

ucc_status_t
ucc_ec_maca_post_kernel_stream_task(ucc_ec_maca_executor_state_t *state,
                                    mcStream_t stream)
{
    wait_kernel<<<1, 1, 0, stream>>>(state);
    MACA_CHECK(mcGetLastError());
    return UCC_OK;
}

#ifdef __cplusplus
}
#endif