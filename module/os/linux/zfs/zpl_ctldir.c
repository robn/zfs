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
 * Copyright (C) 2011 Lawrence Livermore National Security, LLC.
 * Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 * LLNL-CODE-403049.
 * Rewritten for Linux by:
 *   Rohan Puri <rohan.puri15@gmail.com>
 *   Brian Behlendorf <behlendorf1@llnl.gov>
 */

#include <sys/zfs_znode.h>
#include <sys/zfs_vfsops.h>
#include <sys/zfs_vnops.h>
#include <sys/zfs_ctldir.h>
#include <sys/zpl.h>
#include <sys/dmu.h>
#include <sys/dsl_dataset.h>
#include <sys/zap.h>
#include <linux/version.h>

/*
 * Common open routine.  Disallow any write access.
 */
static int
zpl_common_open(struct inode *ip, struct file *filp)
{
	if (blk_mode_is_open_write(filp->f_mode))
		return (-EACCES);

	return (generic_file_open(ip, filp));
}

/*
 * Get root directory contents.
 */
static int
zpl_root_iterate(struct file *filp, struct dir_context *ctx)
{
	zfsvfs_t *zfsvfs = ITOZSB(file_inode(filp));
	int error = 0;

	if (zfsvfs->z_show_ctldir == ZFS_SNAPDIR_DISABLED) {
		return (SET_ERROR(ENOENT));
	}

	if ((error = zpl_enter(zfsvfs, FTAG)) != 0)
		return (error);

	if (!dir_emit_dots(filp, ctx))
		goto out;

	if (ctx->pos == 2) {
		if (!dir_emit(ctx, ZFS_SNAPDIR_NAME,
		    strlen(ZFS_SNAPDIR_NAME), ZFSCTL_INO_SNAPDIR, DT_DIR))
			goto out;

		ctx->pos++;
	}

	if (ctx->pos == 3) {
		if (!dir_emit(ctx, ZFS_SHAREDIR_NAME,
		    strlen(ZFS_SHAREDIR_NAME), ZFSCTL_INO_SHARES, DT_DIR))
			goto out;

		ctx->pos++;
	}
out:
	zpl_exit(zfsvfs, FTAG);

	return (error);
}

/*
 * Get root directory attributes.
 */
static int
#ifdef HAVE_IDMAP_IOPS_GETATTR
zpl_root_getattr_impl(struct mnt_idmap *user_ns,
    const struct path *path, struct kstat *stat, u32 request_mask,
    unsigned int query_flags)
#elif defined(HAVE_USERNS_IOPS_GETATTR)
zpl_root_getattr_impl(struct user_namespace *user_ns,
    const struct path *path, struct kstat *stat, u32 request_mask,
    unsigned int query_flags)
#else
zpl_root_getattr_impl(const struct path *path, struct kstat *stat,
    u32 request_mask, unsigned int query_flags)
#endif
{
	(void) request_mask, (void) query_flags;
	struct inode *ip = path->dentry->d_inode;

#if (defined(HAVE_USERNS_IOPS_GETATTR) || defined(HAVE_IDMAP_IOPS_GETATTR))
#ifdef HAVE_GENERIC_FILLATTR_USERNS
	generic_fillattr(user_ns, ip, stat);
#elif defined(HAVE_GENERIC_FILLATTR_IDMAP)
	generic_fillattr(user_ns, ip, stat);
#elif defined(HAVE_GENERIC_FILLATTR_IDMAP_REQMASK)
	generic_fillattr(user_ns, request_mask, ip, stat);
#else
	(void) user_ns;
#endif
#else
	generic_fillattr(ip, stat);
#endif
	stat->atime = current_time(ip);

	return (0);
}
ZPL_GETATTR_WRAPPER(zpl_root_getattr);

static struct dentry *
zpl_root_lookup(struct inode *dip, struct dentry *dentry, unsigned int flags)
{
	cred_t *cr = CRED();
	struct inode *ip;
	int error;

	crhold(cr);
	error = -zfsctl_root_lookup(dip, dname(dentry), &ip, 0, cr, NULL, NULL);
	ASSERT3S(error, <=, 0);
	crfree(cr);

	if (error) {
		if (error == -ENOENT)
			return (d_splice_alias(NULL, dentry));
		else
			return (ERR_PTR(error));
	}

	return (d_splice_alias(ip, dentry));
}

/*
 * The '.zfs' control directory file and inode operations.
 */
const struct file_operations zpl_fops_root = {
	.open		= zpl_common_open,
	.llseek		= generic_file_llseek,
	.read		= generic_read_dir,
	.iterate_shared	= zpl_root_iterate,
};

const struct inode_operations zpl_ops_root = {
	.lookup		= zpl_root_lookup,
	.getattr	= zpl_root_getattr,
};

