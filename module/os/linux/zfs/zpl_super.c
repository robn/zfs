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
#include <linux/fs_context.h>
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
	return (__zpl_show_options(seq, root->d_sb->s_fs_info));
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
static int
zpl_parse_param(struct fs_context *fc, struct fs_parameter *param)
{
	if (strcmp(param->key, "source") == 0) {
		if (param->type != fs_value_is_string)
			return (-SET_ERROR(EINVAL));
		if (fc->source != NULL)
			return (-SET_ERROR(EINVAL));
		fc->source = param->string;
		param->string = NULL;
		return (0);
	}
	return (0);
}

static int
zpl_test_super_fc(struct super_block *sb, struct fs_context *fc)
{
	/*
	 * s_fs_info holds a spa_t pointer, so match on it to use the same
	 * superblock for all datasets in the same pool.
	 */
	return (sb->s_fs_info == fc->s_fs_info);
}

static vfs_t *
zpl_fc_to_vfs(struct fs_context *fc)
{
	/*
	 * XXX zfsvfs_parse_options but transferring the sb_flags from
	 *     fc->sb_flags. this is insufficient; we will need to use the
	 *     real param parser so we know which are  actually offered vs
	 *     defaults, so we can set vfs_do_*
	 *
	 *     oh actually, sb_flags_mask tell us which ones were offered,
	 *     so we can tell. still, need to check what's up with negation
	 *
	 *     also what is s_iflags. comment says "or'd with sb->s_iflags
	 *     but what?
	 */

	vfs_t *vfs = kmem_zalloc(sizeof (vfs_t), KM_SLEEP);
	mutex_init(&vfs->vfs_mntpt_lock, NULL, MUTEX_DEFAULT, NULL);

	vfs->vfs_readonly = !!(fc->sb_flags & SB_RDONLY);
	vfs->vfs_setuid = !(fc->sb_flags & SB_NOSUID);
	vfs->vfs_exec = !(fc->sb_flags & SB_NOEXEC);
	vfs->vfs_devices = !(fc->sb_flags & SB_NODEV);
	vfs->vfs_atime = !(fc->sb_flags & SB_NOATIME);

	/*
	 * XXX custom param for xattr then?
	case TOKEN_DIRXATTR:
		vfsp->vfs_xattr = ZFS_XATTR_DIR;
		vfsp->vfs_do_xattr = B_TRUE;
		break;
	case TOKEN_SAXATTR:
		vfsp->vfs_xattr = ZFS_XATTR_SA;
		vfsp->vfs_do_xattr = B_TRUE;
		break;
	case TOKEN_XATTR:
		vfsp->vfs_xattr = ZFS_XATTR_SA;
		vfsp->vfs_do_xattr = B_TRUE;
		break;
	case TOKEN_NOXATTR:
		vfsp->vfs_xattr = ZFS_XATTR_OFF;
		vfsp->vfs_do_xattr = B_TRUE;
		break;
	*/

	/*
	 * XXX custom param for relatime? or is it some combo with SB_LAZYTIME?
	case TOKEN_RELATIME:
		vfsp->vfs_relatime = B_TRUE;
		vfsp->vfs_do_relatime = B_TRUE;
		break;
	case TOKEN_NORELATIME:
		vfsp->vfs_relatime = B_FALSE;
		vfsp->vfs_do_relatime = B_TRUE;
		break;
	*/

	
	/*
	 * XXX "non-blocking mandatory locking", see nbmand in zfsprops(7)
	case TOKEN_NBMAND:
		vfsp->vfs_nbmand = B_TRUE;
		vfsp->vfs_do_nbmand = B_TRUE;
		break;
	case TOKEN_NONBMAND:
		vfsp->vfs_nbmand = B_FALSE;
		vfsp->vfs_do_nbmand = B_TRUE;
		break;
	*/

	/* XXX not even sure about mntpoint=/foo, is it a zfs-specific one? */

	return (vfs);
}

static int
zpl_get_dataset_root(const char *name, struct super_block *sb,
    struct fs_context *fc, struct dentry **root)
{
	zfsvfs_t *zfsvfs = NULL;
	int visible, canwrite, err;

	/* XXX cribbing from zfs_domount() */

	/*
	 * Refuse to mount a filesystem if we are in a namespace and the
	 * dataset is not visible or writable in that namespace.
	 */
	visible = zone_dataset_visible(name, &canwrite);
	if (!INGLOBALZONE(curproc) && (!visible || !canwrite))
		return (SET_ERROR(EPERM));

	vfs_t *vfs = zpl_fc_to_vfs(fc);

	/*
	 * If a non-writable filesystem is being mounted without the
	 * read-only flag, pretend it was set, as done for snapshots.
	 */
	if (!canwrite)
		vfs->vfs_readonly = B_TRUE;

	err = zfsvfs_create(name, vfs->vfs_readonly, &zfsvfs);
	if (err != 0) {
		zfsvfs_vfs_free(vfs);
		goto out;
	}

	vfs->vfs_data = zfsvfs;
	zfsvfs->z_vfs = vfs;
	zfsvfs->z_sb = sb; // XXX ok but maybe not?
	
	/* Set features for file system. */
	zfs_set_fuid_feature(zfsvfs);

	/* XXX snapshot setup here */
	if ((err = zfsvfs_setup(zfsvfs, B_TRUE)) != 0)
		goto out;

	struct inode *i;
	err = zfs_root(zfsvfs, &i);
	if (err != 0) {
		// XXX zfs_umount but the right way
		VERIFY0(err);
		zfsvfs = NULL;
		goto out;
	}

	*root = d_make_root(i);
	if (*root == NULL) {
		// XXX zfs_umount but the right way
		VERIFY0(err);
		zfsvfs = NULL;
		err = SET_ERROR(ENOMEM);
		goto out;
	}

