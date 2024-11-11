
MACA_MIN_REQUIRED_MAJOR=1
MACA_MIN_REQUIRED_MINOR=0

# ARCHS = "xcore1000,xcore1002,xcore1010,xcore1020,xcore1100,xcore1500,g100"

AC_DEFUN([CHECK_MACA],[
AS_IF([test "x$maca_checked" != "xyes"],
   [
    AC_ARG_WITH([maca],
                [AS_HELP_STRING([--with-maca=(DIR)], [Enable the use of MACA (default is guess).])],
                [], [with_maca=guess])
    AC_ARG_WITH([maca-device-arch],
                [AS_HELP_STRING([--with-maca-device-arch=archs],
                                [Defines target GPU architecture,
                                 see mxcc --list-gpu-arch option for details])],
                [], [with_maca_device_arch=default])
    AS_IF([test "x$with_maca" = "xno"],
        [
         maca_happy=no
        ],
        [
         save_CPPFLAGS="$CPPFLAGS"
         save_LDFLAGS="$LDFLAGS"
         save_LIBS="$LIBS"
         MACA_CPPFLAGS=""
         MACA_LDFLAGS=""
         MACA_LIBS=""
         AS_IF([test ! -z "$with_maca" -a "x$with_maca" != "xyes" -a "x$with_maca" != "xguess"],
               [check_maca_dir="$with_maca"
                check_maca_libdir="$with_maca/lib"
                MACA_CPPFLAGS="-I$with_maca/include -I$with_maca/include/mcr"
                MACA_LDFLAGS="-L$check_maca_libdir"])
         AS_IF([test ! -z "$with_maca_libdir" -a "x$with_maca_libdir" != "xyes"],
               [check_maca_libdir="$with_maca_libdir"
                MACA_LDFLAGS="-L$check_maca_libdir -L$check_maca_libdir/stubs"])
         CPPFLAGS="$CPPFLAGS $MACA_CPPFLAGS"
         LDFLAGS="$LDFLAGS $MACA_LDFLAGS"
         # Check maca header files
         AC_CHECK_HEADERS([maca.h mc_runtime.h],
                          [maca_happy="yes"], [maca_happy="no"])
         # Check maca libraries
         AS_IF([test "x$maca_happy" = "xyes"],
               [AC_CHECK_LIB([mcruntime], [mcDeviceGetUuid],
                             [MACA_LIBS="$MACA_LIBS -lmcruntime"], [maca_happy="no"])])
         AS_IF([test "x$maca_happy" = "xyes"],
               [AC_CHECK_LIB([mcruntime], [mcGetDeviceCount],
                             [MACA_LIBS="$MACA_LIBS -lmcruntime"], [maca_happy="no"])])

         AC_CHECK_SIZEOF(mcFloatComplex,,[#include <common/mcComplex.h>])
         AC_CHECK_SIZEOF(mcDoubleComplex,,[#include <common/mcComplex.h>])
         # Check for MXCC
         AC_ARG_VAR(MXCC, [MXCC compiler command])
         AS_IF([test "x$maca_happy" = "xyes"],
               [AC_PATH_PROG([MXCC], [mxcc], [notfound], [$PATH:$check_maca_dir/bin])])
         AS_IF([test "$NVCC" = "notfound"], [maca_happy="no"])
         AS_IF([test "x$enable_debug" = xyes],
               [MXCC_CFLAGS="$MXCC_CFLAGS -O0 -g"],
               [MXCC_CFLAGS="$MXCC_CFLAGS -O3 -g -DNDEBUG"])
      #    AS_IF([test "x$maca_happy" = "xyes"],
      #          [AS_IF([test "x$with_maca_device_arch" = "xdefault"],
      #                 [AS_IF([test $MACA_MAJOR_VERSION -eq 12],
      #                        [NVCC_ARCH="${ARCH7} ${ARCH8} ${ARCH9} ${ARCH10} ${ARCH110} ${ARCH111} ${ARCH120}"],
      #                        [AS_IF([test $MACA_MAJOR_VERSION -eq 11],
      #                              [AS_IF([test $MACA_MINOR_VERSION -lt 1],
      #                                      [NVCC_ARCH="${ARCH7} ${ARCH8} ${ARCH9} ${ARCH10} ${ARCH110}"],
      #                                      [NVCC_ARCH="${ARCH7} ${ARCH8} ${ARCH9} ${ARCH10} ${ARCH110} ${ARCH111}"])])])],
      #                 [NVCC_ARCH="$with_nvcc_gencode"])
      #           AC_SUBST([NVCC_ARCH], ["$NVCC_ARCH"])])
         LDFLAGS="$save_LDFLAGS"
         CPPFLAGS="$save_CPPFLAGS"
         LDFLAGS="$save_LDFLAGS"
         LIBS="$save_LIBS"
         AS_IF([test "x$maca_happy" = "xyes"],
               [AC_SUBST([MACA_CPPFLAGS], ["$MACA_CPPFLAGS"])
                AC_SUBST([MACA_LDFLAGS], ["$MACA_LDFLAGS"])
                AC_SUBST([MACA_LIBS], ["$MACA_LIBS"])
                AC_SUBST([MXCC_CFLAGS], ["$MXCC_CFLAGS"])
                AC_DEFINE([HAVE_MACA], 1, [Enable MACA support])],
               [AS_IF([test "x$with_maca" != "xguess"],
                      [AC_MSG_ERROR([MACA support is requested but maca packages cannot be found])],
                      [AC_MSG_WARN([MACA not found])])])
        ]) # "x$with_maca" = "xno"
        maca_checked=yes
        AM_CONDITIONAL([HAVE_MACA], [test "x$maca_happy" != xno])
   ]) # "x$maca_checked" != "xyes"
]) # CHECK_MACA