/*
 * After zpl_snapdir_lookup() has created the snapshot "directory", the
 * dentry tree of the "virtual" .zfs/ control dir looks like.
 *
 * [.zfs] -> [snapshot] -> [snapname]
 *
 * [snapname] has two dentry operations registered:
 *  - MANAGE_TRANSIT (d_manage)
 *  - NEED_AUTOMOUNT (d_automount)
 *
 * On request to look "inside" a dentry, __traverse_mounts() in the kernel is
 * called. Ultimately, it calls d_automount(), which is expected to return a
 * struct vfsmount * (or fail). On return, this is passed to
 * finish_automount(), which grafts the mount into the dentry tree, resulting
 * in:
 *
 * [.zfs] -> [snapshot] -> [snapname] -> {vfsmount} -> [/] -> ...
 *
 * This approach works well enough, but has a couple of problems:
 *
 * - finish_automount() overwrites any mount flags set by d_automount(),
 *   which makes it hard to apply "reduced privilege" flags (eg MNT_NOSUID).
 *
 * - we never get to see the completed mount before the return to userspace.
 *
 * That second part is a problem because (a) the graft may fail for some
 * reason (typically a lost race) and (b) because at expiry, we don't actually
 * know the link between between the snapdir, the mount and the root dentry.
 * Userspace may have moved the mount away (mount --move), manually unmounted,
 * or even moved a different and unrelated mount to this position (maybe not
 * even ZFS!).
 *
 * This is where d_manage() comes in. This is called earlier in
 * __traverse_mounts(), to allow autofs to stop later callers passing into
 * d_automount() while an earlier caller is already there, setting up the
 * mount. On leaving d_manage(), the dentry is reexamined and if there's
 * a mount there, d_automount() is never even called.
 *
 * This allows us to neatly solve both problems.
 *
 * When we create the snapdir dentry, we create a zpl_snapentry_t and stash it
 * on the dentry in d_fsdata. On the first call to d_manage(), we record the
 * current task in sd_mount_task, Then, we call back into __traverse_mounts()
 * (via follow_down()) to request a second traversal into the snapdir dentry.
 *
 * We arrive back in d_manage(). We check sd_mount_task and find the same
 * current task, so we know this is the same thread, and we simply return,
 * which proceeds into d_automount(). That returns the mount and
 * finish_automount() performs the graft. Once done, it returns to
 * __traverse_mounts(), follow_down() and back into the original call to
 * d_manage(), where we can inspect the completed dentry and mount, set any
 * flags on it, and record any details we need, all before we return to the
 * original caller.
 *
 * While this is happening, any other callers to d_manage() will see that
 * sd_mount_task is set to some other thread, and sleep on sd_cv.
 *
 * Regardless of how they get there, all "true" callers to d_manage() will
 * return, do the mount check in __traverse_mounts(), see that the mount
 * exists, and move into it.
 */

