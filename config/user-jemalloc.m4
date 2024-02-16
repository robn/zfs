dnl #
dnl # Check for jemalloc. If found, we use it to implement umem.
dnl #
AC_DEFUN([ZFS_AC_CONFIG_USER_JEMALLOC], [
	ZFS_AC_FIND_SYSTEM_LIBRARY(JEMALLOC, jemalloc, [jemalloc/jemalloc.h],
	    [], [jemalloc], [mallocx], [user_jemalloc=yes], [user_jemalloc=no])
])
