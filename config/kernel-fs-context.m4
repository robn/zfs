dnl # SPDX-License-Identifier: CDDL-1.0
dnl #
dnl # 5.1 API change
dnl # New mount API introduced based on struct fs_context
dnl # Old API maintained through compatibility wrappers
dnl #
dnl # 7.0 API change
dnl # Compat API removed
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_FS_CONTEXT], [
	ZFS_LINUX_TEST_SRC([fs_context], [
		#include <linux/fs.h>
		#include <linux/fs_context.h>

		static struct fs_context fc __attribute__ ((unused)) = {0};
	], [])
])

AC_DEFUN([ZFS_AC_KERNEL_FS_CONTEXT], [
	AC_MSG_CHECKING([whether struct fs_context exists])
	ZFS_LINUX_TEST_RESULT([fs_context], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_FS_CONTEXT, 1, [struct fs_context exists])
	],[
		AC_MSG_RESULT(no)
        ])
])