static int
zpl_snapdir_manage(const struct path *path, bool rcu_walk)
{
	struct dentry *dentry = path->dentry;
	zpl_snapentry_t *se = dentry->d_fsdata;

	if (!atomic_load_32(&se->se_mount_wanted)) {
		/* Mount not requested or in-flight, pass. */
		cmn_err(CE_NOTE, "zpl_snapdir_manage: dname=%s: "
		    "no mount wanted, pass", dname(dentry));
		return (0);
	}

	cmn_err(CE_NOTE, "zpl_snapdir_manage: rcu_walk=%d dname=%s inode=%px",
	    rcu_walk, dname(dentry), dentry->d_inode);

	if (rcu_walk) {
		/*
		 * In "RCU-walk" mode we must not block. We can only signal
		 * that the walk should definitely proceed (ie enter the mount,
		 * or trigger automount), definitely not proceed (ie this is a
		 * normal directory), or that we can't determine without
		 * blocking and the walk should fall back to "REF-walk".
		 *
		 * Because of our reentrant d_manage->d_automount->d_manage
		 * construction, we can't do anything here without taking
		 * se_mtx first to determine if we are the mount task, which
		 * means potentially blocking. Any check we do without that
		 * check first risks being inaccurate, for example, if we
		 * checked if there is a mountpoint and allow it proceed, we
		 * might be letting a different thread through to the
		 * mount before the mount task has returned to d_manage() and
		 * called zpl_snapentry_finish_mount(), so the mount is
		 * technically incomplete.
		 *
		 * So, we simply always request fallback to the slow path.
		 */
		return (-ECHILD);
	}

	mutex_enter(&se->se_mtx);
	if (se->se_mount_task == current) {
		/*
		 * Caller is our own task, reentering through follow_down().
		 * Allow it to proceed.
		 */
		mutex_exit(&se->se_mtx);
		cmn_err(CE_NOTE, "zpl_snapdir_manage: dname=%s: "
		    "recursive entry; allowing to proceed to automount",
		    dname(dentry));
		return (0);
	}

	while (se->se_mount_task != NULL) {
		/* Another task is doing the mount, sleep and wait for it. */
		cmn_err(CE_NOTE, "zpl_snapdir_manage: dname=%s: "
		    "automount in progress; waiting", dname(dentry));
		cv_wait(&se->se_cv, &se->se_mtx);
	}

	/*
	 * If we reach here, either we are the first task to arrive and so
	 * should begin the mount, or we were sleeping while the mount was
	 * happening.
	 *
	 * Regardless, we become the "primary" task for the checks below, so
	 * there is only ever one task in the next section with the locks down
	 * at any moment. If the mount needs to happen, we'll kick it off; if
	 * not, we just skip it.
	 */
	se->se_mount_task = current;
	
	/*
	 * First time through, track the ctldir mount. It's not needed until
	 * much later, during unmount, but this is the first time we've had
	 * the chance to record it with se_mtx held.
	 */
	if (se->se_pmnt == NULL)
		se->se_pmnt = path->mnt;
	mutex_exit(&se->se_mtx);

	int err = 0;
	if (path_is_mountpoint(path)) {
		/*
		 * Something is mounted here already; almost certainly we
		 * were sleeping above. There's nothing we can do here, just
		 * leave. Caller (__traverse_mounts()) will see the mount and
		 * continue into it.
		 */
		/*
		 * XXX we could check that its ours and if not, do something?
		 *     only thing I can think of is to cancel snapentry expiry
		 *     etc; but I think its more effort than its worth - this
		 *     is such a narrow window, and userspace did something
		 *     very strange in this time.
		 */
		cmn_err(CE_NOTE, "zpl_snapdir_manage: dname=%s "
		    "mountpoint active: mnt=%px flags=%08x",
		    dname(dentry), path->mnt, path->mnt->mnt_flags);
		goto out;
	}

	/*
	 * Nothing mounted here, and any other callers are waiting on sd_cv,
	 * so we can trigger the automount.
	 *
	 * Note: since locks are down, userspace may have mounted something at
	 * this position after the path_is_mountpoint() call.  This is not a
	 * problem in practice; right now we're only deciding if this thread
	 * may proceed in __traverse_mounts(); if a mount has appeared, it will
	 * be used as-is; d_automount() won't be called.
	 *
	 */
	cmn_err(CE_NOTE, "zpl_snapdir_manage: dname=%s: "
	    "triggering automount", dname(dentry));

	/*
	 * struct path is a dentry+vfsmount pointer pair. follow_down() will
	 * modify the calling path with its results. So we take a copy of the
	 * path for follow_down(), and then we can look inside and compare
	 * with the original to understand what happened.
	 */
	struct path am_path = *path;
	path_get(&am_path);
	err = follow_down(&am_path, LOOKUP_AUTOMOUNT);
	if (err != 0) {
		/*
		 * follow_down() failure probably means that d_automount()
		 * failed, but could be anything. This just returns it to
		 * the caller.
		 */
		path_put(&am_path);
		cmn_err(CE_NOTE, "zpl_snapdir_manage: dname=%s err=%d: "
		    "follow_down for automount failed", dname(dentry), -err);
		goto out;
	}

	cmn_err(CE_NOTE, "zpl_snapdir_manage: "
	    "orig path [mnt=%px dentry=%px dname=%s] "
	    "orig root [dentry=%px dname=%s] "
	    "new path [mnt=%px dentry=%px dname=%s] "
	    "new root [dentry=%px dname=%s]",
	    path->mnt, path->dentry, dname(path->dentry),
	    path->mnt->mnt_root, dname(path->mnt->mnt_root),
	    am_path.mnt, am_path.dentry, dname(am_path.dentry),
	    am_path.mnt->mnt_root, dname(am_path.mnt->mnt_root));

	/*
	 * Now inspect the completed mount to see what happened. We can't
	 * assume that am_path definitely has the snapshot mount, as something
	 * else may have beaten us to mounting something at that position (see
	 * note above). We can't even assume that there is a mount there at
	 * all, or that its a ZFS mount, so we must carefully check all of
	 * this first before digging around.
	 *
	 * XXX not sure about this comment exactly, depends what we end up
	 *     needing to do. at least, we need to recognise the automount
	 *     on the return into d_manage() to reset the flags, but later
	 *     we will need to recognise the same mount as an overmount for
	 *     teardown (or not). I'm not sure if the latter is the same
	 *     mechanism yet, and won't until I get this new manage_transit
	 *     thing wired into the snapentry code.
	 *       -- robn, 2026-04-24
	 */

	/*
	 * This path is for the wanted snapshot mount if:
	 * - the path dentry is the mount root dentry
	 * - the mount superblock is ZFS
	 * - ...
	 *
	 * XXX this might be akin to the is_our_mount check from
	 *     _zfs_snapentry_detach() (copied here just in case that gets
	 *     refactored away/out):
	 *
	 *	bool is_our_mount = false;
	 *	if (path.mnt->mnt_sb->s_type == &zpl_fs_type) {
	 *		zfsvfs_t *zfsvfs = path.mnt->mnt_sb->s_fs_info;
	 *		spa_t *spa = zfsvfs->z_os->os_spa;
	 *		uint64_t objsetid = dmu_objset_id(zfsvfs->z_os);
	 *		if (se->se_spa == spa && se->se_objsetid == objsetid) {
	 *			is_our_mount = true;
	 *		}
	 *	}
	 *
	 *     or maybe its a simple name check? or maybe its not anything?
	 *     not sure. this is sorta a further case for snapdir_t and
	 *     snapentry_t being same thing?
	 *       -- robn, 2026-04-24
	 */
	if (am_path.dentry != am_path.mnt->mnt_root ||
	    am_path.mnt->mnt_sb->s_type != &zpl_fs_type) {
		/*
		 * Wherever am_path ended up, its not at the root of the
		 * snapshot we're expecting to be mounted here. Nothing else
		 * for us to do in this case.
		 */
		path_put(&am_path);
		cmn_err(CE_NOTE, "zpl_snapdir_manage: dname=%s: "
		    "mount not ours!", dname(dentry));
		goto out;
	}

	/* Give zpl_snapentry one last look at it. */
	zpl_snapentry_finish_mount(se, am_path.mnt);

	path_put(&am_path);

out:
	mutex_enter(&se->se_mtx);
	if (err == 0) {
		atomic_store_32(&se->se_mount_wanted, 0);
		cmn_err(CE_NOTE, "zpl_snapdir_manage: dname=%s: "
		    "clearing mount wanted", dname(dentry));
	}
	se->se_mount_task = NULL;
	cv_broadcast(&se->se_cv);
	mutex_exit(&se->se_mtx);

	return (err);
}