	// XXX snapshots, create ctldir */

	// XXX z_arc_prune for z_prune_sb callback, will need rethink

out:
	if (err != 0) {
		if (zfsvfs != NULL) {
			dmu_objset_disown(zfsvfs->z_os, B_TRUE, zfsvfs);
			zfsvfs_free(zfsvfs);
		}
	}

	return (err);
}

static int
zpl_get_tree(struct fs_context *fc)
{
	spa_t *spa;
	struct super_block *sb;
	objset_t *os;
	int err;

	err = dmu_objset_hold(fc->source, FTAG, &os);
	if (err != 0)
		return (-err);

	/* XXX see comment in zpl_mount_impl */
	dsl_dataset_long_hold(dmu_objset_ds(os), FTAG);
	dsl_pool_rele(dmu_objset_pool(os), FTAG);

	spa = dmu_objset_spa(os);

	fc->s_fs_info = spa; // XXX dsl_pool_t?
	sb = sget_fc(fc, zpl_test_super_fc, set_anon_super_fc);
	if (IS_ERR(sb)) {
		dsl_dataset_long_rele(dmu_objset_ds(os), FTAG);
		dsl_dataset_rele(dmu_objset_ds(os), FTAG);
		return (PTR_ERR(sb));
	}

	/*
	 * XXX I don't think we need the z_os recheck from zfs_mount_impl() here
	 *     because we never compared on it in the test, and we have the
	 *     dataset hold throughout anyway
	 */

	/*
	 * XXX the old code droped the dataset holds around here, but I don't
	 *     really understand why it is safe, as it was before
	 *     zpl_fill_super(). unless zpl_fill_super() picks up another hold?
	 *     as I write this I have not read all the way through the old code
	 *     yet, so it might be obvious, just further down.
	 */

	/*
	 * Find the first separator between pool name and the rest of the
	 * dataset name. We'll need this later for initialising the superblock
	 * and knowing if we're mounting the root.
	 */
	const char *sep = strpbrk(fc->source, "/@#");

	if (sb->s_root == NULL) {
		/*
		 * First time use; set up the superblock for the pool root
		 * filesystem; we'll then set up whatever dataset they asked
		 * for and add that to the context.
		 */

		/*
		 * XXX I don't feel great about all this. of course I have
		 *     to set sb->root, but I'm not sure that its actually
		 *     useable as the root dataset, given the number of
		 *     options we have on every mountpoint.
		 *
		 *     besides, what happens if there's multiple mountpoints
		 *     with different options for the same dataset? what do
		 *     we do right now actually? a bind mount is just an
		 *     extra ref on the root inode, but multiple mounts with
		 *     different options would mean different vfs_t ie
		 *     different zfsvfs_t
		 *
		 *     if we can't resolve the root options, then I guess
		 *     there's no real point setting it up early. do we just
		 *     have a dummy? could be fun to put some sort of control
		 *     doob in there? also this might make a lot more sense
		 *     if I messed with btrfs or bcachefs for like five
		 *     minutes?
		 */

		/*
		 * sget_fc() has transferred the s_fs_info we set above to
		 * the superblock; just checking this.
		 */
		ASSERT3P(sb->s_fs_info, ==, spa);
		ASSERT3P(fc->s_fs_info, ==, NULL);

		sb->s_magic = ZFS_SUPER_MAGIC;
		sb->s_maxbytes = MAX_LFS_FILESIZE;
		sb->s_time_gran = 1;
		sb->s_blocksize = 131072; // XXX for testing; but also variable how?
					  //     or is this ashift-based?
		sb->s_blocksize_bits = ilog2(sb->s_blocksize);

		sb->s_op = &zpl_super_operations;
		sb->s_xattr = zpl_xattr_handlers;
		sb->s_export_op = &zpl_export_operations;
		set_default_d_op(sb, &zpl_dentry_operations);

		if (sep == NULL) {
			err = zpl_get_dataset_root(fc->source, sb, fc, &sb->s_root);
		} else {
			size_t len = (sep - fc->source) + 1;
			char *poolname = kmem_alloc(len, KM_SLEEP);
			strlcpy(poolname, fc->source, len);
			err = zpl_get_dataset_root(poolname, sb, fc, &sb->s_root);
			kmem_free(poolname, len);
		}

		if (err != 0) {
			deactivate_locked_super(sb);
			return (-err);
		}
	}

	ASSERT3P(sb->s_root, !=, NULL);

	if (sep == NULL) {
		fc->root = dget(sb->s_root);
	} else {
		struct dentry *root;
		err = zpl_get_dataset_root(fc->source, sb, fc, &root);
		if (err != 0) {
			if (fc->s_fs_info == NULL) {
				/*
				 * If we don't have s_fs_info, then it was
				 * moved by sget_fc() above to create the
				 * superblock. Since the actual mount they
				 * wanted failed, there are now no users of
				 * the superblock, so release it.
				 */
				dput(sb->s_root);
				deactivate_locked_super(sb);
			}
			return (-err);
		}
		fc->root = dget(root);
	}

	if (fc->s_fs_info == NULL)
		fc->s_fs_info = sb->s_fs_info;

	return (0);
}

static int
zpl_reconfigure(struct fs_context *fc)
{
	return (zpl_remount_fs(fc->root->d_sb, &fc->sb_flags, NULL));
}

const struct fs_context_operations zpl_fs_context_operations = {
	.parse_param		= zpl_parse_param,
	.get_tree		= zpl_get_tree,
	.reconfigure		= zpl_reconfigure,
};

static int
zpl_init_fs_context(struct fs_context *fc)
{
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
