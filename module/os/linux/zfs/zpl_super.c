// SPDX-License-Identifier: CDDL-1.0
/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or https://opensource.org/licenses/CDDL-1.0.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright (c) 2011, Lawrence Livermore National Security, LLC.
 * Copyright (c) 2023, Datto Inc. All rights reserved.
 * Copyright (c) 2025, Klara, Inc.
 * Copyright (c) 2025, Rob Norris <robn@despairlabs.com>
 * Copyright (c) 2026, TrueNAS.
 */


#include <sys/zfs_znode.h>
#include <sys/zfs_vfsops.h>
#include <sys/zfs_vnops.h>
#include <sys/zfs_ctldir.h>
#include <sys/zpl.h>
#include <linux/iversion.h>
#include <linux/version.h>
#include <linux/vfs_compat.h>
#ifdef HAVE_FS_CONTEXT
#include <sys/dsl_prop.h>
#include <linux/fs_context.h>
#include <linux/fs_parser.h>
#endif

/*
 * What to do when the last reference to an inode is released. If 0, the kernel
 * will cache it on the superblock. If 1, the inode will be freed immediately.
 * See zpl_drop_inode().
 */
int zfs_delete_inode = 0;

/*
 * What to do when the last reference to a dentry is released. If 0, the kernel
 * will cache it until the entry (file) is destroyed. If 1, the dentry will be
 * marked for cleanup, at which time its inode reference will be released. See
 * zpl_dentry_delete().
 */
int zfs_delete_dentry = 0;

static struct inode *
zpl_inode_alloc(struct super_block *sb)
{
	struct inode *ip;

	VERIFY3S(zfs_inode_alloc(sb, &ip), ==, 0);
	inode_set_iversion(ip, 1);

	return (ip);
}

#ifdef HAVE_SOPS_FREE_INODE
static void
zpl_inode_free(struct inode *ip)
{
	ASSERT0(atomic_read(&ip->i_count));
	zfs_inode_free(ip);
}
#endif

static void
zpl_inode_destroy(struct inode *ip)
{
	ASSERT0(atomic_read(&ip->i_count));
	zfs_inode_destroy(ip);
}

/*
 * Called from __mark_inode_dirty() to reflect that something in the
 * inode has changed.  We use it to ensure the znode system attributes
 * are always strictly update to date with respect to the inode.
 */
static void
zpl_dirty_inode(struct inode *ip, int flags)
{
	fstrans_cookie_t cookie;

	cookie = spl_fstrans_mark();
	zfs_dirty_inode(ip, flags);
	spl_fstrans_unmark(cookie);
}

/*
 * ->drop_inode() is called when the last reference to an inode is released.
 * Its return value indicates if the inode should be destroyed immediately, or
 * cached on the superblock structure.
 *
 * By default (zfs_delete_inode=0), we call generic_drop_inode(), which returns
 * "destroy immediately" if the inode is unhashed and has no links (roughly: no
 * longer exists on disk). On datasets with millions of rarely-accessed files,
 * this can cause a large amount of memory to be "pinned" by cached inodes,
 * which in turn pin their associated dnodes and dbufs, until the kernel starts
 * reporting memory pressure and requests OpenZFS release some memory (see
 * zfs_prune()).
 *
 * When set to 1, we call generic_delete_inode(), which always returns "destroy
 * immediately", resulting in inodes being destroyed immediately, releasing
 * their associated dnodes and dbufs to the dbuf cached and the ARC to be
 * evicted as normal.
 *
 * Note that the "last reference" doesn't always mean the last _userspace_
 * reference; the dentry cache also holds a reference, so "busy" inodes will
 * still be kept alive that way (subject to dcache tuning).
 */
static int
zpl_drop_inode(struct inode *ip)
{
	if (zfs_delete_inode)
		return (generic_delete_inode(ip));
	return (generic_drop_inode(ip));
}

/*
 * The ->evict_inode() callback must minimally truncate the inode pages,
 * and call clear_inode().  For 2.6.35 and later kernels this will
 * simply update the inode state, with the sync occurring before the
 * truncate in evict().  For earlier kernels clear_inode() maps to
 * end_writeback() which is responsible for completing all outstanding
 * write back.  In either case, once this is done it is safe to cleanup
 * any remaining inode specific data via zfs_inactive().
 * remaining filesystem specific data.
 */
static void
zpl_evict_inode(struct inode *ip)
{
	fstrans_cookie_t cookie;

	cookie = spl_fstrans_mark();
	truncate_setsize(ip, 0);
	clear_inode(ip);
	zfs_inactive(ip);
	spl_fstrans_unmark(cookie);
}

