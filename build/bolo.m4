dnl =============================================================
dnl BOLO_SUBSCRIBER(name, default, description, test-prog, libs)
dnl
dnl Sets up --with-X-subscriber and --without-X-subscriber
dnl arguments to ./configure, which can be used to include
dnl or exclude specific subscribers.
dnl
dnl Parameters:
dnl   name:        Name of the subscriber; used for generating the
dnl                command-line flag and the Makefile.am variable
dnl
dnl   default:     Default value for the argument, one of 'yes',
dnl                'no' or 'auto'
dnl
dnl   description: A string to be used in the ./configure --help
dnl                screen, which completes the sentence
dnl                "which ..."
dnl
dnl   test-prog:   A test C program that will be used to determine
dnl                if the host platform has the correct header files
dnl                and dynamic libraries to compile the subscriber.
dnl
dnl                This is used in 'auto' mode to determine if the
dnl                subscriber should be built, and in 'yes' mode to
dnl                ensure that the user's request can be honored.
dnl
dnl   libs:        Additional library flags for compiling the test
dnl                program, i.e. [-lm -lrt -lvigor]
dnl
dnl
dnl Each call to BOLO_SUBSCRIBER() will set up an Automake variable
dnl named 'build_X_subscriber', which can be used to conditionally
dnl enable compile targets and update the sbin_PROGRAMS var.
dnl
dnl =============================================================
AC_DEFUN([BOLO_SUBSCRIBER],
	# Set up the --with-X-subscriber argument handler
	[AC_ARG_WITH([$1-subscriber],
		[AS_HELP_STRING([--with-$1-subscriber],
			[Build the `$1` subscriber, which $3 (default is $2)])],
		[case "${withval}" in
		 yes)  build_$1_subscriber=yes;  AC_MSG_NOTICE([Will build the $1 subscriber])            ;;
		 no)   build_$1_subscriber=no;   AC_MSG_NOTICE([Will not build the $1 subscriber])        ;;
		 auto) build_$1_subscriber=auto; AC_MSG_NOTICE([Will attempt to build the $1 subscriber]) ;;
		 *)    AC_MSG_ERROR([bad value ${withval} for --with-$1-subscriber]) ;;
		 esac],
		[build_$1_subscriber=$build_ALL])

	# Under 'yes' and 'auto', try to determine if we meet our
	# pre-requisites (from both header and library perspectives)
	 AS_IF([test "x$build_$1_subscriber" != "xno"],
		[bolo_prereqs_save_LIBS=$LIBS
		 LIBS="$5 $LIBS"
		 AC_MSG_CHECKING([for `$1` subscriber prereqs])
		 AC_CACHE_VAL([bolo_cv_$1_ok],
			[AC_LINK_IFELSE([AC_LANG_SOURCE([$4])],
			[bolo_cv_$1_ok=yes],
			[bolo_cv_$1_ok=no])])
		 AC_MSG_RESULT([$bolo_cv_$1_ok])
		 LIBS=$bolo_prereqs_save_LIBS

		 # If we don't have the support we need...
		 AS_IF([test "x$bolo_cv_$1_ok" = "xno"],
			AS_IF([test "x$build_$1_subscriber" = "xyes"],
				# And we specified --with-X=yes, it's a hard failure
				[AC_MSG_ERROR([--with-$1-subscriber: no support for $1 subscriber])],
				# ... otherwise autodetection set to no
				[build_$1_subscriber=no]))
		])

	 AM_CONDITIONAL([build_$1_subscriber], [test "x$build_$1_subscriber" != "xno"])
	])

dnl =============================================================
dnl BOLO_WITH(name, description)
dnl
dnl Sets up --with-X and --without-X arguments to ./configure, and
dnl the necessary handler code to update both CFLAGS and LDFLAGS,
dnl based on arguments and on-disk directories.
dnl
dnl The basic principle is this: if the caller gives us a path
dnl to one of these --with-X arguments, we look for library (lib/)
dnl and header file (include/) directories under the path, and adjust
dnl LDFLAGS and CFLAGS accordingly with new -L and -I flags.
dnl
dnl =============================================================
AC_DEFUN([BOLO_WITH],
	# Set up CFLAGS and LDFLAGS using the --with-X=<path> idiom
	[AC_ARG_WITH([$1],
		[AS_HELP_STRING([--with-$1],[Where to find $2])],
		[if test $withval != "yes" -a $withval != "no"; then
			prefix=${withval%%/}
			if test -d "$prefix/lib/";     then LDFLAGS="$LDFLAGS -L$prefix/lib";   fi
			if test -d "$prefix/include/"; then CFLAGS="$CFLAGS -I$prefix/include"; fi
		fi],[])
	])
