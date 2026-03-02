dnl # SPDX-License-Identifier: CDDL-1.0
dnl #
dnl # On Linux we use libmount for mount/unmount operations.
dnl #
AC_DEFUN([ZFS_AC_CONFIG_USER_LIBMOUNT], [
	ZFS_AC_FIND_SYSTEM_LIBRARY(LIBMOUNT, [mount], [libmount/libmount.h], [], [mount], [], [], [
		AC_MSG_FAILURE([
		*** libmount.h missing, libmount-devel package required])])
])