static void
zpl_put_super(struct super_block *sb)
{
	fstrans_cookie_t cookie;
	int error;

	cookie = spl_fstrans_mark();
	error = -zfs_umount(sb);
	spl_fstrans_unmark(cookie);
	ASSERT3S(error, <=, 0);
}

/*
 * zfs_sync() is the underlying implementation for the sync(2) and syncfs(2)
 * syscalls, via sb->s_op->sync_fs().
 *
 * Before kernel 5.17 (torvalds/linux@5679897eb104), syncfs() ->
 * sync_filesystem() would ignore the return from sync_fs(), instead only
 * considing the error from syncing the underlying block device (sb->s_dev).
 * Since OpenZFS doesn't _have_ an underlying block device, there's no way for
 * us to report a sync directly.
 *
 * However, in 5.8 (torvalds/linux@735e4ae5ba28) the superblock gained an extra
 * error store `s_wb_err`, to carry errors seen on page writeback since the
 * last call to syncfs(). If sync_filesystem() does not return an error, any
 * existing writeback error on the superblock will be used instead (and cleared
 * either way). We don't use this (page writeback is a different thing for us),
 * so for 5.8-5.17 we can use that instead to get syncfs() to return the error.
 *
 * Before 5.8, we have no other good options - no matter what happens, the
 * userspace program will be told the call has succeeded, and so we must make
 * it so, Therefore, when we are asked to wait for sync to complete (wait ==
 * 1), if zfs_sync() has returned an error we have no choice but to block,
 * regardless of the reason.
 *
 * The 5.17 change was backported to the 5.10, 5.15 and 5.16 series, and likely
 * to some vendor kernels. Meanwhile, s_wb_err is still in use in 6.15 (the
 * mainline Linux series at time of writing), and has likely been backported to
 * vendor kernels before 5.8. We don't really want to use a workaround when we
 * don't have to, but we can't really detect whether or not sync_filesystem()
 * will return our errors (without a difficult runtime test anyway). So, we use
 * a static version check: any kernel reporting its version as 5.17+ will use a
 * direct error return, otherwise, we'll either use s_wb_err if it was detected
 * at configure (5.8-5.16 + vendor backports). If it's unavailable, we will
 * block to ensure the correct semantics.
 *
 * See https://github.com/openzfs/zfs/issues/17416 for further discussion.
 */
static int
zpl_sync_fs(struct super_block *sb, int wait)
{
	fstrans_cookie_t cookie;
	cred_t *cr = CRED();
	int error;

	crhold(cr);
	cookie = spl_fstrans_mark();
	error = -zfs_sync(sb, wait, cr);

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 17, 0)
#ifdef HAVE_SUPER_BLOCK_S_WB_ERR
	if (error && wait)
		errseq_set(&sb->s_wb_err, error);
#else
	if (error && wait) {
		zfsvfs_t *zfsvfs = sb->s_fs_info;
		ASSERT3P(zfsvfs, !=, NULL);
		if (zfs_enter(zfsvfs, FTAG) == 0) {
			txg_wait_synced(dmu_objset_pool(zfsvfs->z_os), 0);
			zfs_exit(zfsvfs, FTAG);
			error = 0;
		}
	}
#endif
#endif /* < 5.17.0 */

	spl_fstrans_unmark(cookie);
	crfree(cr);

	ASSERT3S(error, <=, 0);
	return (error);
}

static int
zpl_statfs(struct dentry *dentry, struct kstatfs *statp)
{
	fstrans_cookie_t cookie;
	int error;

	cookie = spl_fstrans_mark();
	error = -zfs_statvfs(dentry->d_inode, statp);
	spl_fstrans_unmark(cookie);
	ASSERT3S(error, <=, 0);

	/*
	 * If required by a 32-bit system call, dynamically scale the
	 * block size up to 16MiB and decrease the block counts.  This
	 * allows for a maximum size of 64EiB to be reported.  The file
	 * counts must be artificially capped at 2^32-1.
	 */
	if (unlikely(zpl_is_32bit_api())) {
		while (statp->f_blocks > UINT32_MAX &&
		    statp->f_bsize < SPA_MAXBLOCKSIZE) {
			statp->f_frsize <<= 1;
			statp->f_bsize <<= 1;

			statp->f_blocks >>= 1;
			statp->f_bfree >>= 1;
			statp->f_bavail >>= 1;
		}

		uint64_t usedobjs = statp->f_files - statp->f_ffree;
		statp->f_ffree = MIN(statp->f_ffree, UINT32_MAX - usedobjs);
		statp->f_files = statp->f_ffree + usedobjs;
	}

	return (error);
}

