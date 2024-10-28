
/**
* Copyright (c) 2022, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
*
* See file LICENSE for terms.
*/

#ifndef UCC_CUDA_DEF_H
#define UCC_CUDA_DEF_H

#include "config.h"

#if HAVE_MACA

#include "utils/ucc_log.h"
#include <mc_runtime.h>

static inline ucc_status_t maca_error_to_ucc_status(mcError_t maca_status)
{
    ucc_status_t ucc_status;

    switch(maca_status) {
    case mcSuccess:
        ucc_status = UCC_OK;
        break;
    case mcErrorNotReady:
        ucc_status = UCC_INPROGRESS;
        break;
    case mcErrorInvalidValue:
        ucc_status = UCC_ERR_INVALID_PARAM;
        break;
    default:
        ucc_status = UCC_ERR_NO_MESSAGE;
    }
    return ucc_status;
}

#define MACA_FUNC(_func)                                                       \
    ({                                                                         \
        ucc_status_t _status;                                                  \
        do {                                                                   \
            mcError_t _result = (_func);                                     \
            if (ucc_unlikely(mcSuccess != _result)) {                        \
                ucc_error("%s() failed: %d(%s)",                               \
                          #_func, _result, mcGetErrorString(_result));       \
            }                                                                  \
            _status = maca_error_to_ucc_status(_result);                       \
        } while (0);                                                           \
        _status;                                                               \
    })

#define MACA_CHECK(_cmd)                                                       \
    /* coverity[dead_error_line] */                                            \
    do {                                                                       \
        ucc_status_t _maca_status = MACA_FUNC(_cmd);                           \
        if (ucc_unlikely(_maca_status != UCC_OK)) {                            \
            return _maca_status;                                               \
        }                                                                      \
    } while(0)

#define MACA_CHECK_GOTO(_cmd, _label, _maca_status)                            \
    do {                                                                       \
        _maca_status = MACA_FUNC(_cmd);                                        \
        if (ucc_unlikely(_maca_status != UCC_OK)) {                            \
            goto _label;                                                       \
        }                                                                      \
    } while (0)

#endif

#endif