static struct vfsmount *
zpl_snapdir_automount(struct path *path)
{
	struct dentry *dentry = path->dentry;
	zpl_snapentry_t *se = dentry->d_fsdata;
	ASSERT3P(path->mnt, ==, se->se_pmnt);
	ASSERT3P(se->se_mount_task, ==, current);

	struct vfsmount *mnt = NULL;

	cmn_err(CE_NOTE, "zpl_snapdir_automount: dname=%s inode=%px",
	    dname(dentry), dentry->d_inode);

	int err = zpl_snapentry_mount(se, &mnt);
	if (err)
		return (ERR_PTR(-err));

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 16, 0)
	/*
	 * XXX before:
	 *   006ff7498fe89 saner calling conventions for ->d_automount()
	 *
	 * d_automount() is expected to return a mount with _two_ refs; the
	 * extra prevents an immediate expiry before the mount is grafted.
	 * finish_automount() will BUG() if its <2, and will release the extra
	 * when its done, so take an extra here.
	 *
	 * haven't looked to see if there's a way we can detect this
	 * specifically. if not, check RHEL8/9 version numbering for backport
	 *   -- robn, 2026-04-14
	 */
	mntget(mntp);
#endif

	return (mnt);
}

/*
 * Negative dentries must always be revalidated so newly created snapshots
 * can be detected and automounted.  Normal dentries should be kept because
 * as of the 3.18 kernel revaliding the mountpoint dentry will result in
 * the snapshot being immediately unmounted.
 */
#ifdef HAVE_D_REVALIDATE_4ARGS
static int
zpl_snapdir_revalidate(struct inode *dir, const struct qstr *name,
    struct dentry *dentry, unsigned int flags)
#else
static int
zpl_snapdir_revalidate(struct dentry *dentry, unsigned int flags)
#endif
{
	zpl_snapentry_t *se = dentry->d_fsdata;

	if (dentry->d_inode) {
		if (flags & (LOOKUP_PARENT | LOOKUP_DIRECTORY | LOOKUP_OPEN |
		    LOOKUP_CREATE | LOOKUP_AUTOMOUNT)) {
			atomic_store_32(&se->se_mount_wanted, 1);
			cmn_err(CE_NOTE, "zpl_snapdir_revalidate: dname=%s: "
			    "setting mount wanted", dname(dentry));
		}
		return (1);
	}

	return (0);
}