static int
zpl_remount_fs(struct super_block *sb, int *flags, char *data)
{
	zfs_mnt_t zm = { .mnt_osname = NULL, .mnt_data = data };
	fstrans_cookie_t cookie;
	int error;

	cookie = spl_fstrans_mark();
	error = -zfs_remount(sb, flags, &zm);
	spl_fstrans_unmark(cookie);
	ASSERT3S(error, <=, 0);

	return (error);
}

static int
__zpl_show_devname(struct seq_file *seq, zfsvfs_t *zfsvfs)
{
	int error;
	if ((error = zpl_enter(zfsvfs, FTAG)) != 0)
		return (error);

	char *fsname = kmem_alloc(ZFS_MAX_DATASET_NAME_LEN, KM_SLEEP);
	dmu_objset_name(zfsvfs->z_os, fsname);

	for (int i = 0; fsname[i] != 0; i++) {
		/*
		 * Spaces in the dataset name must be converted to their
		 * octal escape sequence for getmntent(3) to correctly
		 * parse then fsname portion of /proc/self/mounts.
		 */
		if (fsname[i] == ' ') {
			seq_puts(seq, "\\040");
		} else {
			seq_putc(seq, fsname[i]);
		}
	}

	kmem_free(fsname, ZFS_MAX_DATASET_NAME_LEN);

	zpl_exit(zfsvfs, FTAG);

	return (0);
}

static int
zpl_show_devname(struct seq_file *seq, struct dentry *root)
{
	return (__zpl_show_devname(seq, ITOZSB(root->d_inode)));
}

static int
__zpl_show_options(struct seq_file *seq, zfsvfs_t *zfsvfs)
{
	seq_printf(seq, ",%s",
	    zfsvfs->z_flags & ZSB_XATTR ? "xattr" : "noxattr");

#ifdef CONFIG_FS_POSIX_ACL
	switch (zfsvfs->z_acl_type) {
	case ZFS_ACLTYPE_POSIX:
		seq_puts(seq, ",posixacl");
		break;
	default:
		seq_puts(seq, ",noacl");
		break;
	}
#endif /* CONFIG_FS_POSIX_ACL */

	switch (zfsvfs->z_case) {
	case ZFS_CASE_SENSITIVE:
		seq_puts(seq, ",casesensitive");
		break;
	case ZFS_CASE_INSENSITIVE:
		seq_puts(seq, ",caseinsensitive");
		break;
	default:
		seq_puts(seq, ",casemixed");
		break;
	}

	return (0);
}

static int
zpl_show_options(struct seq_file *seq, struct dentry *root)
{
	return (__zpl_show_options(seq, ITOZSB(root->d_inode)));
}

#if 0
static int
zpl_fill_super(struct super_block *sb, void *data, int silent)
{
	zfs_mnt_t *zm = (zfs_mnt_t *)data;
	fstrans_cookie_t cookie;
	int error;

	cookie = spl_fstrans_mark();
	error = -zfs_domount(sb, zm, silent);
	spl_fstrans_unmark(cookie);
	ASSERT3S(error, <=, 0);

	return (error);
}
#endif

static int
zpl_test_super_fc(struct super_block *sb, struct fs_context *fc)
{
	/*
	 * If the os doesn't match the z_os in the super_block, assume it is
	 * not a match. Matching would imply a multimount of a dataset. It is
	 * possible that during a multimount, there is a simultaneous operation
	 * that changes the z_os, e.g., rollback, where the match will be
	 * missed, but in that case the user will get an EBUSY.
	 */
	/*
	 * XXX I'm not actually sure to what extent this is still true, but I
	 *     think probably. the hackaround here is to stash objset_t in
	 *     vfs_os to make up for the loss of void *data.
	 *       -- robn, 2026-02-26
	 */
	/*
	 * XXX changed my mind for now, just gonna shove os in the info and
	 *     see where we end up. we need multisupport of some sort, binds
	 *     and such are real
	 *       -- robn, 2026-02-26
	 */
	/*
	 * XXX back where we were, straight up yes/no for the vfszfs. but
	 *     maybe we don't need the z_os stuff anymore, because we're
	 *     messing with ownership etc
	 *       -- robn, 2026-02-27
	 */
	/* XXX nope, name binding it is. -- robn, 2026-02-27 */
	vfs_bind_t *vb = sb->s_fs_info;
	return (strcmp(vb->vb_name, fc->source) == 0);
}

