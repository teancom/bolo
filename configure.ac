# configure.ac

AC_PREREQ(2.68)
AC_INIT([Bolo], [0.2.0], [bugs@niftylogic.com])

AC_CONFIG_SRCDIR([src/bolo.h])
AC_CONFIG_AUX_DIR([build])
AC_CONFIG_MACRO_DIR([build])
AC_CONFIG_HEADERS(config.h)

AM_INIT_AUTOMAKE([-Wall -Werror foreign subdir-objects])

AX_TEST_COVERAGE

AC_PROG_CC

AC_HAVE_LIBRARY(pthread,,  AC_MSG_ERROR(Missing pthread library))
AC_HAVE_LIBRARY(ctap,,     AC_MSG_ERROR(Missing ctap testing library - see http://github.com/filefrog/ctap))
AC_HAVE_LIBRARY(vigor,,    AC_MSG_ERROR(Missing libvigor - see http://github.com/filefrog/libvigor))
AC_HAVE_LIBRARY(zmq,,      AC_MSG_ERROR(Missing 0MQ library))
AC_HAVE_LIBRARY(pcre,,     AC_MSG_ERROR(Missing PCRE library))
AC_HAVE_LIBRARY(rrd,,      AC_MSG_ERROR(Missing RRD library))

AC_CONFIG_FILES([Makefile src/bits.h])
AC_OUTPUT
