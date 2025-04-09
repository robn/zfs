dnl #
dnl # address_space_operations has a page/folio migration callback. We don't
dnl # use this, but the kernel expects us to provide a callback all the same,
dnl # so we just use kernel's fallback functions.
dnl #
dnl # These fallback functions don't exist if CONFIG_MIGRATION is not set,
dnl # which will cause the tests below to fail. This is fine, as it will just
dnl # cause not to set a callback, but the kernel will never call it anyway,
dnl # so it all works out in the end.
dnl #
dnl # < 6.0: .migratepage = migrate_page
dnl # >=6.0: .migrate_folio = migrate_folio
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_VFS_MIGRATE_PAGE], [
	ZFS_LINUX_TEST_SRC([vfs_has_migrate_page], [
		#include <linux/fs.h>
		#include <linux/migrate.h>

		static const struct address_space_operations
		    aops __attribute__ ((unused)) = {
			.migratepage	= migrate_page,
		};
	],[])
])
AC_DEFUN([ZFS_AC_KERNEL_SRC_VFS_MIGRATE_FOLIO], [
	ZFS_LINUX_TEST_SRC([vfs_has_migrate_folio], [
		#include <linux/fs.h>
		#include <linux/migrate.h>

		static const struct address_space_operations
		    aops __attribute__ ((unused)) = {
			.migrate_folio	= migrate_folio,
		};
	],[])
])

AC_DEFUN([ZFS_AC_KERNEL_VFS_MIGRATE_PAGE], [
	AC_MSG_CHECKING([whether migrate_page exists])
	ZFS_LINUX_TEST_RESULT([vfs_has_migrate_page], [
		AC_MSG_RESULT([yes])
		AC_DEFINE(HAVE_VFS_MIGRATE_PAGE, 1, [migrate_page exists])
	],[
		AC_MSG_RESULT([no])
	])
])
AC_DEFUN([ZFS_AC_KERNEL_VFS_MIGRATE_FOLIO], [
	AC_MSG_CHECKING([whether migrate_folio exists])
	ZFS_LINUX_TEST_RESULT([vfs_has_migrate_folio], [
		AC_MSG_RESULT([yes])
		AC_DEFINE(HAVE_VFS_MIGRATE_FOLIO, 1, [migrate_folio exists])
	],[
		AC_MSG_RESULT([no])
	])
])

AC_DEFUN([ZFS_AC_KERNEL_SRC_VFS_MIGRATE], [
	ZFS_AC_KERNEL_SRC_VFS_MIGRATE_PAGE
	ZFS_AC_KERNEL_SRC_VFS_MIGRATE_FOLIO
])
AC_DEFUN([ZFS_AC_KERNEL_VFS_MIGRATE], [
	ZFS_AC_KERNEL_VFS_MIGRATE_PAGE
	ZFS_AC_KERNEL_VFS_MIGRATE_FOLIO
])