#if 0
static int
zpl_test_super(struct super_block *s, void *data)
{
	zfsvfs_t *zfsvfs = s->s_fs_info;
	objset_t *os = data;
	/*
	 * If the os doesn't match the z_os in the super_block, assume it is
	 * not a match. Matching would imply a multimount of a dataset. It is
	 * possible that during a multimount, there is a simultaneous operation
	 * that changes the z_os, e.g., rollback, where the match will be
	 * missed, but in that case the user will get an EBUSY.
	 */
	return (zfsvfs != NULL && os == zfsvfs->z_os);
}
#endif

#if 0
static struct super_block *
zpl_mount_impl(struct file_system_type *fs_type, int flags, zfs_mnt_t *zm)
{
	struct super_block *s;
	objset_t *os;
	boolean_t issnap = B_FALSE;
	int err;

	err = dmu_objset_hold(zm->mnt_osname, FTAG, &os);
	if (err)
		return (ERR_PTR(-err));

	/*
	 * The dsl pool lock must be released prior to calling sget().
	 * It is possible sget() may block on the lock in grab_super()
	 * while deactivate_super() holds that same lock and waits for
	 * a txg sync.  If the dsl_pool lock is held over sget()
	 * this can prevent the pool sync and cause a deadlock.
	 */
	dsl_dataset_long_hold(dmu_objset_ds(os), FTAG);
	dsl_pool_rele(dmu_objset_pool(os), FTAG);

	s = sget(fs_type, zpl_test_super, set_anon_super, flags, os);

	/*
	 * Recheck with the lock held to prevent mounting the wrong dataset
	 * since z_os can be stale when the teardown lock is held.
	 *
	 * We can't do this in zpl_test_super in since it's under spinlock and
	 * also s_umount lock is not held there so it would race with
	 * zfs_umount and zfsvfs can be freed.
	 */
	if (!IS_ERR(s) && s->s_fs_info != NULL) {
		zfsvfs_t *zfsvfs = s->s_fs_info;
		if (zpl_enter(zfsvfs, FTAG) == 0) {
			if (os != zfsvfs->z_os)
				err = -SET_ERROR(EBUSY);
			issnap = zfsvfs->z_issnap;
			zpl_exit(zfsvfs, FTAG);
		} else {
			err = -SET_ERROR(EBUSY);
		}
	}
	dsl_dataset_long_rele(dmu_objset_ds(os), FTAG);
	dsl_dataset_rele(dmu_objset_ds(os), FTAG);

	if (IS_ERR(s))
		return (ERR_CAST(s));

	if (err) {
		deactivate_locked_super(s);
		return (ERR_PTR(err));
	}

	if (s->s_root == NULL) {
		err = zpl_fill_super(s, zm, flags & SB_SILENT ? 1 : 0);
		if (err) {
			deactivate_locked_super(s);
			return (ERR_PTR(err));
		}
		s->s_flags |= SB_ACTIVE;
	} else if (!issnap && ((flags ^ s->s_flags) & SB_RDONLY)) {
		/*
		 * Skip ro check for snap since snap is always ro regardless
		 * ro flag is passed by mount or not.
		 */
		deactivate_locked_super(s);
		return (ERR_PTR(-EBUSY));
	}

	return (s);
}
#endif

#if 0
static struct dentry *
zpl_mount(struct file_system_type *fs_type, int flags,
    const char *osname, void *data)
{
	zfs_mnt_t zm = { .mnt_osname = osname, .mnt_data = data };

	struct super_block *sb = zpl_mount_impl(fs_type, flags, &zm);
	if (IS_ERR(sb))
		return (ERR_CAST(sb));

	return (dget(sb->s_root));
}
#endif

static void
zpl_kill_sb(struct super_block *sb)
{
	zfs_preumount(sb);

	/*
	 * XXX pretty sure this is right; may also need the actual
	 *     vfszfs teardown? but maybe that's in the umount callback
	 *     instead. we'll see, lots more to do -- robn, 2026-02-27
	 */
	vfs_bind_t *vb = sb->s_fs_info;
	ASSERT0P(vb->vb_zfsvfs);
	kmem_strfree(vb->vb_name);
	kmem_free(vb, sizeof (vfs_bind_t));
	sb->s_fs_info = NULL;

	kill_anon_super(sb);
}

void
zpl_prune_sb(uint64_t nr_to_scan, void *arg)
{
	struct super_block *sb = (struct super_block *)arg;
	int objects = 0;

	/*
	 * Ensure the superblock is not in the process of being torn down.
	 */
#ifdef HAVE_SB_DYING
	if (down_read_trylock(&sb->s_umount)) {
		if (!(sb->s_flags & SB_DYING) && sb->s_root &&
		    (sb->s_flags & SB_BORN)) {
			(void) zfs_prune(sb, nr_to_scan, &objects);
		}
		up_read(&sb->s_umount);
	}
#else
	if (down_read_trylock(&sb->s_umount)) {
		if (!hlist_unhashed(&sb->s_instances) &&
		    sb->s_root && (sb->s_flags & SB_BORN)) {
			(void) zfs_prune(sb, nr_to_scan, &objects);
		}
		up_read(&sb->s_umount);
	}
#endif
}