static void
zpl_snapdir_release(struct dentry *dentry)
{
	spin_lock(&dentry->d_lock);
	zpl_snapentry_t *se = dentry->d_fsdata;
	dentry->d_fsdata = NULL;
	spin_unlock(&dentry->d_lock);

	/*
	 * Release can be called more than once if part of the release was
	 * deferred, so we might have already cleaned up. Do nothing if so.
	 */
	if (se == NULL) {
		cmn_err(CE_NOTE, "zpl_snapdir_release: dentry=%px: already gone", dentry);
		return;
	}

	cmn_err(CE_NOTE, "zpl_snapdir_release: dentry=%px se=%px: destroying", dentry, se);

	ASSERT3P(dentry, ==, se->se_dentry);
	mutex_destroy(&se->se_mtx);
	cv_destroy(&se->se_cv);
	kmem_free(se, sizeof (zpl_snapentry_t));
}

static const struct dentry_operations zpl_dops_snapdirs = {
/*
 * Auto mounting of snapshots is only supported for 2.6.37 and
 * newer kernels.  Prior to this kernel the ops->follow_link()
 * callback was used as a hack to trigger the mount.  The
 * resulting vfsmount was then explicitly grafted in to the
 * name space.  While it might be possible to add compatibility
 * code to accomplish this it would require considerable care.
 */
	.d_manage	= zpl_snapdir_manage,
	.d_automount	= zpl_snapdir_automount,
	.d_revalidate	= zpl_snapdir_revalidate,
	.d_release	= zpl_snapdir_release,
};

/*
 * For the .zfs control directory to work properly we must be able to override
 * the default operations table and register custom .d_automount and
 * .d_revalidate callbacks.
 */
static void
set_snapdir_dentry_ops(struct dentry *dentry, unsigned int extraflags) {
	static const unsigned int op_flags =
	    DCACHE_OP_HASH | DCACHE_OP_COMPARE |
	    DCACHE_OP_REVALIDATE | DCACHE_OP_DELETE |
	    DCACHE_OP_PRUNE | DCACHE_OP_WEAK_REVALIDATE | DCACHE_OP_REAL;

#ifdef HAVE_D_SET_D_OP
	/*
	 * d_set_d_op() will set the DCACHE_OP_ flags according to what it
	 * finds in the passed dentry_operations, so we don't have to.
	 *
	 * We clear the flags and the old op table before calling d_set_d_op()
	 * because issues a warning when the dentry operations table is already
	 * set.
	 */
	dentry->d_op = NULL;
	dentry->d_flags &= ~op_flags;
	d_set_d_op(dentry, &zpl_dops_snapdirs);
	dentry->d_flags |= extraflags;
#else
	/*
	 * Since 6.17 there's no exported way to modify dentry ops, so we have
	 * to reach in and do it ourselves. This should be safe for our very
	 * narrow use case, which is to create or splice in an entry to give
	 * access to a snapshot.
	 *
	 * We need to set the op flags directly. We hardcode
	 * DCACHE_OP_REVALIDATE because that's the only operation we have; if
	 * we ever extend zpl_dops_snapdirs we will need to update the op flags
	 * to match.
	 */
	spin_lock(&dentry->d_lock);
	dentry->d_op = &zpl_dops_snapdirs;
	dentry->d_flags &= ~op_flags;
	dentry->d_flags |= DCACHE_OP_REVALIDATE | extraflags;
	spin_unlock(&dentry->d_lock);
#endif
}

static struct dentry *
zpl_snapdir_lookup(struct inode *dip, struct dentry *dentry,
    unsigned int flags)
{
	fstrans_cookie_t cookie;
	cred_t *cr = CRED();
	struct inode *ip = NULL;
	int error;

	crhold(cr);
	cookie = spl_fstrans_mark();
	error = -zfsctl_snapdir_lookup(dip, dname(dentry), &ip,
	    0, cr, NULL, NULL);
	ASSERT3S(error, <=, 0);
	spl_fstrans_unmark(cookie);
	crfree(cr);

	if (error && error != -ENOENT)
		return (ERR_PTR(error));

	ASSERT(error == 0 || ip == NULL);

	zpl_snapentry_t *se = kmem_zalloc(sizeof (zpl_snapentry_t), KM_SLEEP);
	mutex_init(&se->se_mtx, NULL, MUTEX_DEFAULT, NULL);
	cv_init(&se->se_cv, NULL, CV_DEFAULT, NULL);
	se->se_dentry = dentry;

	if (flags & (LOOKUP_PARENT | LOOKUP_DIRECTORY | LOOKUP_OPEN |
	    LOOKUP_CREATE | LOOKUP_AUTOMOUNT)) {
		se->se_mount_wanted = 1;
		cmn_err(CE_NOTE, "zpl_snapdir_lookup: dname=%s: "
		    "setting mount wanted", dname(dentry));
	}

	spin_lock(&dentry->d_lock);
	dentry->d_fsdata = se;
	spin_unlock(&dentry->d_lock);

	set_snapdir_dentry_ops(dentry,
	    DCACHE_MANAGE_TRANSIT | DCACHE_NEED_AUTOMOUNT);
	return (d_splice_alias(ip, dentry));
}

