#
# Copyright (c) 2022, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
#

MXCCFLAGS =                                      \
    ${AM_CPPFLAGS}                               \
    ${UCS_CPPFLAGS}                              \
    ${MXCC_CFLAGS}                               \
    -I${UCC_TOP_BUILDDIR}                        \
    -I${UCC_TOP_SRCDIR}/src                      \
    -I${UCC_TOP_BUILDDIR}/src                    \
    -Wno-unknown-attributes                      \
    -Xcompiler "-fno-rtti,-fno-exceptions"

LINK = $(LIBTOOL) --mode=link $(CC) -o $@

.cu.o:
	$(MXCC) -c $< -o $@  $(MXCCFLAGS)

.cu.lo:
	/bin/bash $(top_srcdir)/cuda_lt.sh "$(LIBTOOL)" $@ $(MXCC) -c  $< $(MXCCFLAGS)

comp_noinst = libucc_ec_maca_kernels.la

libucc_ec_maca_kernels_la_SOURCES  = ec_maca_wait_kernel.cu  \
                                     ec_maca_executor.cu     \
                                     ec_maca_reduce.cu
libucc_ec_maca_kernels_la_CPPFLAGS =

noinst_LTLIBRARIES = $(comp_noinst)