#ifdef HAVE_FS_CONTEXT

/* XXX GOOD NEW STUFF */

/*
 * Common superblock flags are handled by vfs_parse_fs_param() and
 * set/cleared on fc->sb_flags and set on fc->sb_flags_mask:
 *
 *	dirsync:	set SB_DIRSYNC
 *	lazytime:	set SB_LAZYTIME
 *	mand:		set SB_MANDLOCK
 *	ro:		set SB_RDONLY
 *	sync:		set SB_SYNCHRONOUS
 *
 *	async:		clear SB_SYNCHRONOUS
 *	nolazytime:	clear SB_LAZYTIME
 *	nomand:		clear SB_MANDLOCK
 *	rw:		clear SB_RDONLY
 *
 * Options handled by vfs_parse_fs_param() if we don't:
 *	source:		set on fc->source
 *
 * Options that are translated by libmount into mount flags. The ones that
 * aren't converted into superblock flags (at least) arrive on the eventual
 * struct mount->mnt_flags, but I'm not sure if we get to see them before that.
 *
 *	ro:		set MS_RDONLY
 *	rw:		clear MS_RDONLY
 *	exec:		clear MS_NOEXEC
 *	noexec:		set MS_NOEXEC
 *	suid:		clear MS_NOSUID
 *	nosuid:		set MS_NOSUID
 *	dev:		clear MS_NODEV
 *	nodev:		set MS_NODEV
 *	atime:		clear MS_NOATIME
 *	noatime:	set MS_NOATIME
 *	relatime:	set MS_RELATIME
 *	norelatime:	clear MS_RELATIME
 *
 * Options that can be passed from libzfs_mount as options rather than mount
 * flags. Arguably a bug, but it doesn't matter - we have to maintain them.
 *
 *	atime
 *	noatime
 *	relatime
 *	strictatime
 *	dev
 *	nodev
 *	exec
 *	noexec
 *	ro
 *	rw
 *	suid
 *	nosuid
 *	mand
 *	nomand
 *
 *	defaults
 *	zfsutil
 *
 *	remount
 *
 *	fscontext
 *	defcontext
 *	rootcontext
 *	context
 *
 * OpenZFS-specific options:
 *
 *	dirxattr:	vfs_xattr=ZFS_XATTR_DIR
 *	saxattr:	vfs_xattr=ZFS_XATTR_SA
 *	xattr:		vfs_xattr=ZFS_XATTR_SA
 *	noxattr:	vfs_xattr=ZFS_XATTR_OFF
 *	mntpoint=	vfs_mntpoint=...
 */

enum {
	Opt_dirxattr, Opt_saxattr, Opt_noxattr, Opt_mntpoint,

	Opt_atime, Opt_noatime, Opt_relatime, Opt_strictatime,
	Opt_dev, Opt_nodev, Opt_exec, Opt_noexec,
	Opt_ro, Opt_rw, Opt_suid, Opt_nosuid,
	Opt_mand, Opt_nomand,

	Opt_ignore,
};

static const struct fs_parameter_spec zpl_param_spec[] = {
	fsparam_flag	("dirxattr",	Opt_dirxattr),
	fsparam_flag	("saxattr",	Opt_saxattr),
	fsparam_flag	("xattr",	Opt_saxattr),
	fsparam_flag	("noxattr",	Opt_noxattr),
	fsparam_string	("mntpoint",	Opt_mntpoint),

	fsparam_flag	("atime",	Opt_atime),
	fsparam_flag	("noatime",	Opt_noatime),
	fsparam_flag	("relatime",	Opt_relatime),
	fsparam_flag	("strictatime",	Opt_strictatime),
	fsparam_flag	("dev",		Opt_dev),
	fsparam_flag	("nodev",	Opt_nodev),
	fsparam_flag	("exec",	Opt_exec),
	fsparam_flag	("noexec",	Opt_noexec),
	fsparam_flag	("ro",		Opt_ro),
	fsparam_flag	("rw",		Opt_rw),
	fsparam_flag	("suid",	Opt_suid),
	fsparam_flag	("nosuid",	Opt_nosuid),
	fsparam_flag	("mand",	Opt_mand),
	fsparam_flag	("nomand",	Opt_nomand),

	fsparam_flag	("defaults",	Opt_ignore),
	fsparam_flag	("zfsutil",	Opt_ignore),

	fsparam_flag	("remount",	Opt_ignore),

	fsparam_flag	("fscontext",	Opt_ignore),
	fsparam_flag	("defcontext",	Opt_ignore),
	fsparam_flag	("rootcontext",	Opt_ignore),
	fsparam_flag	("context",	Opt_ignore),
	{}
};

