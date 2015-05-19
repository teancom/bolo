#
#  Copyright 2015 James Hunt <james@jameshunt.us>
#
#  This file is part of Bolo.
#
#  Bolo is free software: you can redistribute it and/or modify it under the
#  terms of the GNU General Public License as published by the Free Software
#  Foundation, either version 3 of the License, or (at your option) any later
#  version.
#
#  Bolo is distributed in the hope that it will be useful, but WITHOUT ANY
#  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
#  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
#  details.
#
#  You should have received a copy of the GNU General Public License along
#  with Bolo.  If not, see <http://www.gnu.org/licenses/>.
#

AC_PREREQ(2.68)
AC_INIT([Bolo], [0.2.2], [bugs@niftylogic.com])

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