static int
zpl_snapdir_iterate(struct file *filp, struct dir_context *ctx)
{
	zfsvfs_t *zfsvfs = ITOZSB(file_inode(filp));
	fstrans_cookie_t cookie;
	char snapname[MAXNAMELEN];
	boolean_t case_conflict;
	uint64_t id, pos;
	int error = 0;

	if ((error = zpl_enter(zfsvfs, FTAG)) != 0)
		return (error);
	cookie = spl_fstrans_mark();

	if (!dir_emit_dots(filp, ctx))
		goto out;

	/* Start the position at 0 if it already emitted . and .. */
	pos = (ctx->pos == 2 ? 0 : ctx->pos);
	while (error == 0) {
		dsl_pool_config_enter(dmu_objset_pool(zfsvfs->z_os), FTAG);
		error = -dmu_snapshot_list_next(zfsvfs->z_os, MAXNAMELEN,
		    snapname, &id, &pos, &case_conflict);
		dsl_pool_config_exit(dmu_objset_pool(zfsvfs->z_os), FTAG);
		if (error)
			goto out;

		if (!dir_emit(ctx, snapname, strlen(snapname),
		    ZFSCTL_INO_SHARES - id, DT_DIR))
			goto out;

		ctx->pos = pos;
	}
out:
	spl_fstrans_unmark(cookie);
	zpl_exit(zfsvfs, FTAG);

	if (error == -ENOENT)
		return (0);

	return (error);
}

static int
#ifdef HAVE_IOPS_RENAME_USERNS
zpl_snapdir_rename2(struct user_namespace *user_ns, struct inode *sdip,
    struct dentry *sdentry, struct inode *tdip, struct dentry *tdentry,
    unsigned int flags)
#elif defined(HAVE_IOPS_RENAME_IDMAP)
zpl_snapdir_rename2(struct mnt_idmap *user_ns, struct inode *sdip,
    struct dentry *sdentry, struct inode *tdip, struct dentry *tdentry,
    unsigned int flags)
#else
zpl_snapdir_rename2(struct inode *sdip, struct dentry *sdentry,
    struct inode *tdip, struct dentry *tdentry, unsigned int flags)
#endif
{
	cred_t *cr = CRED();
	int error;

	/* We probably don't want to support renameat2(2) in ctldir */
	if (flags)
		return (-EINVAL);

	crhold(cr);
	error = -zfsctl_snapdir_rename(sdip, dname(sdentry),
	    tdip, dname(tdentry), cr, 0);
	ASSERT3S(error, <=, 0);
	crfree(cr);

	return (error);
}

#if (!defined(HAVE_RENAME_WANTS_FLAGS) && \
	!defined(HAVE_IOPS_RENAME_USERNS) && \
	!defined(HAVE_IOPS_RENAME_IDMAP))
static int
zpl_snapdir_rename(struct inode *sdip, struct dentry *sdentry,
    struct inode *tdip, struct dentry *tdentry)
{
	return (zpl_snapdir_rename2(sdip, sdentry, tdip, tdentry, 0));
}
#endif

static int
zpl_snapdir_rmdir(struct inode *dip, struct dentry *dentry)
{
	cred_t *cr = CRED();
	int error;

	crhold(cr);
	error = -zfsctl_snapdir_remove(dip, dname(dentry), cr, 0);
	ASSERT3S(error, <=, 0);
	crfree(cr);

	return (error);
}

#if defined(HAVE_IOPS_MKDIR_USERNS)
static int
zpl_snapdir_mkdir(struct user_namespace *user_ns, struct inode *dip,
    struct dentry *dentry, umode_t mode)
#elif defined(HAVE_IOPS_MKDIR_IDMAP)
static int
zpl_snapdir_mkdir(struct mnt_idmap *user_ns, struct inode *dip,
    struct dentry *dentry, umode_t mode)
#elif defined(HAVE_IOPS_MKDIR_DENTRY)
static struct dentry *
zpl_snapdir_mkdir(struct mnt_idmap *user_ns, struct inode *dip,
    struct dentry *dentry, umode_t mode)