static int zpl_parse_param(struct fs_context *fc, struct fs_parameter *param)
{
	vfs_t *vfs = fc->fs_private;

	struct fs_parse_result result;
	int opt = fs_parse(fc, zpl_param_spec, param, &result);
	if (opt < 0)
		return (opt);

	switch (opt) {
	case Opt_dirxattr:
		vfs->vfs_xattr = ZFS_XATTR_DIR;
		vfs->vfs_do_xattr = B_TRUE;
		break;
	case Opt_saxattr:
		vfs->vfs_xattr = ZFS_XATTR_SA;
		vfs->vfs_do_xattr = B_TRUE;
		break;
	case Opt_noxattr:
		vfs->vfs_xattr = ZFS_XATTR_OFF;
		vfs->vfs_do_xattr = B_FALSE;
		break;
	case Opt_mntpoint:
		if (vfs->vfs_mntpoint != NULL)
			kmem_strfree(vfs->vfs_mntpoint);
		vfs->vfs_mntpoint = kmem_strdup(param->string);
		break;
	case Opt_atime:
		vfs->vfs_atime = B_TRUE;
		vfs->vfs_do_atime = B_TRUE;
		break;
	case Opt_noatime:
		vfs->vfs_atime = B_FALSE;
		vfs->vfs_do_atime = B_TRUE;
		break;
	case Opt_relatime:
		vfs->vfs_relatime = B_FALSE;
		vfs->vfs_do_relatime = B_TRUE;
		break;
	case Opt_strictatime:
		vfs->vfs_atime = B_TRUE;
		vfs->vfs_do_atime = B_TRUE;
		vfs->vfs_relatime = B_FALSE;
		vfs->vfs_do_relatime = B_TRUE;
		break;
	case Opt_dev:
		vfs->vfs_devices = B_TRUE;
		vfs->vfs_do_devices = B_TRUE;
		break;
	case Opt_nodev:
		vfs->vfs_devices = B_FALSE;
		vfs->vfs_do_devices = B_TRUE;
		break;
	case Opt_exec:
		vfs->vfs_exec = B_TRUE;
		vfs->vfs_do_exec = B_TRUE;
		break;
	case Opt_noexec:
		vfs->vfs_exec = B_FALSE;
		vfs->vfs_do_exec = B_TRUE;
		break;
	case Opt_ro:
		vfs->vfs_readonly = B_TRUE;
		vfs->vfs_do_readonly = B_TRUE;
		break;
	case Opt_rw:
		vfs->vfs_readonly = B_FALSE;
		vfs->vfs_do_readonly = B_TRUE;
		break;
	case Opt_suid:
		vfs->vfs_setuid = B_TRUE;
		vfs->vfs_do_setuid = B_TRUE;
		break;
	case Opt_nosuid:
		vfs->vfs_setuid = B_FALSE;
		vfs->vfs_do_setuid = B_TRUE;
		break;
	case Opt_mand:
		vfs->vfs_nbmand = B_TRUE;
		vfs->vfs_do_nbmand = B_TRUE;
		break;
	case Opt_nomand:
		vfs->vfs_nbmand = B_FALSE;
		vfs->vfs_do_nbmand = B_TRUE;
		break;
	case Opt_ignore:
		cmn_err(CE_NOTE, "ignore option: %s", param->key);
		break;
	default:
		return (-SET_ERROR(EINVAL));
	}

	return (0);
}

/*
 * Since kernel 5.2, the "new" fs_context-based mount API has been preferred
 * over the traditional file_system_type->mount() and
 * super_operations->remount_fs() callbacks, which were deprectate. In 7.0,
 * those callbacks were removed.
 *
 * Currently, the old-style interface are the only ones we need, so this is
 * a simple compatibility shim to adapt the new API to the old-style calls.
 */
#if 0
static int
zpl_parse_monolithic(struct fs_context *fc, void *data)
{
	/*
	 * We do options parsing in zfs_domount(); just stash the options blob
	 * in the fs_context so we can pass it down later.
	 */
	fc->fs_private = data;
	return (0);
}
#endif

