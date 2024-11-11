
#
# Copyright (c) 2021, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
#

if HAVE_MACA

sources =     \
	mc_maca.h \
	mc_maca.c

module_LTLIBRARIES         = libucc_mc_maca.la
libucc_mc_maca_la_SOURCES  = $(sources)
libucc_mc_maca_la_CPPFLAGS = $(AM_CPPFLAGS) $(BASE_CPPFLAGS) $(MACA_CPPFLAGS)
libucc_mc_maca_la_CFLAGS   = $(BASE_CFLAGS)
libucc_mc_maca_la_LDFLAGS  = -version-info $(SOVERSION) --as-needed $(MACA_LDFLAGS)
libucc_mc_maca_la_LIBADD   = $(MACA_LIBS)                      \
                             $(UCC_TOP_BUILDDIR)/src/libucc.la

include $(top_srcdir)/config/module.am
endif
