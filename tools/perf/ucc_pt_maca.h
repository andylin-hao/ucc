#ifndef UCC_PT_MACA_H
#define UCC_PT_MACA_H
#include <iostream>

#define mcSuccess 0
#define mcStreamNonBlocking 0x01  /**< Stream does not synchronize with stream 0 (the NULL stream) */
#define mcMemAttachGlobal   0x01  /**< Memory can be accessed by any stream on any device*/
typedef struct MCstream_st *mcStream_t;

#define STR(x) #x
#define MACA_CHECK(_call)                                       \
    do {                                                        \
        int _status = (_call);                                  \
        if (mcSuccess != _status) {                           \
            std::cerr << "UCC perftest error: " <<              \
                ucc_pt_maca_iface.getErrorString(_status)       \
                      << " in " << STR(_call) << "\n";          \
            return _status;                                     \
        }                                                       \
    } while (0)

typedef struct ucc_pt_maca_iface {
    int available;
    int (*getDeviceCount)(int* count);
    int (*setDevice)(int device);
    int (*streamCreateWithFlags)(mcStream_t *stream, unsigned int flags);
    int (*streamDestroy)(mcStream_t stream);
    char* (*getErrorString)(int err);
    int (*mcMalloc)(void **devptr, size_t size);
    int (*mcMallocManaged)(void **ptr, size_t size, unsigned int flags);
    int (*mcFree)(void *devptr);
    int (*mcMemset)(void *devptr, int value, size_t count);
} ucc_pt_maca_iface_t;

extern ucc_pt_maca_iface_t ucc_pt_maca_iface;

void ucc_pt_maca_init(void);

static inline int ucc_pt_mcGetDeviceCount(int *count)
{
    if (!ucc_pt_maca_iface.available) {
        return 1;
    }
    MACA_CHECK(ucc_pt_maca_iface.getDeviceCount(count));
    return 0;
}

static inline int ucc_pt_mcSetDevice(int device)
{
    if (!ucc_pt_maca_iface.available) {
        return 1;
    }
    MACA_CHECK(ucc_pt_maca_iface.setDevice(device));
    return 0;
}

static inline int ucc_pt_mcStreamCreateWithFlags(mcStream_t *stream,
                                                   unsigned int flags)
{
    if (!ucc_pt_maca_iface.available) {
        return 1;
    }
    MACA_CHECK(ucc_pt_maca_iface.streamCreateWithFlags(stream, flags));
    return 0;
}

static inline int ucc_pt_mcStreamDestroy(mcStream_t stream)
{
    if (!ucc_pt_maca_iface.available) {
        return 1;
    }
    MACA_CHECK(ucc_pt_maca_iface.streamDestroy(stream));
    return 0;
}

static inline int ucc_pt_mcMalloc(void **devptr, size_t size)
{
    if (!ucc_pt_maca_iface.available) {
        return 1;
    }
    MACA_CHECK(ucc_pt_maca_iface.mcMalloc(devptr, size));
    return 0;
}

static inline int ucc_pt_mcMallocManaged(void **ptr, size_t size)
{
    if (!ucc_pt_maca_iface.available) {
        return 1;
    }
    MACA_CHECK(ucc_pt_maca_iface.mcMallocManaged(ptr, size,
               mcMemAttachGlobal));
    return 0;
}

static inline int ucc_pt_mcFree(void *devptr)
{
    if (!ucc_pt_maca_iface.available) {
        return 1;
    }
    MACA_CHECK(ucc_pt_maca_iface.mcFree(devptr));
    return 0;
}

static inline int ucc_pt_mcMemset(void *devptr, int value, size_t count)
{
    if (!ucc_pt_maca_iface.available) {
        return 1;
    }
    MACA_CHECK(ucc_pt_maca_iface.mcMemset(devptr, value, count));
    return 0;
}

#endif