static int
zpl_get_tree(struct fs_context *fc)
{
	// XXX zfs_mnt_t zm = { .mnt_osname = fc->source, .mnt_data = fs->fs_private };

	// XXX struct super_block *sb = zpl_mount_impl(fc->fs_type, fc->sb_flags, &zm);

	vfs_t *vfs = fc->fs_private;

	/*
	 * XXX maybe need to reject snapshots or other weird nonsense?
	 *     otoh would it not be sick as to mount a snapshot directly?
	 *       -- robn, 2026-02-26
	 */

	/*
	 * Refuse to mount a filesystem if we are in a namespace and the
	 * dataset is not visible or writable in that namespace.
	 */
	int canwrite;
	int visible = zone_dataset_visible(fc->source, &canwrite);
	if (!INGLOBALZONE(curproc) && (!visible || !canwrite))
		return (-SET_ERROR(EPERM));

	/*
	 * If a non-writable filesystem is being mounted without the
	 * read-only flag, pretend it was set, as done for snapshots.
	 */
	/*
	 * XXX zone check maybe can go in an earlier stage eg param check?
	 *     even context creation?
	 *       -- robn, 2026-02-26
	 */
	if (!canwrite)
		vfs->vfs_readonly = B_TRUE;

	/* Prepare the superblock binding. */
	vfs_bind_t *vb = kmem_zalloc(sizeof (vfs_bind_t), KM_NOSLEEP);
	if (vb == NULL)
		return (-SET_ERROR(ENOMEM));
	vb->vb_name = kmem_strdup(fc->source);

	/* And see if we already have a superblock for this one already. */
	fc->s_fs_info = vb;
	struct super_block *sb =
	    sget_fc(fc, zpl_test_super_fc, set_anon_super_fc);
	if (IS_ERR(sb)) {
		kmem_strfree(vb->vb_name);
		kmem_free(vb, sizeof (vfs_bind_t));
		return (PTR_ERR(sb));
	}

	if (fc->s_fs_info == NULL) {
		/*
		 * If sget_fc() took fc->s_fs_info, then this is the first
		 * mount for the dataset and we have to spin everything up.
		 */
		ASSERT3P(sb->s_fs_info, ==, vb);
		ASSERT0P(vb->vb_zfsvfs);
		ASSERT0P(sb->s_root);

		/*
		 * No existing zfsvfs on this dataset, so take the initial
		 * owning hold and create one.
		 */
		zfsvfs_t *zfsvfs;
		int err = zfsvfs_create(fc->source, vfs->vfs_readonly, &zfsvfs);
		if (err != 0) {
			deactivate_locked_super(sb);
			return (-err);
		}

		/* XXX legacy probably */
		vfs->vfs_data = zfsvfs;
		zfsvfs->z_vfs = vfs;
		zfsvfs->z_sb = sb;

		uint64_t recordsize;
		err = dsl_prop_get_integer(fc->source, "recordsize",
		    &recordsize, NULL);
		if (err != 0) {
			deactivate_locked_super(sb);
			dmu_objset_disown(zfsvfs->z_os, B_TRUE, zfsvfs);
			zfsvfs_free(zfsvfs);
			return (-err);
		}

		/*
		 * XXX this is zpl_fill_super() or maybe zfs_domount();
		 *     I need to go read them both. -- robn, 2026-02-27
		 */
		sb->s_magic = ZFS_SUPER_MAGIC;
		sb->s_maxbytes = MAX_LFS_FILESIZE;
		sb->s_time_gran = 1;
		sb->s_blocksize = recordsize;
		sb->s_blocksize_bits = ilog2(sb->s_blocksize);

		sb->s_op = &zpl_super_operations;
		sb->s_xattr = zpl_xattr_handlers;
		sb->s_export_op = &zpl_export_operations;
		set_default_d_op(sb, &zpl_dentry_operations);

		/* Set features for file system. */
		zfs_set_fuid_feature(zfsvfs);

		err = zfsvfs_setup(zfsvfs, B_TRUE);
		if (err != 0) {
			deactivate_locked_super(sb);
			dmu_objset_disown(zfsvfs->z_os, B_TRUE, zfsvfs);
			zfsvfs_free(zfsvfs);
			return (-err);
		}

		struct inode *root_inode;
		err = zfs_root(zfsvfs, &root_inode);
		if (err != 0) {
			// XXX zfs_umount
			deactivate_locked_super(sb);
			dmu_objset_disown(zfsvfs->z_os, B_TRUE, zfsvfs);
			zfsvfs_free(zfsvfs);
			return (-err);
		}

		sb->s_root = d_make_root(root_inode);
		if (sb->s_root == NULL) {
			// XXX iput?
			// XXX zfs_umount
			deactivate_locked_super(sb);
			dmu_objset_disown(zfsvfs->z_os, B_TRUE, zfsvfs);
			zfsvfs_free(zfsvfs);
			return (-err);
		}

		vb->vb_zfsvfs = zfsvfs;
	} else {
		VERIFY0("second mount");
	}

	ASSERT3P(sb->s_root, !=, NULL);

	fc->root = dget(sb->s_root);

	return (0);
}

