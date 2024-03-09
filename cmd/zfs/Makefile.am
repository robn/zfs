sbin_PROGRAMS   += zfs
CPPCHECKTARGETS += zfs

zfs_SOURCES = \
	%D%/zfs_allow.c \
	%D%/zfs_bookmark.c \
	%D%/zfs_change_key.c \
	%D%/zfs_clone.c \
	%D%/zfs_create.c \
	%D%/zfs_destroy.c \
	%D%/zfs_diff.c \
	%D%/zfs_get.c \
	%D%/zfs_hold.c \
	%D%/zfs_holds.c \
	%D%/zfs_inherit.c \
	%D%/zfs_iter.c \
	%D%/zfs_list.c \
	%D%/zfs_load_key.c \
	%D%/zfs_main.c \
	%D%/zfs_mount.c \
	%D%/zfs_program.c \
	%D%/zfs_project.c \
	%D%/zfs_promote.c \
	%D%/zfs_receive.c \
	%D%/zfs_redact.c \
	%D%/zfs_rename.c \
	%D%/zfs_rollback.c \
	%D%/zfs_send.c \
	%D%/zfs_set.c \
	%D%/zfs_snapshot.c \
	%D%/zfs_unmount.c \
	%D%/zfs_upgrade.c \
	%D%/zfs_userspace.c \
	%D%/zfs_wait.c

if BUILD_FREEBSD
zfs_SOURCES += \
	%D%/zfs_jail.c
endif
if BUILD_LINUX
zfs_SOURCES += \
	%D%/zfs_zone.c
endif

zfs_LDADD = \
	libzfs.la \
	libzfs_core.la \
	libnvpair.la \
	libuutil.la

zfs_LDADD += $(LTLIBINTL)

if BUILD_FREEBSD
zfs_LDADD += -lgeom -ljail
endif
