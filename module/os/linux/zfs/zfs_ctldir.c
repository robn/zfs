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
 *
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright (C) 2011 Lawrence Livermore National Security, LLC.
 * Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 * LLNL-CODE-403049.
 * Rewritten for Linux by:
 *   Rohan Puri <rohan.puri15@gmail.com>
 *   Brian Behlendorf <behlendorf1@llnl.gov>
 * Copyright (c) 2013 by Delphix. All rights reserved.
 * Copyright 2015, OmniTI Computer Consulting, Inc. All rights reserved.
 * Copyright (c) 2018 George Melikov. All Rights Reserved.
 * Copyright (c) 2019 Datto, Inc. All rights reserved.
 * Copyright (c) 2020 The MathWorks, Inc. All rights reserved.
 */

/*
 * ZFS control directory (a.k.a. ".zfs")
 *
 * This directory provides a common location for all ZFS meta-objects.
 * Currently, this is only the 'snapshot' and 'shares' directory, but this may
 * expand in the future.  The elements are built dynamically, as the hierarchy
 * does not actually exist on disk.
 *
 * For 'snapshot', we don't want to have all snapshots always mounted, because
 * this would take up a huge amount of space in /etc/mnttab.  We have three
 * types of objects:
 *
 *	ctldir ------> snapshotdir -------> snapshot
 *                                             |
 *                                             |
 *                                             V
 *                                         mounted fs
 *
 * The 'snapshot' node contains just enough information to lookup '..' and act
 * as a mountpoint for the snapshot.  Whenever we lookup a specific snapshot, we
 * perform an automount of the underlying filesystem and return the
 * corresponding inode.
 *
 * All mounts are handled automatically by an user mode helper which invokes
 * the mount procedure.  Unmounts are handled by allowing the mount
 * point to expire so the kernel may automatically unmount it.
 *
 * The '.zfs', '.zfs/snapshot', and all directories created under
 * '.zfs/snapshot' (ie: '.zfs/snapshot/<snapname>') all share the same
 * zfsvfs_t as the head filesystem (what '.zfs' lives under).
 *
 * File systems mounted on top of the '.zfs/snapshot/<snapname>' paths
 * (ie: snapshots) are complete ZFS filesystems and have their own unique
 * zfsvfs_t.  However, the fsid reported by these mounts will be the same
 * as that used by the parent zfsvfs_t to make NFS happy.
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/time.h>
#include <sys/sysmacros.h>
#include <sys/pathname.h>
#include <sys/vfs.h>
#include <sys/zfs_ctldir.h>
#include <sys/zfs_ioctl.h>
#include <sys/zfs_vfsops.h>
#include <sys/zfs_vnops.h>
#include <sys/stat.h>
#include <sys/dmu.h>
#include <sys/dmu_objset.h>
#include <sys/dsl_destroy.h>
#include <sys/dsl_deleg.h>
#include <sys/zpl.h>
#include <sys/mntent.h>
#include <linux/fs_context.h>
#include "zfs_namecheck.h"

/*
 * Two AVL trees are maintained which contain all currently automounted
 * snapshots.  Every automounted snapshots maps to a single zfs_snapentry_t
 * entry which MUST:
 *
 *   - be attached to both trees, and
 *   - be unique, no duplicate entries are allowed.
 *
 * The zfs_snapshots_by_name tree is indexed by the full dataset name
 * while the zfs_snapshots_by_objsetid tree is indexed by the unique
 * objsetid.  This allows for fast lookups either by name or objsetid.
 */
static avl_tree_t zfs_snapshots_by_name;
static avl_tree_t zfs_snapshots_by_objsetid;
static krwlock_t zfs_snapshot_lock;

/*
 * Control Directory Tunables (.zfs)
 */
int zfs_expire_snapshot = ZFSCTL_EXPIRE_SNAPSHOT;
static int zfs_admin_snapshot = 0;
static int zfs_snapshot_no_setuid = 0;

/*
 * XXX rough state stuff. Write me better! -- robn, 2026-04-10
 *
 * MOUNTING: first caller to d_automount() is working through fc setup,
 *           fc_mount, filling out the snapentry. other arrivals will just
 *           get whatever fc_mount() returns, and we'll let the kernel sort
 *           out the graft conflicts.
 *
 * MOUNTED: we've been through mount, and the timer is armed. that's all we
 *          know; we can't really tell what's actually mounted at the
 *          snapentry position, if anything - we can check, but we don't get
 *          told when the kernel moves mounts around, so there's always a
 *          TOCTOU problem. we try our best.
 *
 * DETACHING: anything mounted at the slot is being detached so we can
 *            clean up the snapentry. can be requested directly, or might be
 *            from a timer expiry. the timer will be disarmed if this wasn't
 *            a timer request.
 *
 * DEAD: unublished, unmounted, unusable. will be freed when the last hold is
 *       released.
 */

typedef enum {
	SE_MOUNTING,	/* being mounted, others must wait */
	SE_MOUNTED,	/* up and running, please enjoy */
	SE_DETACHING,	/* detaching on demand (from expiry task) */
	SE_DEAD,	/* to be destroyed when last hold released */
} zfs_snapentry_state_t;

static const char *se_state_str[] = {
	"MOUNTING", "MOUNTED", "DETACHING", "DEAD",
};

typedef struct {
	char		*se_name;	/* full snapshot name */
	spa_t		*se_spa;	/* pool spa (NULL if pending) */
	uint64_t	se_objsetid;	/* snapshot objset id */
	avl_node_t	se_node_name;	/* zfs_snapshots_by_name link */
	avl_node_t	se_node_objsetid; /* zfs_snapshots_by_objsetid link */
	zfs_refcount_t	se_refcount;	/* reference count */

	zfs_snapentry_state_t	se_state;

	kmutex_t		se_mtx;
	kcondvar_t		se_cv;

	struct vfsmount	*se_pmnt;	/* parent mount, for unmount */
	struct dentry	*se_dentry;	/* mount root dentry, for unmount */

	taskqid_t	se_taskqid;	/* scheduled expire taskqid */
} zfs_snapentry_t;

static void
_zfs_snapentry_debug(zfs_snapentry_t *se, const char *act,
    const char *func, int line)
{
	char pbuf[256], *prefix;
	if (act == NULL)
		prefix = "snapentry";
	else {
		snprintf(pbuf, sizeof (pbuf), "snapentry[%s]", act);
		prefix = pbuf;
	}

	char dbuf[256];
	if (se->se_dentry == NULL)
		snprintf(dbuf, sizeof (dbuf), "dentry=%px", se->se_dentry);
	else
		snprintf(dbuf, sizeof (dbuf),
		    "dentry=%px [dname=%s mountpoint=%s refcnt=%u]",
		    se->se_dentry, dname(se->se_dentry),
		    d_mountpoint(se->se_dentry) ? "true" : "false",
		    d_count(se->se_dentry));

	cmn_err(CE_NOTE,
	    "%s: [%s:%d] [%s#%d] "
	    "se=%px [name=%s objsetid=%llu refcnt=%llu state=%s taskqid=%lu] "
	    "pmnt=%px %s",
	    prefix, getcomm(), getpid(), func, line,
	    se, se->se_name, se->se_objsetid,
	    zfs_refcount_count(&se->se_refcount), se_state_str[se->se_state],
	    se->se_taskqid,
	    se->se_pmnt, dbuf);
}
#define	zfs_snapentry_debug(se)			\
	_zfs_snapentry_debug(se, NULL, __FUNCTION__, __LINE__)
#define	zfs_snapentry_debug_act(se, act)	\
	_zfs_snapentry_debug(se, act, __FUNCTION__, __LINE__)

