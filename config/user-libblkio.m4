dnl #
dnl # Check for libblkio - fast userspace block device access for libzpool
dnl #
AC_DEFUN([ZFS_AC_CONFIG_USER_LIBBLKIO], [
	ZFS_AC_FIND_SYSTEM_LIBRARY(LIBBLKIO, [blkio], [blkio.h], [], [blkio], [], [user_libblkio=yes], [user_libblkio=no])

	AS_IF([test "x$user_libblkio" = xyes], [
	    AX_SAVE_FLAGS

	    CFLAGS="$CFLAGS $LIBBLKIO_CFLAGS"
	    LIBS="$LIBBLKIO_LIBS $LIBS"

	    AC_CHECK_FUNCS([blkio_create])

	    AX_RESTORE_FLAGS
	])

	AM_CONDITIONAL([HAVE_LIBBLKIO], [test "x$user_libblkio" = xyes ])
])
