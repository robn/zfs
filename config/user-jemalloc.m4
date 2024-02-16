dnl #
dnl # Check for jemalloc. If found, we use it to implement umem.
dnl #
AC_DEFUN([ZFS_AC_CONFIG_USER_JEMALLOC], [
	AC_ARG_WITH([jemalloc],
	    AS_HELP_STRING([--with-jemalloc],
		[use jemalloc for userspace memory management]),
	    [],
	    [with_jemalloc=auto])

	AS_IF([test "x$with_jemalloc" != "xno"], [
		ZFS_AC_FIND_SYSTEM_LIBRARY(JEMALLOC, jemalloc, [jemalloc/jemalloc.h],
		    [], [jemalloc], [mallocx], [
			user_jemalloc=yes
		    ], [
			AS_IF([test "x$with_jemalloc" = "xyes"], [
				AC_MSG_FAILURE([--with-jemalloc was given, but jemalloc is not available, try installing jemalloc-devel])
			])
			user_jemalloc=no
		])
	])
])
