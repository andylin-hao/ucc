#include "ucc_pt_maca.h"
#include <iostream>
#include <dlfcn.h>
#include <stdlib.h>

ucc_pt_maca_iface_t ucc_pt_maca_iface = {
    .available = 0,
};

#define LOAD_MACA_SYM(_sym, _pt_sym) ({                                    \
            void *h = dlsym(handle, _sym);                                 \
            if (dlerror() != NULL)  {                                      \
                return;                                                    \
            }                                                              \
            ucc_pt_maca_iface. _pt_sym =                                   \
                reinterpret_cast<decltype(ucc_pt_maca_iface. _pt_sym)>(h); \
        })

void ucc_pt_maca_init(void)
{
    void *handle;

    handle = dlopen ("libmcruntime.so", RTLD_LAZY);
    if (!handle) {
        return;
    }

    LOAD_MACA_SYM("mcGetDeviceCount", getDeviceCount);
    LOAD_MACA_SYM("mcSetDevice", setDevice);
    LOAD_MACA_SYM("mcGetErrorString", getErrorString);
    LOAD_MACA_SYM("mcStreamCreateWithFlags", streamCreateWithFlags);
    LOAD_MACA_SYM("mcStreamDestroy", streamDestroy);
    LOAD_MACA_SYM("mcMalloc", mcMalloc);
    LOAD_MACA_SYM("mcFree", mcFree);
    LOAD_MACA_SYM("mcMemset", mcMemset);
    LOAD_MACA_SYM("mcMallocManaged", mcMallocManaged);

    ucc_pt_maca_iface.available = 1;
}
