dnl #
dnl # Check for mimalloc. If found, we use it to implement umem.
dnl #
AC_DEFUN([ZFS_AC_CONFIG_USER_MIMALLOC], [
	AC_ARG_WITH([mimalloc],
	    AS_HELP_STRING([--with-mimalloc],
		[use mimalloc for userspace memory management]),
	    [],
	    [with_mimalloc=auto])

	AS_IF([test "x$with_mimalloc" != "xno"], [
		ZFS_AC_FIND_SYSTEM_LIBRARY(MIMALLOC, mimalloc, [mimalloc.h],
		    [], [mimalloc], [mi_malloc], [
			user_mimalloc=yes
		    ], [
			AS_IF([test "x$with_mimalloc" = "xyes"], [
				AC_MSG_FAILURE([--with-mimalloc was given, but mimalloc is not available, try installing mimalloc-devel])
			])
			user_mimalloc=no
		])
	])
])
