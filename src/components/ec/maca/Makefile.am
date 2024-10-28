#
# Copyright (c) 2022, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
#

if HAVE_MACA
SUBDIRS = kernel

sources =    \
	ec_maca.h                          \
	ec_maca.c                          \
	ec_maca_executor.h                 \
	ec_maca_executor.c                 \
	ec_maca_executor_interruptible.c   \
	ec_maca_executor_persistent.c      \
	ec_maca_executor_persistent_wait.c

module_LTLIBRARIES         = libucc_ec_maca.la
libucc_ec_maca_la_SOURCES  = $(sources)
libucc_ec_maca_la_CPPFLAGS = $(AM_CPPFLAGS) $(BASE_CPPFLAGS) $(MACA_CPPFLAGS)
libucc_ec_maca_la_CFLAGS   = $(BASE_CFLAGS)
libucc_ec_maca_la_LDFLAGS  = -version-info $(SOVERSION) --as-needed $(MACA_LDFLAGS)
libucc_ec_maca_la_LIBADD   = $(MACA_LIBS)                      \
                             $(UCC_TOP_BUILDDIR)/src/libucc.la \
                             kernel/libucc_ec_maca_kernels.la

include $(top_srcdir)/config/module.am
endif