#else
static int
zpl_snapdir_mkdir(struct inode *dip, struct dentry *dentry, umode_t mode)
#endif
{
	cred_t *cr = CRED();
	vattr_t *vap;
	struct inode *ip;
	int error;

	crhold(cr);
	vap = kmem_zalloc(sizeof (vattr_t), KM_SLEEP);
#if (defined(HAVE_IOPS_MKDIR_USERNS) || defined(HAVE_IOPS_MKDIR_IDMAP))
	zpl_vap_init(vap, dip, mode | S_IFDIR, cr, user_ns);
#else
	zpl_vap_init(vap, dip, mode | S_IFDIR, cr, zfs_init_idmap);
#endif

	error = -zfsctl_snapdir_mkdir(dip, dname(dentry), vap, &ip, cr, 0);
	if (error == 0) {
		set_snapdir_dentry_ops(dentry, 0);
		d_instantiate(dentry, ip);
	}

	kmem_free(vap, sizeof (vattr_t));
	ASSERT3S(error, <=, 0);
	crfree(cr);

#if defined(HAVE_IOPS_MKDIR_DENTRY)
	return (ERR_PTR(error));
#else
	return (error);
#endif
}

/*
 * Get snapshot directory attributes.
 */
static int
#ifdef HAVE_IDMAP_IOPS_GETATTR
zpl_snapdir_getattr_impl(struct mnt_idmap *user_ns,
    const struct path *path, struct kstat *stat, u32 request_mask,
    unsigned int query_flags)
#elif defined(HAVE_USERNS_IOPS_GETATTR)
zpl_snapdir_getattr_impl(struct user_namespace *user_ns,
    const struct path *path, struct kstat *stat, u32 request_mask,
    unsigned int query_flags)
#else
zpl_snapdir_getattr_impl(const struct path *path, struct kstat *stat,
    u32 request_mask, unsigned int query_flags)
#endif
{
	(void) request_mask, (void) query_flags;
	struct inode *ip = path->dentry->d_inode;
	zfsvfs_t *zfsvfs = ITOZSB(ip);
	int error;

	if ((error = zpl_enter(zfsvfs, FTAG)) != 0)
		return (error);
#if (defined(HAVE_USERNS_IOPS_GETATTR) || defined(HAVE_IDMAP_IOPS_GETATTR))
#ifdef HAVE_GENERIC_FILLATTR_USERNS
	generic_fillattr(user_ns, ip, stat);
#elif defined(HAVE_GENERIC_FILLATTR_IDMAP)
	generic_fillattr(user_ns, ip, stat);
#elif defined(HAVE_GENERIC_FILLATTR_IDMAP_REQMASK)
	generic_fillattr(user_ns, request_mask, ip, stat);
#else
	(void) user_ns;
#endif
#else
	generic_fillattr(ip, stat);
#endif

	stat->nlink = stat->size = 2;

	dsl_dataset_t *ds = dmu_objset_ds(zfsvfs->z_os);
	if (dsl_dataset_phys(ds)->ds_snapnames_zapobj != 0) {
		uint64_t snap_count;
		int err = zap_count(
		    dmu_objset_pool(ds->ds_objset)->dp_meta_objset,
		    dsl_dataset_phys(ds)->ds_snapnames_zapobj, &snap_count);
		if (err != 0) {
			zpl_exit(zfsvfs, FTAG);
			return (-err);
		}
		stat->nlink += snap_count;
	}

	stat->ctime = stat->mtime = dmu_objset_snap_cmtime(zfsvfs->z_os);
	stat->atime = current_time(ip);
	zpl_exit(zfsvfs, FTAG);

	return (0);
}
ZPL_GETATTR_WRAPPER(zpl_snapdir_getattr);

/*
 * The '.zfs/snapshot' directory file operations.  These mainly control
 * generating the list of available snapshots when doing an 'ls' in the
 * directory.  See zpl_snapdir_readdir().
 */
const struct file_operations zpl_fops_snapdir = {
	.open		= zpl_common_open,
	.llseek		= generic_file_llseek,
	.read		= generic_read_dir,
	.iterate_shared	= zpl_snapdir_iterate,

};

/*
 * The '.zfs/snapshot' directory inode operations.  These mainly control
 * creating an inode for a snapshot directory and initializing the needed
 * infrastructure to automount the snapshot.  See zpl_snapdir_lookup().
 */
const struct inode_operations zpl_ops_snapdir = {
	.lookup		= zpl_snapdir_lookup,
	.getattr	= zpl_snapdir_getattr,
#if (defined(HAVE_RENAME_WANTS_FLAGS) || \
	defined(HAVE_IOPS_RENAME_USERNS) || \
	defined(HAVE_IOPS_RENAME_IDMAP))
	.rename		= zpl_snapdir_rename2,
#else
	.rename		= zpl_snapdir_rename,
#endif
	.rmdir		= zpl_snapdir_rmdir,
	.mkdir		= zpl_snapdir_mkdir,
};