#define	zfs_snapentry_log(se, fmt, ...)		\
	cmn_err(CE_NOTE, "%s: se=%px: " fmt, __FUNCTION__, se, ##__VA_ARGS__)

static void
zfs_snapentry_wait(zfs_snapentry_t *se)
{
	ASSERT(MUTEX_HELD(&se->se_mtx));

	while (se->se_state == SE_MOUNTING || se->se_state == SE_DETACHING)
		cv_wait(&se->se_cv, &se->se_mtx);

	ASSERT(MUTEX_HELD(&se->se_mtx));
}

static void
zfs_snapentry_change_state(zfs_snapentry_t *se,
    zfs_snapentry_state_t new_state)
{
	ASSERT(MUTEX_HELD(&se->se_mtx));

	if (se->se_state == new_state)
		return;

	zfs_snapentry_log(se, "state change: %s -> %s",
	    se_state_str[se->se_state], se_state_str[new_state]);

	se->se_state = new_state;

	cv_broadcast(&se->se_cv);
}

static bool
_zfs_snapentry_validate_path(zfs_snapentry_t *se, struct path *pathp)
{
	ASSERT(MUTEX_HELD(&se->se_mtx));
	ASSERT3U(se->se_state, ==, SE_MOUNTED);
	ASSERT3P(se->se_dentry, !=, NULL);

	if (!d_mountpoint(se->se_dentry)) {
		zfs_snapentry_log(se, "not mountpoint, invalid");
		return (false);
	}

	struct path path;
	if (pathp == NULL)
		pathp = &path;

	pathp->mnt = se->se_pmnt;
	pathp->dentry = se->se_dentry;
	path_get(pathp);

	if (!follow_down_one(pathp)) {
		zfs_snapentry_log(se, "didn't find dentry under mount, invalid");
		path_put(pathp);
		return (false);
	}

	if (pathp == &path)
		path_put(pathp);

	return (true);
}

static void zfsctl_snapshot_remove(zfs_snapentry_t *se);
static void zfsctl_snapshot_expire_cancel(zfs_snapentry_t *se);

static void
_zfs_snapentry_teardown(zfs_snapentry_t *se)
{
	ASSERT(MUTEX_HELD(&se->se_mtx));

	if (se->se_state == SE_DEAD)
		return;

	ASSERT3P(se->se_dentry, !=, NULL);

	struct dentry *dentry = se->se_dentry;

	se->se_pmnt = NULL;
	se->se_dentry = NULL;

	zfsctl_snapshot_expire_cancel(se);

	/*
	 * XXX I dislike this, because se_mtx is held. however, we never
	 *     take an se_mtx with zfs_snapshot_lock held, and sleeping here
	 *     should never be for very long, because the global lock is
	 *     very narrowly used.
	 *
	 *     the alternative is to allow callers to see a SE_DEAD entry
	 *     on the global lists, which I think we don't want - what if
	 *     they're actually looking for that name? - or a separate
	 *     TEARDOWN state, which seems like a huge overkill
	 *
	 *     all that said, all the callers that arrive here (or _don't_
	 *     arrive here but could) immediately drop their se_mtx afterwards,
	 *     so we could possibly just not pickup se_mtx again afterwards.
	 *     I fear this makes it more difficult to see what's happening
	 *     though. unless, perhaps, zfs_snapentry_wait() becomes our
	 *     acquisition function?
	 *
	 *     or maybe it all just doesn't matter: its weird, but it works,
	 *     and everyone will like that.
	 *       -- robn, 2026-04-10
	 */
	rw_enter(&zfs_snapshot_lock, RW_WRITER);
	zfsctl_snapshot_remove(se);
	rw_exit(&zfs_snapshot_lock);

	zfs_snapentry_change_state(se, SE_DEAD);
	mutex_exit(&se->se_mtx);

	dput(dentry);

	mutex_enter(&se->se_mtx);
	ASSERT3U(se->se_state, ==, SE_DEAD);
}

static void
zfs_snapentry_teardown_invalid(zfs_snapentry_t *se)
{
	ASSERT(MUTEX_HELD(&se->se_mtx));

	if (se->se_state == SE_DEAD)
		return;

	ASSERT3U(se->se_state, ==, SE_MOUNTED);

	if (!_zfs_snapentry_validate_path(se, NULL))
		_zfs_snapentry_teardown(se);
}

static void exportfs_flush(void);

static void
_zfs_snapentry_detach(zfs_snapentry_t *se, bool idle)
{
	ASSERT(MUTEX_HELD(&se->se_mtx));

	if (se->se_state == SE_DEAD)
		return;

	ASSERT3U(se->se_state, ==, SE_MOUNTED);

	struct path path;

	if (!_zfs_snapentry_validate_path(se, &path)) {
		zfs_snapentry_log(se, "invalid, proceeding to teardown");
		_zfs_snapentry_teardown(se);
		return;
	}

	if (idle && !may_umount_tree(path.mnt)) {
		path_put(&path);
		zfs_snapentry_log(se, "busy");
		return;
	}

	/*
	 * MNT_INTERNAL has the side-effect that the unmount will happen "now"
	 * (inside d_invalidate()) rather than being deferred until the return
	 * to userspace. That's important to ensure that zfsctl_destroy()
	 * doesn't return until after the mount is dead (provided its not
	 * busy), otherwise the mount will hold the dataset and so cause the
	 * calling operation (eg `zfs destroy`) to fail with EBUSY.
	 *
	 * However, the thing mounted here may not be a ZFS dataset, if
	 * userspace has moved something else to this position. Messing inside
	 * someone else's mount is high danger, so we ensure its really ours
	 * before we fiddle with it.
	 *
	 * This does mean that for things that aren't our mount, they may
	 * be delayed until after we return, and so a call may fail with EBUSY.
	 * That's ok in this case; userspace has done something weird; they
	 * will have to find their own way out of it.
	 */
	bool is_our_mount = false;
	if (path.mnt->mnt_sb->s_type == &zpl_fs_type) {
		zfsvfs_t *zfsvfs = path.mnt->mnt_sb->s_fs_info;
		spa_t *spa = zfsvfs->z_os->os_spa;
		uint64_t objsetid = dmu_objset_id(zfsvfs->z_os);
		if (se->se_spa == spa && se->se_objsetid == objsetid) {
			is_our_mount = true;
		}
	}

	zfs_snapentry_log(se, "%s our mount", is_our_mount ? "is" : "is not");

	if (idle && !is_our_mount) {
		path_put(&path);
		zfs_snapentry_log(se, "not ours, busy");
		return;
	}

	if (is_our_mount)
		path.mnt->mnt_flags |= MNT_INTERNAL;
	path_put(&path);

	struct dentry *dentry = se->se_dentry;

	zfs_snapentry_change_state(se, SE_DETACHING);
	mutex_exit(&se->se_mtx);

	d_invalidate(dentry);

	if (is_our_mount)
		exportfs_flush(); /* XXX delay a moment? */

	mutex_enter(&se->se_mtx);
	ASSERT3U(se->se_state, ==, SE_DETACHING);

	_zfs_snapentry_teardown(se);
}
#define	zfs_snapentry_detach(se)	\
	_zfs_snapentry_detach(se, false)
#define	zfs_snapentry_detach_idle(se)	\
	_zfs_snapentry_detach(se, true)

/*
 * Allocate a new zfs_snapentry_t being careful to make a copy of the
 * the snapshot name No reference is taken.
 */
static zfs_snapentry_t *
zfsctl_snapshot_alloc(const char *full_name, spa_t *spa, uint64_t objsetid)
{
	zfs_snapentry_t *se;

	se = kmem_zalloc(sizeof (zfs_snapentry_t), KM_SLEEP);

	se->se_name = kmem_strdup(full_name);
	se->se_spa = spa;
	se->se_objsetid = objsetid;
	se->se_taskqid = TASKQID_INVALID;

	se->se_state = SE_MOUNTING;
	mutex_init(&se->se_mtx, NULL, MUTEX_DEFAULT, NULL);
	cv_init(&se->se_cv, NULL, CV_DEFAULT, NULL);

	zfs_refcount_create(&se->se_refcount);

	return (se);
}

/*
 * Free a zfs_snapentry_t the caller must ensure there are no active
 * references.
 */
static void
zfsctl_snapshot_free(zfs_snapentry_t *se)
{
	ASSERT(!MUTEX_HELD(&se->se_mtx));

	mutex_destroy(&se->se_mtx);
	cv_destroy(&se->se_cv);

	zfs_refcount_destroy(&se->se_refcount);
	kmem_strfree(se->se_name);

	kmem_free(se, sizeof (zfs_snapentry_t));
}

/*
 * Hold a reference on the zfs_snapentry_t.
 */
static void
zfsctl_snapshot_hold(zfs_snapentry_t *se)
{
	zfs_refcount_add(&se->se_refcount, NULL);
}

/*
 * Release a reference on the zfs_snapentry_t.  When the number of
 * references drops to zero the structure will be freed.
 */
static void
zfsctl_snapshot_rele(zfs_snapentry_t *se)
{
	if (zfs_refcount_remove(&se->se_refcount, NULL) == 0)
		zfsctl_snapshot_free(se);
}

#ifdef SNAPENTRY_DEBUG_DUMP
static void
zfsctl_snapshot_dump(void)
{
	rw_enter(&zfs_snapshot_lock, RW_READER);
	for (zfs_snapentry_t *se = avl_first(&zfs_snapshots_by_name);
	    se != NULL; se = AVL_NEXT(&zfs_snapshots_by_name, se))
		zfs_snapentry_debug(se);
	rw_exit(&zfs_snapshot_lock);
}
#endif

/*
 * Add a zfs_snapentry_t to the zfs_snapshots_by_name tree.  If the entry
 * is not pending (se_spa != NULL), also add to zfs_snapshots_by_objsetid.
 * While the zfs_snapentry_t is part of the trees a reference is held.
 */
static void
zfsctl_snapshot_add(zfs_snapentry_t *se)
{
	ASSERT(RW_WRITE_HELD(&zfs_snapshot_lock));
	zfsctl_snapshot_hold(se);
	avl_add(&zfs_snapshots_by_name, se);
	avl_add(&zfs_snapshots_by_objsetid, se);
}

/*
 * Remove a zfs_snapentry_t from the zfs_snapshots_by_name tree and
 * zfs_snapshots_by_objsetid tree (if not pending).  Upon removal a
 * reference is dropped, this can result in the structure being freed
 * if that was the last remaining reference.
 */
static void
zfsctl_snapshot_remove(zfs_snapentry_t *se)
{
	ASSERT(RW_WRITE_HELD(&zfs_snapshot_lock));
	avl_remove(&zfs_snapshots_by_name, se);
	avl_remove(&zfs_snapshots_by_objsetid, se);
	zfsctl_snapshot_rele(se);
}

/*
 * Snapshot name comparison function for the zfs_snapshots_by_name.
 */
static int
snapentry_compare_by_name(const void *a, const void *b)
{
	const zfs_snapentry_t *se_a = a;
	const zfs_snapentry_t *se_b = b;

	return (TREE_ISIGN(strcmp(se_a->se_name, se_b->se_name)));
}

/*
 * Snapshot name comparison function for the zfs_snapshots_by_objsetid.
 */
static int
snapentry_compare_by_objsetid(const void *a, const void *b)
{
	const zfs_snapentry_t *se_a = a;
	const zfs_snapentry_t *se_b = b;

	int cmp = TREE_PCMP(se_a->se_spa, se_b->se_spa);
	if (cmp != 0)
		return (cmp);
	return (TREE_CMP(se_a->se_objsetid, se_b->se_objsetid));
}

/*
 * Find a zfs_snapentry_t in zfs_snapshots_by_name.  If the snapname
 * is found a pointer to the zfs_snapentry_t is returned and a reference
 * taken on the structure.  The caller is responsible for dropping the
 * reference with zfsctl_snapshot_rele().  If the snapname is not found
 * NULL will be returned.
 */
static zfs_snapentry_t *
zfsctl_snapshot_find_by_name(const char *snapname)
{
	zfs_snapentry_t *se, search;

	ASSERT(RW_LOCK_HELD(&zfs_snapshot_lock));

	search.se_name = (char *)snapname;
	se = avl_find(&zfs_snapshots_by_name, &search, NULL);
	if (se)
		zfsctl_snapshot_hold(se);

	return (se);
}

/*
 * Find a zfs_snapentry_t in zfs_snapshots_by_objsetid given the objset id
 * rather than the snapname.  In all other respects it behaves the same
 * as zfsctl_snapshot_find_by_name().
 */
static zfs_snapentry_t *
zfsctl_snapshot_find_by_objsetid(spa_t *spa, uint64_t objsetid)
{
	zfs_snapentry_t *se, search;

	ASSERT(RW_LOCK_HELD(&zfs_snapshot_lock));

	search.se_spa = spa;
	search.se_objsetid = objsetid;
	se = avl_find(&zfs_snapshots_by_objsetid, &search, NULL);
	if (se)
		zfsctl_snapshot_hold(se);

	return (se);
}

/*
 * Dispatch the unmount task for delayed handling with a hold protecting it.
 */
static void zfsctl_snapshot_expire_task(void *);

static void
zfsctl_snapshot_expire_delay(zfs_snapentry_t *se, int delay)
{
	ASSERT(MUTEX_HELD(&se->se_mtx));

	if (delay <= 0)
		return;

	/*
	 * Timer already armed, so don't rearm it. The expiry task will
	 * rearm it when it fires.
	 */
	if (se->se_taskqid != TASKQID_INVALID)
		return;

	zfs_snapentry_log(se, "arming timer: delay=%d", delay);

	zfsctl_snapshot_hold(se);
	se->se_taskqid = taskq_dispatch_delay(system_delay_taskq,
	    zfsctl_snapshot_expire_task, se, TQ_SLEEP,
	    ddi_get_lbolt() + delay * HZ);
}

/*
 * Delayed task responsible for unmounting an expired automounted snapshot.
 */
static void
zfsctl_snapshot_expire_task(void *data)
{
	zfs_snapentry_t *se = (zfs_snapentry_t *)data;

	mutex_enter(&se->se_mtx);
	zfs_snapentry_debug(se);
	zfs_snapentry_wait(se);

	se->se_taskqid = TASKQID_INVALID;

	if (zfs_expire_snapshot <= 0) {
		/*
		 * Expiry was disabled via tunable since we armed the timer;
		 * go through the motions but don't actually unmount anything.
		 */
		zfs_snapentry_teardown_invalid(se);
		zfs_snapentry_debug(se);
		mutex_exit(&se->se_mtx);
		zfsctl_snapshot_rele(se);
		return;
	}

	zfs_snapentry_detach_idle(se);

	if (se->se_state == SE_MOUNTED) {
		/*
		 * Snapshot still active; re-arm the expiry timer. Will take a
		 * fresh hold if the timer is set, so its always safe to
		 * release ours below.
		 */
		zfsctl_snapshot_expire_delay(se, zfs_expire_snapshot);
	}

	zfs_snapentry_debug(se);
	mutex_exit(&se->se_mtx);
	zfsctl_snapshot_rele(se);
}

/*
 * Cancel an automatic unmount of a snapname.  This callback is responsible
 * for dropping the reference on the zfs_snapentry_t which was taken when
 * during dispatch.
 */
static void
zfsctl_snapshot_expire_cancel(zfs_snapentry_t *se)
{
	ASSERT(MUTEX_HELD(&se->se_mtx));

	if (se->se_taskqid == TASKQID_INVALID)
		return;

	zfs_snapentry_log(se, "disarming timer");

	int err = taskq_cancel_id(system_delay_taskq, se->se_taskqid, B_FALSE);
	/*
	 * Clear taskqid only if we successfully cancelled before execution.
	 * For ENOENT, task already cleared it. For EBUSY, task will clear
	 * it when done.
	 */
	if (err == 0) {
		se->se_taskqid = TASKQID_INVALID;
		zfsctl_snapshot_rele(se);
	}
}

/*
 * Schedule an automatic unmount of objset id to occur in delay seconds from
 * now.  Any previous delayed unmount will be cancelled in favor of the
 * updated deadline.  A reference is taken by zfsctl_snapshot_find_by_name()
 * and held until the outstanding task is handled or cancelled.
 */
int
zfsctl_snapshot_unmount_delay(spa_t *spa, uint64_t objsetid, int delay)
{
	zfs_snapentry_t *se;

	rw_enter(&zfs_snapshot_lock, RW_READER);
	se = zfsctl_snapshot_find_by_objsetid(spa, objsetid);
	rw_exit(&zfs_snapshot_lock);

	if (se == NULL)
		return (SET_ERROR(ENOENT));

	mutex_enter(&se->se_mtx);
	zfs_snapentry_wait(se);
	if (se->se_state == SE_DEAD) {
		mutex_exit(&se->se_mtx);
		zfsctl_snapshot_rele(se);
		return (SET_ERROR(ENOENT));
	}

	zfsctl_snapshot_expire_cancel(se);
	zfsctl_snapshot_expire_delay(se, delay);
	mutex_exit(&se->se_mtx);

	zfsctl_snapshot_rele(se);

	return (0);
}

/*
 * Check if the given inode is a part of the virtual .zfs directory.
 */
boolean_t
zfsctl_is_node(struct inode *ip)
{
	return (ITOZ(ip)->z_is_ctldir);
}

/*
 * Check if the given inode is a .zfs/snapshots/snapname directory.
 */
boolean_t
zfsctl_is_snapdir(struct inode *ip)
{
	return (zfsctl_is_node(ip) && (ip->i_ino <= ZFSCTL_INO_SNAPDIRS));
}

/*
 * Allocate a new inode with the passed id and ops.
 */
static struct inode *
zfsctl_inode_alloc(zfsvfs_t *zfsvfs, uint64_t id,
    const struct file_operations *fops, const struct inode_operations *ops,
    uint64_t creation)
{
	struct inode *ip;
	znode_t *zp;
	inode_timespec_t now = {.tv_sec = creation};

	ip = new_inode(zfsvfs->z_sb);
	if (ip == NULL)
		return (NULL);

	if (!creation)
		now = current_time(ip);
	zp = ITOZ(ip);
	ASSERT0P(zp->z_dirlocks);
	ASSERT0P(zp->z_acl_cached);
	ASSERT0P(zp->z_xattr_cached);
	zp->z_id = id;
	zp->z_unlinked = B_FALSE;
	zp->z_atime_dirty = B_FALSE;
	zp->z_zn_prefetch = B_FALSE;
	zp->z_is_sa = B_FALSE;
	zp->z_is_ctldir = B_TRUE;
	zp->z_sa_hdl = NULL;
	zp->z_blksz = 0;
	zp->z_seq = 0;
	zp->z_mapcnt = 0;
	zp->z_size = 0;
	zp->z_pflags = 0;
	zp->z_mode = 0;
	zp->z_sync_cnt = 0;
	ip->i_generation = 0;
	ip->i_ino = id;
	ip->i_mode = (S_IFDIR | S_IRWXUGO);
	ip->i_uid = SUID_TO_KUID(0);
	ip->i_gid = SGID_TO_KGID(0);
	ip->i_blkbits = SPA_MINBLOCKSHIFT;
	zpl_inode_set_atime_to_ts(ip, now);
	zpl_inode_set_mtime_to_ts(ip, now);
	zpl_inode_set_ctime_to_ts(ip, now);
	ip->i_fop = fops;
	ip->i_op = ops;
#if defined(IOP_XATTR)
	ip->i_opflags &= ~IOP_XATTR;
#endif

	if (insert_inode_locked(ip)) {
		unlock_new_inode(ip);
		iput(ip);
		return (NULL);
	}

	mutex_enter(&zfsvfs->z_znodes_lock);
	list_insert_tail(&zfsvfs->z_all_znodes, zp);
	membar_producer();
	mutex_exit(&zfsvfs->z_znodes_lock);

	unlock_new_inode(ip);

	return (ip);
}

/*
 * Lookup the inode with given id, it will be allocated if needed.
 */
static struct inode *
zfsctl_inode_lookup(zfsvfs_t *zfsvfs, uint64_t id,
    const struct file_operations *fops, const struct inode_operations *ops)
{
	struct inode *ip = NULL;
	uint64_t creation = 0;
	dsl_dataset_t *snap_ds;
	dsl_pool_t *pool;

	while (ip == NULL) {
		ip = ilookup(zfsvfs->z_sb, (unsigned long)id);
		if (ip)
			break;

		if (id <= ZFSCTL_INO_SNAPDIRS && !creation) {
			pool = dmu_objset_pool(zfsvfs->z_os);
			dsl_pool_config_enter(pool, FTAG);
			if (!dsl_dataset_hold_obj(pool,
			    ZFSCTL_INO_SNAPDIRS - id, FTAG, &snap_ds)) {
				creation = dsl_get_creation(snap_ds);
				dsl_dataset_rele(snap_ds, FTAG);
			}
			dsl_pool_config_exit(pool, FTAG);
		 }

		/* May fail due to concurrent zfsctl_inode_alloc() */
		ip = zfsctl_inode_alloc(zfsvfs, id, fops, ops, creation);
	}

	return (ip);
}

/*
 * Create the '.zfs' directory.  This directory is cached as part of the VFS
 * structure.  This results in a hold on the zfsvfs_t.  The code in zfs_umount()
 * therefore checks against a vfs_count of 2 instead of 1.  This reference
 * is removed when the ctldir is destroyed in the unmount.  All other entities
 * under the '.zfs' directory are created dynamically as needed.
 *
 * Because the dynamically created '.zfs' directory entries assume the use
 * of 64-bit inode numbers this support must be disabled on 32-bit systems.
 */
int
zfsctl_create(zfsvfs_t *zfsvfs)
{
	ASSERT0P(zfsvfs->z_ctldir);

	zfsvfs->z_ctldir = zfsctl_inode_alloc(zfsvfs, ZFSCTL_INO_ROOT,
	    &zpl_fops_root, &zpl_ops_root, 0);
	if (zfsvfs->z_ctldir == NULL)
		return (SET_ERROR(ENOENT));

	return (0);
}

/*
 * Destroy the '.zfs' directory or remove a snapshot from zfs_snapshots_by_name.
 * Only called when the filesystem is unmounted.
 */
void
zfsctl_destroy(zfsvfs_t *zfsvfs)
{
	if (zfsvfs->z_issnap) {
		zfs_snapentry_t *se;
		spa_t *spa = zfsvfs->z_os->os_spa;
		uint64_t objsetid = dmu_objset_id(zfsvfs->z_os);

		rw_enter(&zfs_snapshot_lock, RW_READER);
		se = zfsctl_snapshot_find_by_objsetid(spa, objsetid);
		rw_exit(&zfs_snapshot_lock);

		if (se != NULL) {
			cmn_err(CE_NOTE, "zfsctl_destroy: se=%px state=%d", se, se->se_state);

			mutex_enter(&se->se_mtx);
			/*
			 * Don't detach if we're already detaching; likely
			 * we're being called as a result of d_invalidate()
			 * and waiting here would deadlock.
			 */
			if (se->se_state != SE_DETACHING) {
				zfs_snapentry_wait(se);
				zfs_snapentry_detach(se);
			}
			mutex_exit(&se->se_mtx);

			zfsctl_snapshot_rele(se);
		} else {
			cmn_err(CE_NOTE, "zfsctl_destroy: snap spa=%s objsetid=%llu no snapentry", spa_name(spa), objsetid);
		}
	} else if (zfsvfs->z_ctldir) {
		char dsname[ZFS_MAX_DATASET_NAME_LEN];
		dmu_objset_name(zfsvfs->z_os, dsname);
		size_t dsnamelen = strlen(dsname);

		cmn_err(CE_NOTE, "zfsctl_destroy: dsname=%s: detaching snapshots", dsname);

		rw_enter(&zfs_snapshot_lock, RW_READER);
		zfs_snapentry_t *se = avl_first(&zfs_snapshots_by_name);

		while (se != NULL) {
			if (strncmp(se->se_name, dsname, dsnamelen) != 0 ||
			    se->se_name[dsnamelen] != '@') {
				se = AVL_NEXT(&zfs_snapshots_by_name, se);
				continue;
			}

			zfsctl_snapshot_hold(se);
			rw_exit(&zfs_snapshot_lock);

			mutex_enter(&se->se_mtx);
			zfs_snapentry_wait(se);
			zfs_snapentry_detach(se);
			mutex_exit(&se->se_mtx);

			zfsctl_snapshot_rele(se);

			rw_enter(&zfs_snapshot_lock, RW_READER);
			se = avl_first(&zfs_snapshots_by_name);
		}

		rw_exit(&zfs_snapshot_lock);

		cmn_err(CE_NOTE, "zfsctl_destroy: dsname=%s: collapsing snapdir", dsname);
		iput(zfsvfs->z_ctldir);
		zfsvfs->z_ctldir = NULL;
	}
}

/*
 * Given a root znode, retrieve the associated .zfs directory.
 * Add a hold to the vnode and return it.
 */
struct inode *
zfsctl_root(znode_t *zp)
{
	ASSERT(zfs_has_ctldir(zp));
	/* Must have an existing ref, so igrab() cannot return NULL */
	VERIFY3P(igrab(ZTOZSB(zp)->z_ctldir), !=, NULL);
	return (ZTOZSB(zp)->z_ctldir);
}

/*
 * Generate a long fid to indicate a snapdir. We encode whether snapdir is
 * already mounted in gen field. We do this because nfsd lookup will not
 * trigger automount. Next time the nfsd does fh_to_dentry, we will notice
 * this and do automount and return ESTALE to force nfsd revalidate and follow
 * mount.
 */
static int
zfsctl_snapdir_fid(struct inode *ip, fid_t *fidp)
{
	zfid_short_t *zfid = (zfid_short_t *)fidp;
	zfid_long_t *zlfid = (zfid_long_t *)fidp;
	uint32_t gen = 0;
	uint64_t object;
	uint64_t objsetid;
	int i;
	struct dentry *dentry;

	if (fidp->fid_len < LONG_FID_LEN) {
		fidp->fid_len = LONG_FID_LEN;
		return (SET_ERROR(ENOSPC));
	}

	object = ip->i_ino;
	objsetid = ZFSCTL_INO_SNAPDIRS - ip->i_ino;
	zfid->zf_len = LONG_FID_LEN;

	dentry = d_obtain_alias(igrab(ip));
	if (!IS_ERR(dentry)) {
		gen = !!d_mountpoint(dentry);
		dput(dentry);
	}

	for (i = 0; i < sizeof (zfid->zf_object); i++)
		zfid->zf_object[i] = (uint8_t)(object >> (8 * i));

	for (i = 0; i < sizeof (zfid->zf_gen); i++)
		zfid->zf_gen[i] = (uint8_t)(gen >> (8 * i));

	for (i = 0; i < sizeof (zlfid->zf_setid); i++)
		zlfid->zf_setid[i] = (uint8_t)(objsetid >> (8 * i));

	for (i = 0; i < sizeof (zlfid->zf_setgen); i++)
		zlfid->zf_setgen[i] = 0;

	return (0);
}

/*
 * Generate an appropriate fid for an entry in the .zfs directory.
 */
int
zfsctl_fid(struct inode *ip, fid_t *fidp)
{
	znode_t		*zp = ITOZ(ip);
	zfsvfs_t	*zfsvfs = ITOZSB(ip);
	uint64_t	object = zp->z_id;
	zfid_short_t	*zfid;
	int		i;
	int		error;

	if ((error = zfs_enter(zfsvfs, FTAG)) != 0)
		return (error);

	if (zfsctl_is_snapdir(ip)) {
		zfs_exit(zfsvfs, FTAG);
		return (zfsctl_snapdir_fid(ip, fidp));
	}

	if (fidp->fid_len < SHORT_FID_LEN) {
		fidp->fid_len = SHORT_FID_LEN;
		zfs_exit(zfsvfs, FTAG);
		return (SET_ERROR(ENOSPC));
	}

	zfid = (zfid_short_t *)fidp;

	zfid->zf_len = SHORT_FID_LEN;

	for (i = 0; i < sizeof (zfid->zf_object); i++)
		zfid->zf_object[i] = (uint8_t)(object >> (8 * i));

	/* .zfs znodes always have a generation number of 0 */
	for (i = 0; i < sizeof (zfid->zf_gen); i++)
		zfid->zf_gen[i] = 0;

	zfs_exit(zfsvfs, FTAG);
	return (0);
}

/*
 * Construct a full dataset name in full_name: "pool/dataset@snap_name"
 */
static int
zfsctl_snapshot_name(zfsvfs_t *zfsvfs, const char *snap_name, int len,
    char *full_name)
{
	objset_t *os = zfsvfs->z_os;

	if (zfs_component_namecheck(snap_name, NULL, NULL) != 0)
		return (SET_ERROR(EILSEQ));

	dmu_objset_name(os, full_name);
	if ((strlen(full_name) + 1 + strlen(snap_name)) >= len)
		return (SET_ERROR(ENAMETOOLONG));

	(void) strcat(full_name, "@");
	(void) strcat(full_name, snap_name);

	return (0);
}

/*
 * Returns full path in full_path: "/pool/dataset/.zfs/snapshot/snap_name/"
 */
static int
zfsctl_snapshot_path_objset(zfsvfs_t *zfsvfs, uint64_t objsetid,
    int path_len, char *full_path)
{
	objset_t *os = zfsvfs->z_os;
	fstrans_cookie_t cookie;
	char *snapname;
	boolean_t case_conflict;
	uint64_t id, pos = 0;
	int error = 0;

	cookie = spl_fstrans_mark();
	snapname = kmem_alloc(ZFS_MAX_DATASET_NAME_LEN, KM_SLEEP);

	while (error == 0) {
		dsl_pool_config_enter(dmu_objset_pool(os), FTAG);
		error = dmu_snapshot_list_next(zfsvfs->z_os,
		    ZFS_MAX_DATASET_NAME_LEN, snapname, &id, &pos,
		    &case_conflict);
		dsl_pool_config_exit(dmu_objset_pool(os), FTAG);
		if (error)
			goto out;

		if (id == objsetid)
			break;
	}

	mutex_enter(&zfsvfs->z_vfs->vfs_mntpt_lock);
	if (zfsvfs->z_vfs->vfs_mntpoint != NULL) {
		snprintf(full_path, path_len, "%s/.zfs/snapshot/%s",
		    zfsvfs->z_vfs->vfs_mntpoint, snapname);
	} else
		error = SET_ERROR(ENOENT);
	mutex_exit(&zfsvfs->z_vfs->vfs_mntpt_lock);

out:
	kmem_free(snapname, ZFS_MAX_DATASET_NAME_LEN);
	spl_fstrans_unmark(cookie);

	return (error);
}

/*
 * Special case the handling of "..".
 */
int
zfsctl_root_lookup(struct inode *dip, const char *name, struct inode **ipp,
    int flags, cred_t *cr, int *direntflags, pathname_t *realpnp)
{
	zfsvfs_t *zfsvfs = ITOZSB(dip);
	int error = 0;

	if ((error = zfs_enter(zfsvfs, FTAG)) != 0)
		return (error);

	if (zfsvfs->z_show_ctldir == ZFS_SNAPDIR_DISABLED) {
		*ipp = NULL;
	} else if (strcmp(name, "..") == 0) {
		*ipp = dip->i_sb->s_root->d_inode;
	} else if (strcmp(name, ZFS_SNAPDIR_NAME) == 0) {
		*ipp = zfsctl_inode_lookup(zfsvfs, ZFSCTL_INO_SNAPDIR,
		    &zpl_fops_snapdir, &zpl_ops_snapdir);
	} else if (strcmp(name, ZFS_SHAREDIR_NAME) == 0) {
		*ipp = zfsctl_inode_lookup(zfsvfs, ZFSCTL_INO_SHARES,
		    &zpl_fops_shares, &zpl_ops_shares);
	} else {
		*ipp = NULL;
	}

	if (*ipp == NULL)
		error = SET_ERROR(ENOENT);

	zfs_exit(zfsvfs, FTAG);

	return (error);
}

/*
 * Lookup entry point for the 'snapshot' directory.  Try to open the
 * snapshot if it exist, creating the pseudo filesystem inode as necessary.
 */
int
zfsctl_snapdir_lookup(struct inode *dip, const char *name, struct inode **ipp,
    int flags, cred_t *cr, int *direntflags, pathname_t *realpnp)
{
	zfsvfs_t *zfsvfs = ITOZSB(dip);
	uint64_t id;
	int error;

	if ((error = zfs_enter(zfsvfs, FTAG)) != 0)
		return (error);

	error = dmu_snapshot_lookup(zfsvfs->z_os, name, &id);
	if (error) {
		zfs_exit(zfsvfs, FTAG);
		return (error);
	}

	*ipp = zfsctl_inode_lookup(zfsvfs, ZFSCTL_INO_SNAPDIRS - id,
	    &simple_dir_operations, &simple_dir_inode_operations);
	if (*ipp == NULL)
		error = SET_ERROR(ENOENT);

	zfs_exit(zfsvfs, FTAG);

	return (error);
}

/*
 * Renaming a directory under '.zfs/snapshot' will automatically trigger
 * a rename of the snapshot to the new given name.  The rename is confined
 * to the '.zfs/snapshot' directory snapshots cannot be moved elsewhere.
 */
int
zfsctl_snapdir_rename(struct inode *sdip, const char *snm,
    struct inode *tdip, const char *tnm, cred_t *cr, int flags)
{
	zfsvfs_t *zfsvfs = ITOZSB(sdip);
	char *to, *from, *real, *fsname;
	int error;

	if (!zfs_admin_snapshot)
		return (SET_ERROR(EACCES));

	if ((error = zfs_enter(zfsvfs, FTAG)) != 0)
		return (error);

	to = kmem_alloc(ZFS_MAX_DATASET_NAME_LEN, KM_SLEEP);
	from = kmem_alloc(ZFS_MAX_DATASET_NAME_LEN, KM_SLEEP);
	real = kmem_alloc(ZFS_MAX_DATASET_NAME_LEN, KM_SLEEP);
	fsname = kmem_alloc(ZFS_MAX_DATASET_NAME_LEN, KM_SLEEP);

	if (zfsvfs->z_case == ZFS_CASE_INSENSITIVE) {
		error = dmu_snapshot_realname(zfsvfs->z_os, snm, real,
		    ZFS_MAX_DATASET_NAME_LEN, NULL);
		if (error == 0) {
			snm = real;
		} else if (error != ENOTSUP) {
			goto out;
		}
	}

	dmu_objset_name(zfsvfs->z_os, fsname);

	error = zfsctl_snapshot_name(ITOZSB(sdip), snm,
	    ZFS_MAX_DATASET_NAME_LEN, from);
	if (error == 0)
		error = zfsctl_snapshot_name(ITOZSB(tdip), tnm,
		    ZFS_MAX_DATASET_NAME_LEN, to);
	if (error == 0)
		error = zfs_secpolicy_rename_perms(from, to, cr);
	if (error != 0)
		goto out;

	/*
	 * Cannot move snapshots out of the snapdir.
	 */
	if (sdip != tdip) {
		error = SET_ERROR(EINVAL);
		goto out;
	}

	/*
	 * No-op when names are identical.
	 */
	if (strcmp(snm, tnm) == 0) {
		error = 0;
		goto out;
	}

	/*
	 * For the rename proper, we need to ensure the that all snapentry
	 * names remain where they are until we change them. That means the
	 * write lock.
	 */
retry:
	rw_enter(&zfs_snapshot_lock, RW_WRITER);
	zfs_snapentry_t *se = zfsctl_snapshot_find_by_name(snm);

	if (se != NULL) {
		/*
		 * If the old entry exists (almost certainly, but might have
		 * been expired since the call was made), we need to make sure
		 * its in a safe state before we change its name. To check
		 * state we must take se_mtx, but to avoid an inversion we have
		 * to drop the snapshot lock first.
		 */
		rw_exit(&zfs_snapshot_lock);

		/* Take lock, and wait for a stable state */
		mutex_enter(&se->se_mtx);
		zfs_snapentry_wait(se);

		if (se->se_state == SE_DEAD) {
			/*
			 * While locks were down, it was removed from the
			 * global list. Drop locks and go around again, in
			 * case a new entry was created for the old name.
			 */
			mutex_exit(&se->se_mtx);
			zfsctl_snapshot_rele(se);
			goto retry;
		}

		/*
		 * snapentry is in the MOUNTED state, and can't move while
		 * we have se_mtx. Pick up the snapshot lock again to protect
		 * the new name as well.
		 */
		rw_enter(&zfs_snapshot_lock, RW_WRITER);
	}

	/* Rename the snapshot proper. */
	error = dsl_dataset_rename_snapshot(fsname, snm, tnm, B_FALSE);

	if (error == 0 && se != NULL) {
		/* Success, now rename the snapshot entry proper. */
		ASSERT3U(se->se_state, ==, SE_MOUNTED);

		/*
		 * The new name can't be on the global lists already; the
		 * target name didn't already exist or
		 * dsl_dataset_rename_snapshot() would have failed, and there
		 * can't be any left over from a previous generation of that
		 * name, because they would have unmounted and removed when
		 * the name was released. Still, it's tricky, so lets assert
		 * that.
		 */
		ASSERT0P(zfsctl_snapshot_find_by_name(tnm));

		/* Remove entry, change the name, and put it back. */
		zfsctl_snapshot_remove(se);
		kmem_strfree(se->se_name);
		se->se_name = kmem_strdup(tnm);
		zfsctl_snapshot_add(se);
	}

	/* Make the new name visible. */
	rw_exit(&zfs_snapshot_lock);

	if (se != NULL) {
		/* Release the entry. */
		mutex_exit(&se->se_mtx);
		zfsctl_snapshot_rele(se);
	}

out:
	kmem_free(from, ZFS_MAX_DATASET_NAME_LEN);
	kmem_free(to, ZFS_MAX_DATASET_NAME_LEN);
	kmem_free(real, ZFS_MAX_DATASET_NAME_LEN);
	kmem_free(fsname, ZFS_MAX_DATASET_NAME_LEN);

	zfs_exit(zfsvfs, FTAG);

	return (error);
}

/*
 * Removing a directory under '.zfs/snapshot' will automatically trigger
 * the removal of the snapshot with the given name.
 */
int
zfsctl_snapdir_remove(struct inode *dip, const char *name, cred_t *cr,
    int flags)
{
	zfsvfs_t *zfsvfs = ITOZSB(dip);
	char *snapname, *real;
	int error;

	if (!zfs_admin_snapshot)
		return (SET_ERROR(EACCES));

	if ((error = zfs_enter(zfsvfs, FTAG)) != 0)
		return (error);

	snapname = kmem_alloc(ZFS_MAX_DATASET_NAME_LEN, KM_SLEEP);
	real = kmem_alloc(ZFS_MAX_DATASET_NAME_LEN, KM_SLEEP);

	if (zfsvfs->z_case == ZFS_CASE_INSENSITIVE) {
		error = dmu_snapshot_realname(zfsvfs->z_os, name, real,
		    ZFS_MAX_DATASET_NAME_LEN, NULL);
		if (error == 0) {
			name = real;
		} else if (error != ENOTSUP) {
			goto out;
		}
	}

	error = zfsctl_snapshot_name(ITOZSB(dip), name,
	    ZFS_MAX_DATASET_NAME_LEN, snapname);
	if (error == 0)
		error = zfs_secpolicy_destroy_perms(snapname, cr);
	if (error != 0)
		goto out;

	error = zfsctl_snapshot_unmount(snapname, MNT_FORCE);
	if ((error == 0) || (error == ENOENT))
		error = dsl_destroy_snapshot(snapname, B_FALSE);
out:
	kmem_free(snapname, ZFS_MAX_DATASET_NAME_LEN);
	kmem_free(real, ZFS_MAX_DATASET_NAME_LEN);

	zfs_exit(zfsvfs, FTAG);

	return (error);
}

/*
 * Creating a directory under '.zfs/snapshot' will automatically trigger
 * the creation of a new snapshot with the given name.
 */
int
zfsctl_snapdir_mkdir(struct inode *dip, const char *dirname, vattr_t *vap,
    struct inode **ipp, cred_t *cr, int flags)
{
	zfsvfs_t *zfsvfs = ITOZSB(dip);
	char *dsname;
	int error;

	if (!zfs_admin_snapshot)
		return (SET_ERROR(EACCES));

	dsname = kmem_alloc(ZFS_MAX_DATASET_NAME_LEN, KM_SLEEP);

	if (zfs_component_namecheck(dirname, NULL, NULL) != 0) {
		error = SET_ERROR(EILSEQ);
		goto out;
	}

	dmu_objset_name(zfsvfs->z_os, dsname);

	error = zfs_secpolicy_snapshot_perms(dsname, cr);
	if (error != 0)
		goto out;

	if (error == 0) {
		error = dmu_objset_snapshot_one(dsname, dirname);
		if (error != 0)
			goto out;

		error = zfsctl_snapdir_lookup(dip, dirname, ipp,
		    0, cr, NULL, NULL);
	}
out:
	kmem_free(dsname, ZFS_MAX_DATASET_NAME_LEN);

	return (error);
}

/*
 * Flush everything out of the kernel's export table and such.
 * This is needed as once the snapshot is used over NFS, its
 * entries in svc_export and svc_expkey caches hold reference
 * to the snapshot mount point. There is no known way of flushing
 * only the entries related to the snapshot.
 */
static void
exportfs_flush(void)
{
	char *argv[] = { "/usr/sbin/exportfs", "-f", NULL };
	char *envp[] = { NULL };

	(void) call_usermodehelper(argv[0], argv, envp, UMH_WAIT_PROC);
}

/*
 * XXX Called from zfs_snapdir_remove(), zfs_destroy_unmount_origin() and
 *     zfs_ioc_destroy_snaps() to "forcibly" unmount whatever is in this
 *     position. Need to check call sites and understand what they're
 *     expecting; possibly this should not be have an busy check, but also,
 *     what is the expected behaviour if there's some other weird mount
 *     hanging out here?
 *	 -- robn, 2026-04-10
 */
int
zfsctl_snapshot_unmount(const char *snapname, int flags)
{
	zfs_snapentry_t *se;

	cmn_err(CE_NOTE, "zfsctl_snapshot_unmount: snapname=%s flags=%04x",
	    snapname, flags);

	rw_enter(&zfs_snapshot_lock, RW_READER);
	if ((se = zfsctl_snapshot_find_by_name(snapname)) == NULL) {
		cmn_err(CE_NOTE, "zfsctl_snapshot_unmount: snapname=%s: "
		    "not found", snapname);
		rw_exit(&zfs_snapshot_lock);
		return (SET_ERROR(ENOENT));
	}
	rw_exit(&zfs_snapshot_lock);

	mutex_enter(&se->se_mtx);
	zfs_snapentry_debug(se);

	zfs_snapentry_wait(se);
	zfs_snapentry_detach_idle(se);

	int err = (se->se_state == SE_MOUNTED) ? SET_ERROR(EBUSY) : 0;

	mutex_exit(&se->se_mtx);
	zfsctl_snapshot_rele(se);

	cmn_err(CE_NOTE, "zfsctl_snapshot_unmount: snapname=%s: return=%d",
	    snapname, err);

	return (err);
}

/* 6.18 compat: 4th arg removed; function will do strlen() internally. */
#ifdef HAVE_VFS_PARSE_FS_STRING_3ARGS
#define	zpl_vfs_parse_fs_string(fc, key, val)	\
	vfs_parse_fs_string((fc), (key), (val))
#else
#define	zpl_vfs_parse_fs_string(fc, key, val)	\
	vfs_parse_fs_string((fc), (key), (val), strlen(val))
#endif

int
zfsctl_snapshot_mount(struct path *path, int flags, struct vfsmount **mntp)
{
	struct dentry *dentry = path->dentry;
	struct inode *ip = dentry->d_inode;
	zfsvfs_t *zfsvfs;
	zfs_snapentry_t *se;
	char *snapname;
	struct fs_context *fc = NULL;
	int err;

	if (ip == NULL) {
		/*
		 * The kernel is walking a dentry tree and has reached an
		 * invalidated dentry that used to be ours, and still has our
		 * dentry ops set on it. Returning EISDIR is how we say "not
		 * found, and stop searching this path".
		 */
		return (SET_ERROR(EISDIR));
	}

	/* Hold the parent dataset, mostly to keep it alive while we work. */
	zfsvfs = ITOZSB(ip);
	if ((err = zfs_enter(zfsvfs, FTAG)) != 0)
		return (err);

	/*
	 * Compute the global name for the snapshot; this is our key for
	 * most snapentry ops.
	 */
	snapname = kmem_zalloc(ZFS_MAX_DATASET_NAME_LEN, KM_SLEEP);
	err = zfsctl_snapshot_name(zfsvfs, dname(dentry),
	    ZFS_MAX_DATASET_NAME_LEN, snapname);
	if (err != 0) {
		zfs_exit(zfsvfs, FTAG);
		goto out;
	}

	/*
	 * Release z_teardown_lock before potentially blocking operations
	 * (cv_wait for concurrent mounts, call_usermodehelper for the mount
	 * helper).  Holding z_teardown_lock(R) across call_usermodehelper
	 * deadlocks with namespace_sem: the mount helper needs
	 * namespace_sem(W) via move_mount, while /proc/self/mountinfo
	 * readers hold namespace_sem(R) and need z_teardown_lock(R) via
	 * zpl_show_devname.  A concurrent zfs_suspend_fs queuing
	 * z_teardown_lock(W) blocks new readers, completing the cycle.
	 * See https://github.com/openzfs/zfs/issues/18409
	 *
	 * Releasing the lock allows zfs_suspend_fs to proceed during
	 * the mount, so dmu_objset_hold in zpl_get_tree can transiently
	 * fail with ENOENT during the clone swap.  The mount helper
	 * fails, this function returns EISDIR, and the VFS silently
	 * falls back to the ctldir stub (empty directory).  The caller
	 * gets the stub inode instead of the real snapshot root until
	 * the next access retries the automount.
	 *
	 * Safe because everything below operates on local string copies
	 * (full_name, full_path) or uses its own synchronization
	 * (zfs_snapshot_lock, se_mtx).  The parent zfsvfs pointer
	 * remains valid because we hold a path reference to the
	 * automount trigger dentry.
	 */
	zfs_exit(zfsvfs, FTAG);

	/*
	 * There may be multiple concurrent callers through d_automount()
	 * until one succeeds and the mountpoint dentry is full established.
	 * Ideally we would be able to hold all but the first here until the
	 * mountpoint dentry is fully established, but all callers that have
	 * made it this far must return either an error or a new vfsmount
	 * with refcount=1, so in practice its easier to just let all mounts
	 * proceed and decide on a winner later.
	 */
retry_mount:
	fc = fs_context_for_submount(path->mnt->mnt_sb->s_type, dentry);
	if (IS_ERR(fc)) {
		err = -PTR_ERR(fc);
		goto out;
	}

	err = -zpl_vfs_parse_fs_string(fc, "source", snapname);
	if (err != 0) {
		put_fs_context(fc);
		goto out;
	}

	struct vfsmount *mnt = fc_mount(fc);
	put_fs_context(fc);

	if (IS_ERR(mnt)) {
		err = -PTR_ERR(mnt);
		if (err == EBUSY) {
			/*
			 * Most likely, the zfsvfs was in setup or teardown
			 * in zpl_get_tree() when we arrived. There might be
			 * a better way we can handle this over there, but
			 * regardless it should be resolved soon, so just
			 * retrying is fine.
			 */
			goto retry_mount;
		}
		goto out;
	}

	/*
	 * XXX ideally, we would do this here:
	 *
	 *     if (zfs_snapshot_no_setuid)
	 *         mnt->mnt_flags |= MNT_NOSUID;
	 *
	 *     the problem is that the return from d_automount gets mnt_flags
	 *     set to its parent's mnt_flags, so they get lost, and I've not
	 *     yet found another way to make it happen.
	 *
	 *     ideas:
	 *
	 *     sb->s_flags |= SB_NOSUID
	 *         no good, not considered for SUID checks
	 *     fc->s_flags, fc->sb_iflags, etc
	 *         not copied without using sget_fc, maybe more useful for
	 *         NOEXEC though, different mechanism
	 *     leave a note for d_revalidate, set it there
	 *         didn't work first time, but pretty awful as well
	 *     timer, set later
	 *         not tried, worse than d_revalidate
	 *
	 *     leaving it for the moment, until I get everything else going.
	 *     a better option might be to forcibly downgrade the flags? could
	 *     we force it into a different namespace? (ie the other conditions
	 *     for mnt_may_suid()). or maybe we set it later? or fake it via
	 *     statx? show_options? there's a lot of touch points but nothing
	 *     really nice.
	 *
	 *     incidentally, the option was added in d34d4f97a8 #16587 so
	 *     its possibly not so established that we couldn't get away with
	 *     something else.
	 *
	 *       -- robn, 2026-04-03
	 */

	/* Create new snapentry for this mount. */
	zfsvfs_t *snap_zfsvfs = ITOZSB(mnt->mnt_root->d_inode);
	snap_zfsvfs->z_parent = zfsvfs;

	se = zfsctl_snapshot_alloc(snapname,
	    snap_zfsvfs->z_os->os_spa, dmu_objset_id(snap_zfsvfs->z_os));
	zfs_snapentry_debug_act(se, "new");

	rw_enter(&zfs_snapshot_lock, RW_WRITER);
	zfs_snapentry_t *pse = zfsctl_snapshot_find_by_name(snapname);
	if (pse != NULL) {
		/*
		 * Snapentry already exists; just leave it there and return
		 * the new mount. The kernel will either graft it or discard
		 * it, nothing more for us to do.
		 */
		rw_exit(&zfs_snapshot_lock);
		zfsctl_snapshot_rele(pse);

		zfs_snapentry_debug_act(se, "abandon");
		zfsctl_snapshot_free(se);
		*mntp = mnt;
		goto out;
	}

	/* Publish in MOUNTING state */
	zfsctl_snapshot_add(se);
	rw_exit(&zfs_snapshot_lock);

	mutex_enter(&se->se_mtx);
	se->se_pmnt = path->mnt;
	se->se_dentry = dget(dentry);

	zfsctl_snapshot_expire_delay(se, zfs_expire_snapshot);

	zfs_snapentry_change_state(se, SE_MOUNTED);
	zfs_snapentry_debug(se);
	mutex_exit(&se->se_mtx);

	*mntp = mnt;

out:
	kmem_free(snapname, ZFS_MAX_DATASET_NAME_LEN);

	return (err);
}

/*
 * Get the snapdir inode from fid
 */
int
zfsctl_snapdir_vget(struct super_block *sb, uint64_t objsetid, int gen,
    struct inode **ipp)
{
	zfsvfs_t *zfsvfs = sb->s_fs_info;
	int error;
	struct path path;
	char *mnt;
	struct dentry *dentry;
	zfs_snapentry_t *se;

	mnt = kmem_alloc(MAXPATHLEN, KM_SLEEP);

#if 0
	/*
	 * Try the in-memory AVL tree first for previously mounted
	 * snapshots, falling back to the on-disk scan if not found.
	 */
	rw_enter(&zfs_snapshot_lock, RW_READER);
	se = zfsctl_snapshot_find_by_objsetid(zfsvfs->z_os->os_spa, objsetid);
	rw_exit(&zfs_snapshot_lock);
	if (se != NULL) {
		strlcpy(mnt, se->se_path, MAXPATHLEN);
		zfsctl_snapshot_rele(se);
#endif
	/*
	 * XXX hack to cope with se_path loss. this was added later, and might
	 *     mean that we have to restore it (ie drop this commit). I
	 *     just don't want to think about it right this second
	 *       -- robn, 2026-04-17
	 */
	if (0) {
	} else {
		(void) se;
		error = zfsctl_snapshot_path_objset(zfsvfs, objsetid,
		    MAXPATHLEN, mnt);
		if (error)
			goto out;
	}

	/* Trigger automount */
	error = -kern_path(mnt, LOOKUP_FOLLOW|LOOKUP_DIRECTORY, &path);
	if (error)
		goto out;

	path_put(&path);
	/*
	 * Get the snapdir inode. Note, we don't want to use the above
	 * path because it contains the root of the snapshot rather
	 * than the snapdir.
	 */
	*ipp = ilookup(sb, ZFSCTL_INO_SNAPDIRS - objsetid);
	if (*ipp == NULL) {
		error = SET_ERROR(ENOENT);
		goto out;
	}

	/* check gen, see zfsctl_snapdir_fid */
	dentry = d_obtain_alias(igrab(*ipp));
	if (gen != (!IS_ERR(dentry) && d_mountpoint(dentry))) {
		iput(*ipp);
		*ipp = NULL;
		error = SET_ERROR(ENOENT);
	}
	if (!IS_ERR(dentry))
		dput(dentry);
out:
	kmem_free(mnt, MAXPATHLEN);
	return (error);
}

int
zfsctl_shares_lookup(struct inode *dip, char *name, struct inode **ipp,
    int flags, cred_t *cr, int *direntflags, pathname_t *realpnp)
{
	zfsvfs_t *zfsvfs = ITOZSB(dip);
	znode_t *zp;
	znode_t *dzp;
	int error;

	if ((error = zfs_enter(zfsvfs, FTAG)) != 0)
		return (error);

	if (zfsvfs->z_shares_dir == 0) {
		zfs_exit(zfsvfs, FTAG);
		return (SET_ERROR(ENOTSUP));
	}

	if ((error = zfs_zget(zfsvfs, zfsvfs->z_shares_dir, &dzp)) == 0) {
		error = zfs_lookup(dzp, name, &zp, 0, cr, NULL, NULL);
		zrele(dzp);
	}

	zfs_exit(zfsvfs, FTAG);

	return (error);
}

#ifdef SNAPENTRY_DEBUG_DUMP
taskqid_t dumptask = TASKQID_INVALID;

static void
snapshot_dump_task(void *data)
{
	(void) data;

	zfsctl_snapshot_dump();

	dumptask = taskq_dispatch_delay(system_delay_taskq,
	    snapshot_dump_task, NULL, TQ_SLEEP, ddi_get_lbolt() + 5 * HZ);
}
#endif

/*
 * Initialize the various pieces we'll need to create and manipulate .zfs
 * directories.  Currently this is unused but available.
 */
void
zfsctl_init(void)
{
	avl_create(&zfs_snapshots_by_name, snapentry_compare_by_name,
	    sizeof (zfs_snapentry_t), offsetof(zfs_snapentry_t,
	    se_node_name));
	avl_create(&zfs_snapshots_by_objsetid, snapentry_compare_by_objsetid,
	    sizeof (zfs_snapentry_t), offsetof(zfs_snapentry_t,
	    se_node_objsetid));
	rw_init(&zfs_snapshot_lock, NULL, RW_DEFAULT, NULL);

#ifdef SNAPENTRY_DEBUG_DUMP
	dumptask = taskq_dispatch_delay(system_delay_taskq,
	    snapshot_dump_task, NULL, TQ_SLEEP, ddi_get_lbolt() + 5 * HZ);
#endif
}

/*
 * Cleanup the various pieces we needed for .zfs directories.  In particular
 * ensure the expiry timer is canceled safely.
 */
void
zfsctl_fini(void)
{
#ifdef SNAPENTRY_DEBUG_DUMP
	taskq_cancel_id(system_delay_taskq, dumptask, B_FALSE);
#endif

	avl_destroy(&zfs_snapshots_by_name);
	avl_destroy(&zfs_snapshots_by_objsetid);
	rw_destroy(&zfs_snapshot_lock);
}

module_param(zfs_admin_snapshot, int, 0644);
MODULE_PARM_DESC(zfs_admin_snapshot, "Enable mkdir/rmdir/mv in .zfs/snapshot");

module_param(zfs_expire_snapshot, int, 0644);
MODULE_PARM_DESC(zfs_expire_snapshot, "Seconds to expire .zfs/snapshot");

module_param(zfs_snapshot_no_setuid, int, 0644);
MODULE_PARM_DESC(zfs_snapshot_no_setuid,
	"Disable setuid/setgid for automounts in .zfs/snapshot");