static int
zpl_reconfigure(struct fs_context *fc)
{
	cmn_err(CE_NOTE, "zpl_reconfigure: source %s", fc->source);
	return (zpl_remount_fs(fc->root->d_sb, &fc->sb_flags, fc->fs_private));
}

static void
zpl_free_fc(struct fs_context *fc)
{
	vfs_t *vfs = fc->fs_private;
	if (vfs->vfs_mntpoint != NULL)
		kmem_strfree(vfs->vfs_mntpoint);
	kmem_free(vfs, sizeof (vfs_t));
}

const struct fs_context_operations zpl_fs_context_operations = {
	.parse_param		= zpl_parse_param,
	// XXX .parse_monolithic	= zpl_parse_monolithic,
	.get_tree		= zpl_get_tree,
	.reconfigure		= zpl_reconfigure,
	.free			= zpl_free_fc,
};

static int
zpl_init_fs_context(struct fs_context *fc)
{
	fc->fs_private = kmem_zalloc(sizeof (vfs_t), KM_NOSLEEP);
	if (fc->fs_private == NULL)
		return (-SET_ERROR(ENOMEM));

	fc->ops = &zpl_fs_context_operations;

	return (0);
}
#endif

const struct super_operations zpl_super_operations = {
	.alloc_inode		= zpl_inode_alloc,
#ifdef HAVE_SOPS_FREE_INODE
	.free_inode		= zpl_inode_free,
#endif
	.destroy_inode		= zpl_inode_destroy,
	.dirty_inode		= zpl_dirty_inode,
	.write_inode		= NULL,
	.drop_inode		= zpl_drop_inode,
	.evict_inode		= zpl_evict_inode,
	.put_super		= zpl_put_super,
	.sync_fs		= zpl_sync_fs,
	.statfs			= zpl_statfs,
#ifndef HAVE_FS_CONTEXT
	.remount_fs		= zpl_remount_fs,
#endif
	.show_devname		= zpl_show_devname,
	.show_options		= zpl_show_options,
	.show_stats		= NULL,
};

/*
 * ->d_delete() is called when the last reference to a dentry is released. Its
 *  return value indicates if the dentry should be destroyed immediately, or
 *  retained in the dentry cache.
 *
 * By default (zfs_delete_dentry=0) the kernel will always cache unused
 * entries.  Each dentry holds an inode reference, so cached dentries can hold
 * the final inode reference indefinitely, leading to the inode and its related
 * data being pinned (see zpl_drop_inode()).
 *
 * When set to 1, we signal that the dentry should be destroyed immediately and
 * never cached. This reduces memory usage, at the cost of higher overheads to
 * lookup a file, as the inode and its underlying data (dnode/dbuf) need to be
 * reloaded and reinflated.
 *
 * Note that userspace does not have direct control over dentry references and
 * reclaim; rather, this is part of the kernel's caching and reclaim subsystems
 * (eg vm.vfs_cache_pressure).
 */
static int
zpl_dentry_delete(const struct dentry *dentry)
{
	return (zfs_delete_dentry ? 1 : 0);
}

const struct dentry_operations zpl_dentry_operations = {
	.d_delete = zpl_dentry_delete,
};

struct file_system_type zpl_fs_type = {
	.owner			= THIS_MODULE,
	.name			= ZFS_DRIVER,
#if defined(HAVE_IDMAP_MNT_API)
	.fs_flags		= FS_USERNS_MOUNT | FS_ALLOW_IDMAP,
#else
	.fs_flags		= FS_USERNS_MOUNT,
#endif
#ifdef HAVE_FS_CONTEXT
	.init_fs_context	= zpl_init_fs_context,
#else
	.mount			= zpl_mount,
#endif
	.kill_sb		= zpl_kill_sb,
};

ZFS_MODULE_PARAM(zfs, zfs_, delete_inode, INT, ZMOD_RW,
	"Delete inodes as soon as the last reference is released.");

ZFS_MODULE_PARAM(zfs, zfs_, delete_dentry, INT, ZMOD_RW,
	"Delete dentries from dentry cache as soon as the last reference is "
	"released.");