static struct dentry *
zpl_shares_lookup(struct inode *dip, struct dentry *dentry,
    unsigned int flags)
{
	fstrans_cookie_t cookie;
	cred_t *cr = CRED();
	struct inode *ip = NULL;
	int error;

	crhold(cr);
	cookie = spl_fstrans_mark();
	error = -zfsctl_shares_lookup(dip, dname(dentry), &ip,
	    0, cr, NULL, NULL);
	ASSERT3S(error, <=, 0);
	spl_fstrans_unmark(cookie);
	crfree(cr);

	if (error) {
		if (error == -ENOENT)
			return (d_splice_alias(NULL, dentry));
		else
			return (ERR_PTR(error));
	}

	return (d_splice_alias(ip, dentry));
}

static int
zpl_shares_iterate(struct file *filp, struct dir_context *ctx)
{
	fstrans_cookie_t cookie;
	cred_t *cr = CRED();
	zfsvfs_t *zfsvfs = ITOZSB(file_inode(filp));
	znode_t *dzp;
	int error = 0;

	if ((error = zpl_enter(zfsvfs, FTAG)) != 0)
		return (error);
	cookie = spl_fstrans_mark();

	if (zfsvfs->z_shares_dir == 0) {
		dir_emit_dots(filp, ctx);
		goto out;
	}

	error = -zfs_zget(zfsvfs, zfsvfs->z_shares_dir, &dzp);
	if (error)
		goto out;

	crhold(cr);
	error = -zfs_readdir(ZTOI(dzp), ctx, cr);
	crfree(cr);

	iput(ZTOI(dzp));
out:
	spl_fstrans_unmark(cookie);
	zpl_exit(zfsvfs, FTAG);
	ASSERT3S(error, <=, 0);

	return (error);
}

static int
#ifdef HAVE_USERNS_IOPS_GETATTR
zpl_shares_getattr_impl(struct user_namespace *user_ns,
    const struct path *path, struct kstat *stat, u32 request_mask,
    unsigned int query_flags)
#elif defined(HAVE_IDMAP_IOPS_GETATTR)
zpl_shares_getattr_impl(struct mnt_idmap *user_ns,
    const struct path *path, struct kstat *stat, u32 request_mask,
    unsigned int query_flags)
#else
zpl_shares_getattr_impl(const struct path *path, struct kstat *stat,
    u32 request_mask, unsigned int query_flags)
#endif
{
	(void) request_mask, (void) query_flags;
	struct inode *ip = path->dentry->d_inode;
	zfsvfs_t *zfsvfs = ITOZSB(ip);
	znode_t *dzp;
	int error;

	if ((error = zpl_enter(zfsvfs, FTAG)) != 0)
		return (error);

	if (zfsvfs->z_shares_dir == 0) {
#if (defined(HAVE_USERNS_IOPS_GETATTR) || defined(HAVE_IDMAP_IOPS_GETATTR))
#ifdef HAVE_GENERIC_FILLATTR_USERNS
		generic_fillattr(user_ns, path->dentry->d_inode, stat);
#elif defined(HAVE_GENERIC_FILLATTR_IDMAP)
		generic_fillattr(user_ns, path->dentry->d_inode, stat);
#elif defined(HAVE_GENERIC_FILLATTR_IDMAP_REQMASK)
	generic_fillattr(user_ns, request_mask, ip, stat);
#else
		(void) user_ns;
#endif
#else
		generic_fillattr(path->dentry->d_inode, stat);
#endif
		stat->nlink = stat->size = 2;
		stat->atime = current_time(ip);
		zpl_exit(zfsvfs, FTAG);
		return (0);
	}

	error = -zfs_zget(zfsvfs, zfsvfs->z_shares_dir, &dzp);
	if (error == 0) {
#ifdef HAVE_GENERIC_FILLATTR_IDMAP_REQMASK
		error = -zfs_getattr_fast(user_ns, request_mask, ZTOI(dzp),
		    stat);
#elif (defined(HAVE_USERNS_IOPS_GETATTR) || defined(HAVE_IDMAP_IOPS_GETATTR))
		error = -zfs_getattr_fast(user_ns, ZTOI(dzp), stat);
#else
		error = -zfs_getattr_fast(kcred->user_ns, ZTOI(dzp), stat);
#endif
		iput(ZTOI(dzp));
	}

	zpl_exit(zfsvfs, FTAG);
	ASSERT3S(error, <=, 0);

	return (error);
}
ZPL_GETATTR_WRAPPER(zpl_shares_getattr);

/*
 * The '.zfs/shares' directory file operations.
 */
const struct file_operations zpl_fops_shares = {
	.open		= zpl_common_open,
	.llseek		= generic_file_llseek,
	.read		= generic_read_dir,
	.iterate_shared	= zpl_shares_iterate,
};

/*
 * The '.zfs/shares' directory inode operations.
 */
const struct inode_operations zpl_ops_shares = {
	.lookup		= zpl_shares_lookup,
	.getattr	= zpl_shares_getattr,
};
