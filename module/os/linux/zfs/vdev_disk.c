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
 * Copyright (C) 2008-2010 Lawrence Livermore National Security, LLC.
 * Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 * Rewritten for Linux by Brian Behlendorf <behlendorf1@llnl.gov>.
 * LLNL-CODE-403049.
 * Copyright (c) 2012, 2019 by Delphix. All rights reserved.
 * Copyright (c) 2023, Klara Inc.
 */

#include <sys/zfs_context.h>
#include <sys/spa_impl.h>
#include <sys/vdev_disk.h>
#include <sys/vdev_impl.h>
#include <sys/vdev_trim.h>
#include <sys/abd.h>
#include <sys/fs/zfs.h>
#include <sys/zio.h>
#include <linux/blkpg.h>
#include <linux/msdos_fs.h>
#include <linux/vfs_compat.h>
#include <linux/mm_compat.h>
#ifdef HAVE_LINUX_BLK_CGROUP_HEADER
#include <linux/blk-cgroup.h>
#endif

typedef struct vdev_disk {
	struct block_device		*vd_bdev;
	krwlock_t			vd_lock;
} vdev_disk_t;

/*
 * Maximum number of segments to add to a bio (min 4). If this is higher than
 * the maximum allowed by the device queue or the kernel itself, it will be
 * clamped. Setting it to zero will cause the kernel's ideal size to be used.
 */
uint_t vdev_disk_max_segs = 0;

/*
 * Unique identifier for the exclusive vdev holder.
 */
static void *zfs_vdev_holder = VDEV_HOLDER;

/*
 * Wait up to zfs_vdev_open_timeout_ms milliseconds before determining the
 * device is missing. The missing path may be transient since the links
 * can be briefly removed and recreated in response to udev events.
 */
static uint_t zfs_vdev_open_timeout_ms = 1000;

/*
 * Size of the "reserved" partition, in blocks.
 */
#define	EFI_MIN_RESV_SIZE	(16 * 1024)



/*
 * BIO request failfast mask.
 */

static unsigned int zfs_vdev_failfast_mask = 1;

#ifdef HAVE_BLK_MODE_T
static blk_mode_t
#else
static fmode_t
#endif
vdev_bdev_mode(spa_mode_t spa_mode)
{
#ifdef HAVE_BLK_MODE_T
	blk_mode_t mode = 0;

	if (spa_mode & SPA_MODE_READ)
		mode |= BLK_OPEN_READ;

	if (spa_mode & SPA_MODE_WRITE)
		mode |= BLK_OPEN_WRITE;
#else
	fmode_t mode = 0;

	if (spa_mode & SPA_MODE_READ)
		mode |= FMODE_READ;

	if (spa_mode & SPA_MODE_WRITE)
		mode |= FMODE_WRITE;
#endif

	return (mode);
}

/*
 * Returns the usable capacity (in bytes) for the partition or disk.
 */
static uint64_t
bdev_capacity(struct block_device *bdev)
{
	return (i_size_read(bdev->bd_inode));
}

#if !defined(HAVE_BDEV_WHOLE)
static inline struct block_device *
bdev_whole(struct block_device *bdev)
{
	return (bdev->bd_contains);
}
#endif

#if defined(HAVE_BDEVNAME)
#define	vdev_bdevname(bdev, name)	bdevname(bdev, name)
#else
static inline void
vdev_bdevname(struct block_device *bdev, char *name)
{
	snprintf(name, BDEVNAME_SIZE, "%pg", bdev);
}
#endif

/*
 * Returns the maximum expansion capacity of the block device (in bytes).
 *
 * It is possible to expand a vdev when it has been created as a wholedisk
 * and the containing block device has increased in capacity.  Or when the
 * partition containing the pool has been manually increased in size.
 *
 * This function is only responsible for calculating the potential expansion
 * size so it can be reported by 'zpool list'.  The efi_use_whole_disk() is
 * responsible for verifying the expected partition layout in the wholedisk
 * case, and updating the partition table if appropriate.  Once the partition
 * size has been increased the additional capacity will be visible using
 * bdev_capacity().
 *
 * The returned maximum expansion capacity is always expected to be larger, or
 * at the very least equal, to its usable capacity to prevent overestimating
 * the pool expandsize.
 */
static uint64_t
bdev_max_capacity(struct block_device *bdev, uint64_t wholedisk)
{
	uint64_t psize;
	int64_t available;

	if (wholedisk && bdev != bdev_whole(bdev)) {
		/*
		 * When reporting maximum expansion capacity for a wholedisk
		 * deduct any capacity which is expected to be lost due to
		 * alignment restrictions.  Over reporting this value isn't
		 * harmful and would only result in slightly less capacity
		 * than expected post expansion.
		 * The estimated available space may be slightly smaller than
		 * bdev_capacity() for devices where the number of sectors is
		 * not a multiple of the alignment size and the partition layout
		 * is keeping less than PARTITION_END_ALIGNMENT bytes after the
		 * "reserved" EFI partition: in such cases return the device
		 * usable capacity.
		 */
		available = i_size_read(bdev_whole(bdev)->bd_inode) -
		    ((EFI_MIN_RESV_SIZE + NEW_START_BLOCK +
		    PARTITION_END_ALIGNMENT) << SECTOR_BITS);
		psize = MAX(available, bdev_capacity(bdev));
	} else {
		psize = bdev_capacity(bdev);
	}

	return (psize);
}

static void
vdev_disk_error(zio_t *zio)
{
	/*
	 * This function can be called in interrupt context, for instance while
	 * handling IRQs coming from a misbehaving disk device; use printk()
	 * which is safe from any context.
	 */
	printk(KERN_WARNING "zio pool=%s vdev=%s error=%d type=%d "
	    "offset=%llu size=%llu flags=%llu\n", spa_name(zio->io_spa),
	    zio->io_vd->vdev_path, zio->io_error, zio->io_type,
	    (u_longlong_t)zio->io_offset, (u_longlong_t)zio->io_size,
	    zio->io_flags);
}

static void
vdev_disk_kobj_evt_post(vdev_t *v)
{
	vdev_disk_t *vd = v->vdev_tsd;
	if (vd && vd->vd_bdev) {
		spl_signal_kobj_evt(vd->vd_bdev);
	} else {
		vdev_dbgmsg(v, "vdev_disk_t is NULL for VDEV:%s\n",
		    v->vdev_path);
	}
}

#if !defined(HAVE_BLKDEV_GET_BY_PATH_4ARG)
/*
 * Define a dummy struct blk_holder_ops for kernel versions
 * prior to 6.5.
 */
struct blk_holder_ops {};
#endif

static struct block_device *
vdev_blkdev_get_by_path(const char *path, spa_mode_t mode, void *holder,
    const struct blk_holder_ops *hops)
{
#ifdef HAVE_BLKDEV_GET_BY_PATH_4ARG
	return (blkdev_get_by_path(path,
	    vdev_bdev_mode(mode) | BLK_OPEN_EXCL, holder, hops));
#else
	return (blkdev_get_by_path(path,
	    vdev_bdev_mode(mode) | FMODE_EXCL, holder));
#endif
}

static void
vdev_blkdev_put(struct block_device *bdev, spa_mode_t mode, void *holder)
{
#ifdef HAVE_BLKDEV_PUT_HOLDER
	return (blkdev_put(bdev, holder));
#else
	return (blkdev_put(bdev, vdev_bdev_mode(mode) | FMODE_EXCL));
#endif
}

static int
vdev_disk_open(vdev_t *v, uint64_t *psize, uint64_t *max_psize,
    uint64_t *logical_ashift, uint64_t *physical_ashift)
{
	struct block_device *bdev;
#ifdef HAVE_BLK_MODE_T
	blk_mode_t mode = vdev_bdev_mode(spa_mode(v->vdev_spa));
#else
	fmode_t mode = vdev_bdev_mode(spa_mode(v->vdev_spa));
#endif
	hrtime_t timeout = MSEC2NSEC(zfs_vdev_open_timeout_ms);
	vdev_disk_t *vd;

	/* Must have a pathname and it must be absolute. */
	if (v->vdev_path == NULL || v->vdev_path[0] != '/') {
		v->vdev_stat.vs_aux = VDEV_AUX_BAD_LABEL;
		vdev_dbgmsg(v, "invalid vdev_path");
		return (SET_ERROR(EINVAL));
	}

	/*
	 * Reopen the device if it is currently open.  When expanding a
	 * partition force re-scanning the partition table if userland
	 * did not take care of this already. We need to do this while closed
	 * in order to get an accurate updated block device size.  Then
	 * since udev may need to recreate the device links increase the
	 * open retry timeout before reporting the device as unavailable.
	 */
	vd = v->vdev_tsd;
	if (vd) {
		char disk_name[BDEVNAME_SIZE + 6] = "/dev/";
		boolean_t reread_part = B_FALSE;

		rw_enter(&vd->vd_lock, RW_WRITER);
		bdev = vd->vd_bdev;
		vd->vd_bdev = NULL;

		if (bdev) {
			if (v->vdev_expanding && bdev != bdev_whole(bdev)) {
				vdev_bdevname(bdev_whole(bdev), disk_name + 5);
				/*
				 * If userland has BLKPG_RESIZE_PARTITION,
				 * then it should have updated the partition
				 * table already. We can detect this by
				 * comparing our current physical size
				 * with that of the device. If they are
				 * the same, then we must not have
				 * BLKPG_RESIZE_PARTITION or it failed to
				 * update the partition table online. We
				 * fallback to rescanning the partition
				 * table from the kernel below. However,
				 * if the capacity already reflects the
				 * updated partition, then we skip
				 * rescanning the partition table here.
				 */
				if (v->vdev_psize == bdev_capacity(bdev))
					reread_part = B_TRUE;
			}

			vdev_blkdev_put(bdev, mode, zfs_vdev_holder);
		}

		if (reread_part) {
			bdev = vdev_blkdev_get_by_path(disk_name, mode,
			    zfs_vdev_holder, NULL);
			if (!IS_ERR(bdev)) {
				int error = vdev_bdev_reread_part(bdev);
				vdev_blkdev_put(bdev, mode, zfs_vdev_holder);
				if (error == 0) {
					timeout = MSEC2NSEC(
					    zfs_vdev_open_timeout_ms * 2);
				}
			}
		}
	} else {
		vd = kmem_zalloc(sizeof (vdev_disk_t), KM_SLEEP);

		rw_init(&vd->vd_lock, NULL, RW_DEFAULT, NULL);
		rw_enter(&vd->vd_lock, RW_WRITER);
	}

	/*
	 * Devices are always opened by the path provided at configuration
	 * time.  This means that if the provided path is a udev by-id path
	 * then drives may be re-cabled without an issue.  If the provided
	 * path is a udev by-path path, then the physical location information
	 * will be preserved.  This can be critical for more complicated
	 * configurations where drives are located in specific physical
	 * locations to maximize the systems tolerance to component failure.
	 *
	 * Alternatively, you can provide your own udev rule to flexibly map
	 * the drives as you see fit.  It is not advised that you use the
	 * /dev/[hd]d devices which may be reordered due to probing order.
	 * Devices in the wrong locations will be detected by the higher
	 * level vdev validation.
	 *
	 * The specified paths may be briefly removed and recreated in
	 * response to udev events.  This should be exceptionally unlikely
	 * because the zpool command makes every effort to verify these paths
	 * have already settled prior to reaching this point.  Therefore,
	 * a ENOENT failure at this point is highly likely to be transient
	 * and it is reasonable to sleep and retry before giving up.  In
	 * practice delays have been observed to be on the order of 100ms.
	 *
	 * When ERESTARTSYS is returned it indicates the block device is
	 * a zvol which could not be opened due to the deadlock detection
	 * logic in zvol_open().  Extend the timeout and retry the open
	 * subsequent attempts are expected to eventually succeed.
	 */
	hrtime_t start = gethrtime();
	bdev = ERR_PTR(-ENXIO);
	while (IS_ERR(bdev) && ((gethrtime() - start) < timeout)) {
		bdev = vdev_blkdev_get_by_path(v->vdev_path, mode,
		    zfs_vdev_holder, NULL);
		if (unlikely(PTR_ERR(bdev) == -ENOENT)) {
			/*
			 * There is no point of waiting since device is removed
			 * explicitly
			 */
			if (v->vdev_removed)
				break;

			schedule_timeout(MSEC_TO_TICK(10));
		} else if (unlikely(PTR_ERR(bdev) == -ERESTARTSYS)) {
			timeout = MSEC2NSEC(zfs_vdev_open_timeout_ms * 10);
			continue;
		} else if (IS_ERR(bdev)) {
			break;
		}
	}

	if (IS_ERR(bdev)) {
		int error = -PTR_ERR(bdev);
		vdev_dbgmsg(v, "open error=%d timeout=%llu/%llu", error,
		    (u_longlong_t)(gethrtime() - start),
		    (u_longlong_t)timeout);
		vd->vd_bdev = NULL;
		v->vdev_tsd = vd;
		rw_exit(&vd->vd_lock);
		return (SET_ERROR(error));
	} else {
		vd->vd_bdev = bdev;
		v->vdev_tsd = vd;
		rw_exit(&vd->vd_lock);
	}

	/*  Determine the physical block size */
	int physical_block_size = bdev_physical_block_size(vd->vd_bdev);

	/*  Determine the logical block size */
	int logical_block_size = bdev_logical_block_size(vd->vd_bdev);

	/* Clear the nowritecache bit, causes vdev_reopen() to try again. */
	v->vdev_nowritecache = B_FALSE;

	/* Set when device reports it supports TRIM. */
	v->vdev_has_trim = bdev_discard_supported(vd->vd_bdev);

	/* Set when device reports it supports secure TRIM. */
	v->vdev_has_securetrim = bdev_secure_discard_supported(vd->vd_bdev);

	/* Inform the ZIO pipeline that we are non-rotational */
	v->vdev_nonrot = blk_queue_nonrot(bdev_get_queue(vd->vd_bdev));

	/* Physical volume size in bytes for the partition */
	*psize = bdev_capacity(vd->vd_bdev);

	/* Physical volume size in bytes including possible expansion space */
	*max_psize = bdev_max_capacity(vd->vd_bdev, v->vdev_wholedisk);

	/* Based on the minimum sector size set the block size */
	*physical_ashift = highbit64(MAX(physical_block_size,
	    SPA_MINBLOCKSIZE)) - 1;

	*logical_ashift = highbit64(MAX(logical_block_size,
	    SPA_MINBLOCKSIZE)) - 1;

	return (0);
}

static void
vdev_disk_close(vdev_t *v)
{
	vdev_disk_t *vd = v->vdev_tsd;

	if (v->vdev_reopening || vd == NULL)
		return;

	if (vd->vd_bdev != NULL) {
		vdev_blkdev_put(vd->vd_bdev, spa_mode(v->vdev_spa),
		    zfs_vdev_holder);
	}

	rw_destroy(&vd->vd_lock);
	kmem_free(vd, sizeof (vdev_disk_t));
	v->vdev_tsd = NULL;
}

static inline void
vdev_submit_bio_impl(struct bio *bio)
{
#ifdef HAVE_1ARG_SUBMIT_BIO
	(void) submit_bio(bio);
#else
	(void) submit_bio(bio_data_dir(bio), bio);
#endif
}

/*
 * preempt_schedule_notrace is GPL-only which breaks the ZFS build, so
 * replace it with preempt_schedule under the following condition:
 */
#if defined(CONFIG_ARM64) && \
    defined(CONFIG_PREEMPTION) && \
    defined(CONFIG_BLK_CGROUP)
#define	preempt_schedule_notrace(x) preempt_schedule(x)
#endif

/*
 * As for the Linux 5.18 kernel bio_alloc() expects a block_device struct
 * as an argument removing the need to set it with bio_set_dev().  This
 * removes the need for all of the following compatibility code.
 */
#if !defined(HAVE_BIO_ALLOC_4ARG)

#ifdef HAVE_BIO_SET_DEV
#if defined(CONFIG_BLK_CGROUP) && defined(HAVE_BIO_SET_DEV_GPL_ONLY)
/*
 * The Linux 5.5 kernel updated percpu_ref_tryget() which is inlined by
 * blkg_tryget() to use rcu_read_lock() instead of rcu_read_lock_sched().
 * As a side effect the function was converted to GPL-only.  Define our
 * own version when needed which uses rcu_read_lock_sched().
 *
 * The Linux 5.17 kernel split linux/blk-cgroup.h into a private and a public
 * part, moving blkg_tryget into the private one. Define our own version.
 */
#if defined(HAVE_BLKG_TRYGET_GPL_ONLY) || !defined(HAVE_BLKG_TRYGET)
static inline bool
vdev_blkg_tryget(struct blkcg_gq *blkg)
{
	struct percpu_ref *ref = &blkg->refcnt;
	unsigned long __percpu *count;
	bool rc;

	rcu_read_lock_sched();

	if (__ref_is_percpu(ref, &count)) {
		this_cpu_inc(*count);
		rc = true;
	} else {
#ifdef ZFS_PERCPU_REF_COUNT_IN_DATA
		rc = atomic_long_inc_not_zero(&ref->data->count);
#else
		rc = atomic_long_inc_not_zero(&ref->count);
#endif
	}

	rcu_read_unlock_sched();

	return (rc);
}
#else
#define	vdev_blkg_tryget(bg)	blkg_tryget(bg)
#endif
#ifdef HAVE_BIO_SET_DEV_MACRO
/*
 * The Linux 5.0 kernel updated the bio_set_dev() macro so it calls the
 * GPL-only bio_associate_blkg() symbol thus inadvertently converting
 * the entire macro.  Provide a minimal version which always assigns the
 * request queue's root_blkg to the bio.
 */
static inline void
vdev_bio_associate_blkg(struct bio *bio)
{
#if defined(HAVE_BIO_BDEV_DISK)
	struct request_queue *q = bio->bi_bdev->bd_disk->queue;
#else
	struct request_queue *q = bio->bi_disk->queue;
#endif

	ASSERT3P(q, !=, NULL);
	ASSERT3P(bio->bi_blkg, ==, NULL);

	if (q->root_blkg && vdev_blkg_tryget(q->root_blkg))
		bio->bi_blkg = q->root_blkg;
}

#define	bio_associate_blkg vdev_bio_associate_blkg
#else
static inline void
vdev_bio_set_dev(struct bio *bio, struct block_device *bdev)
{
#if defined(HAVE_BIO_BDEV_DISK)
	struct request_queue *q = bdev->bd_disk->queue;
#else
	struct request_queue *q = bio->bi_disk->queue;
#endif
	bio_clear_flag(bio, BIO_REMAPPED);
	if (bio->bi_bdev != bdev)
		bio_clear_flag(bio, BIO_THROTTLED);
	bio->bi_bdev = bdev;

	ASSERT3P(q, !=, NULL);
	ASSERT3P(bio->bi_blkg, ==, NULL);

	if (q->root_blkg && vdev_blkg_tryget(q->root_blkg))
		bio->bi_blkg = q->root_blkg;
}
#define	bio_set_dev		vdev_bio_set_dev
#endif
#endif
#else
/*
 * Provide a bio_set_dev() helper macro for pre-Linux 4.14 kernels.
 */
static inline void
bio_set_dev(struct bio *bio, struct block_device *bdev)
{
	bio->bi_bdev = bdev;
}
#endif /* HAVE_BIO_SET_DEV */
#endif /* !HAVE_BIO_ALLOC_4ARG */

static inline void
vdev_submit_bio(struct bio *bio)
{
	struct bio_list *bio_list = current->bio_list;
	current->bio_list = NULL;
	vdev_submit_bio_impl(bio);
	current->bio_list = bio_list;
}

static inline struct bio *
vdev_bio_alloc(struct block_device *bdev, gfp_t gfp_mask,
    unsigned short nr_vecs)
{
	struct bio *bio;

#ifdef HAVE_BIO_ALLOC_4ARG
	bio = bio_alloc(bdev, nr_vecs, 0, gfp_mask);
#else
	bio = bio_alloc(gfp_mask, nr_vecs);
	if (likely(bio != NULL))
		bio_set_dev(bio, bdev);
#endif

	return (bio);
}

static inline uint_t
vdev_bio_max_segs(struct block_device *bdev)
{
	/*
	 * Smallest of the device max segs and the tuneable max segs. Minimum
	 * 4, so there's room to finish split pages if they come up.
	 */
	const uint_t dev_max_segs = queue_max_segments(bdev_get_queue(bdev));
	const uint_t tune_max_segs = (vdev_disk_max_segs > 0) ?
	    MAX(4, vdev_disk_max_segs) : dev_max_segs;
	const uint_t max_segs = MIN(tune_max_segs, dev_max_segs);

#ifdef HAVE_BIO_MAX_SEGS
	return (bio_max_segs(max_segs));
#else
	return (MIN(max_segs, BIO_MAX_PAGES));
#endif
}


/*
 * Virtual block IO object (VBIO)
 *
 * Linux block IO (BIO) objects have a limit on how many data segments (pages)
 * they can hold. Depending on how they're allocated and structured, a large
 * ZIO can require more than one BIO to be submitted to the kernel, which then
 * all have to complete before we can return the completed ZIO back to ZFS.
 *
 * A VBIO is a wrapper around multiple BIOs, carrying everything needed to
 * translate a ZIO down into the kernel block layer and back again.
 *
 * Note that these are only used for data ZIOs (read/write). Meta-operations
 * (flush/trim) don't need multiple BIOs and so can just make the call
 * directly.
 */
typedef struct {
	zio_t		*vbio_zio;	/* parent zio */

	struct block_device *vbio_bdev;	/* blockdev to submit bios to */

	abd_t		*vbio_abd;	/* abd carrying borrowed linear buf */

	atomic_t	vbio_ref;	/* bio refcount */
	int		vbio_error;	/* error from failed bio */

	uint_t		vbio_max_segs;	/* max segs per bio */
	uint_t		vbio_max_bios;	/* max bios (size of vbio_bio) */

	uint_t		vbio_npages;	/* pages remaining */

	uint64_t	vbio_offset;	/* start offset of next bio */

	uint_t		vbio_nbios;	/* allocated bios */
	uint_t		vbio_cur;	/* current bio */
	struct bio	*vbio_bio[];	/* attached bios */
} vbio_t;

static vbio_t *
vbio_alloc(zio_t *zio, struct block_device *bdev, uint_t pages)
{
	/* Max segments we need in each BIO to take all pages */
	uint_t max_segs = MIN(pages, vdev_bio_max_segs(bdev));

	/* Number of BIOs we need, at max_segs segments each */
	int max_bios = (pages + max_segs - 1) / max_segs;

	vbio_t *vbio = kmem_zalloc(sizeof (vbio_t) +
	    sizeof (struct bio *) * max_bios, KM_SLEEP);

	vbio->vbio_zio = zio;
	vbio->vbio_bdev = bdev;
	atomic_set(&vbio->vbio_ref, 0);
	vbio->vbio_max_segs = max_segs;
	vbio->vbio_max_bios = max_bios;
	vbio->vbio_npages = pages;
	vbio->vbio_cur = 0;
	vbio->vbio_offset = zio->io_offset;

	return (vbio);
}

static int
vbio_add_page(vbio_t *vbio, struct page *page, uint_t size, uint_t offset)
{
	struct bio *bio;

	/*
	 * Weird housekeeping error; shouldn't happen, but try and save the
	 * furniture if it happens in production.
	 */
	ASSERT3U(vbio->vbio_npages, >, 0);
	if (vbio->vbio_npages == 0)
		return (SET_ERROR(ENOMEM));

	for (;;) {
		bio = vbio->vbio_bio[vbio->vbio_cur];
		if (bio == NULL) {
			/* New BIO, allocate and set up */
			bio = vdev_bio_alloc(vbio->vbio_bdev, GFP_NOIO,
			    MIN(vbio->vbio_npages, vbio->vbio_max_segs));
			if (unlikely(bio == NULL))
				return (SET_ERROR(ENOMEM));
			BIO_BI_SECTOR(bio) = vbio->vbio_offset >> 9;
			vbio->vbio_bio[vbio->vbio_cur] = bio;
			vbio->vbio_nbios++;
		}

		if (bio_add_page(bio, page, size, offset) == size) {
			vbio->vbio_npages--;
			return (0);
		}

		/* No room, set up for a new BIO and loop */
		vbio->vbio_offset += BIO_BI_SIZE(bio);
		vbio->vbio_cur++;

		VERIFY3U(vbio->vbio_cur, <, vbio->vbio_max_bios);
	}
}

BIO_END_IO_PROTO(vdev_disk_io_rw_completion, bio, error);
static void vbio_put(vbio_t *vbio);

static void
vbio_submit(vbio_t *vbio, int flags)
{
	struct blk_plug plug;

	/*
	 * Take a reference for each BIO we're about to submit, plus one to
	 * protect us from BIOs completing before we're done submitting them
	 * all, causing vbio_put() to free vbio out from under us and/or the
	 * zio to be returned before all its IO has completed.
	 */
	atomic_set(&vbio->vbio_ref, vbio->vbio_nbios + 1);

	/*
	 * If we're submitting more than one BIO, inform the block layer so
	 * it can batch them if it wants.
	 */
	if (vbio->vbio_nbios > 1)
		blk_start_plug(&plug);

	/* Submit all the BIOs */
	for (int i = 0; i < vbio->vbio_nbios; i++) {
		struct bio *bio = vbio->vbio_bio[i];
		ASSERT3P(bio, !=, NULL);

		bio->bi_end_io = vdev_disk_io_rw_completion;
		bio->bi_private = vbio;
		bio_set_op_attrs(bio,
		    vbio->vbio_zio->io_type == ZIO_TYPE_WRITE ?
		    WRITE : READ, flags);
		vdev_submit_bio(bio);
	}

	/* Finish the batch */
	if (vbio->vbio_nbios > 1)
		blk_finish_plug(&plug);

	/* Release the extra reference */
	vbio_put(vbio);
}

static void
vbio_free(vbio_t *vbio)
{
	VERIFY0(atomic_read(&vbio->vbio_ref));

	for (int i = 0; i < vbio->vbio_nbios; i++)
		bio_put(vbio->vbio_bio[i]);

	zio_t *zio = vbio->vbio_zio;
	if (vbio->vbio_abd != NULL && vbio->vbio_abd != zio->io_abd) {
		/*
		 * We copied the ABD before issuing it, so return the copy to
		 * the ABD, with changes if appropriate.
		 */
		void *buf = abd_to_buf(vbio->vbio_abd);
		abd_free(vbio->vbio_abd);
		vbio->vbio_abd = NULL;

		if (zio->io_type == ZIO_TYPE_READ)
			abd_return_buf_copy(zio->io_abd, buf, zio->io_size);
		else
			abd_return_buf(zio->io_abd, buf, zio->io_size);
	}

	kmem_free(vbio, sizeof (vbio_t) +
	    sizeof (struct bio *) * vbio->vbio_max_bios);
}

static void
vbio_put(vbio_t *vbio)
{
	if (atomic_dec_return(&vbio->vbio_ref) > 0)
		return;

	/*
	 * This was the last reference, so the entire IO is completed. Clean
	 * up and submit it for processing.
	 */
	zio_t *zio = vbio->vbio_zio;

	/*
	 * Set the overall error. If multiple BIOs returned an error, only the
	 * first will be taken; the others are dropped (see
	 * vdev_disk_io_rw_completion()). Its pretty much impossible for
	 * multiple IOs to the same device to fail with different errors, so
	 * there's no real risk.
	 */
	zio->io_error = vbio->vbio_error;

	/* Return any data buf back to the original ABD, and clean up */
	vbio_free(vbio);

	/* Do any additional error reporting */
	if (zio->io_error)
		vdev_disk_error(zio);

	/* All done, submit for processing */
	zio_delay_interrupt(zio);
}

BIO_END_IO_PROTO(vdev_disk_io_rw_completion, bio, error)
{
	vbio_t *vbio = bio->bi_private;

	if (vbio->vbio_error == 0) {
#ifdef HAVE_1ARG_BIO_END_IO_T
		vbio->vbio_error = BIO_END_IO_ERROR(bio);
#else
		if (error)
			vbio->vbio_error = -(error);
		else if (!test_bit(BIO_UPTODATE, &bio->bi_flags))
			vbio->vbio_error = EIO;
#endif
	}

	/* Drop this BIOs reference acquired by vbio_submit() */
	vbio_put(vbio);
}

static inline void
_buf_to_page_and_offset(const void *buf, struct page **pagep, uint_t *offp)
{
	struct page *page = is_vmalloc_addr(buf) ?
	    vmalloc_to_page(buf) : virt_to_page(buf);

	if (!PageCompound(page)) {
		/* Single boring page, nothing more to see */
		*pagep = page;
		*offp = offset_in_page(buf);
		return;
	}

	/*
	 * This page is part of a "compound page", which is a group of pages
	 * that can be referenced from a single struct page *. Its organised as
	 * a "head" page, followed by a serious of "tail" pages.
	 *
	 * In OpenZFS, compound pages are allocated using the __GFP_COMP flag,
	 * which we get from scatter ABDs and SPL vmalloc slabs (ie >16K
	 * allocations). So a great many of the IO buffers we get are going to
	 * be of this type.
	 *
	 * The tail pages are just regular PAGE_SIZE pages, and we can just
	 * load them into the BIO the same as we would for non-compound pages
	 * above, and it all works just fine. However, the head page has length
	 * covering itself and all the tail pages. If our buffer spans multiple
	 * pages, then we can load the head page and a >PAGE_SIZE length into
	 * the BIO, which is far more efficient.
	 *
	 * To do this, we need to calculate the offset of the buffer from the
	 * head page (offset_in_page() is the offset within its PAGE_SIZE'd
	 * page ie just a simple ~(PAGE_SIZE-1) mask).
	 */

	*pagep = compound_head(page);
	*offp = (uint_t)((uintptr_t)(buf) & (page_size(*pagep)-1));
}

/*
 * Iterator callback to count ABD pages and check their size & alignment.
 *
 * On Linux, each BIO segment can take a page pointer, and an offset+length of
 * the data within that page. A page can be arbitrarily large ("compound"
 * pages) but we still have to ensure the data portion is correctly sized and
 * aligned to the logical block size, to ensure that if the kernel wants to
 * split the BIO, the two halves will still be properly aligned.
 */
typedef struct {
	uint_t	bmask;
	uint_t	npages;
	uint_t	end;
} vdev_disk_check_pages_t;

static int
vdev_disk_check_pages_cb(void *buf, size_t len, void *priv)
{
	vdev_disk_check_pages_t *s = priv;

	struct page *page;
	uint_t off;

	/*
	 * If we didn't finish on a block size boundary last time, then there
	 * would be a gap if we tried to use this ABD as-is, so abort.
	 */
	if (s->end != 0)
		return (1);

	/*
	 * Note if we're taking less than a full block, so we can check it
	 * above on the next call.
	 */
	s->end = len & s->bmask;

	while (len > 0) {
		_buf_to_page_and_offset(buf, &page, &off);

		/*
		 * All blocks after the first must start on a block size
		 * boundary.
		 */
		if (s->npages != 0 && (off & s->bmask) != 0)
			return (1);

		uint_t take = MIN(len, page_size(page)-off);

		buf += take;
		len -= take;

		s->npages++;
	}

	return (0);
}

/*
 * Check if we can submit the pages in this ABD to the kernel as-is. Returns
 * the number of pages, or 0 if it can't be submitted like this.
 */
static uint_t
vdev_disk_check_pages(abd_t *abd, uint64_t size, uint_t lbs)
{
	vdev_disk_check_pages_t s = {
	    .bmask = lbs-1,
	    .npages = 0,
	    .end = 0
	};

	if (abd_iterate_func(abd, 0, size, vdev_disk_check_pages_cb, &s))
		return (0);

	return (s.npages);
}

/* Iterator callback to submit ABD pages to the vbio. */
static int
vdev_disk_fill_vbio_cb(void *buf, size_t len, void *priv)
{
	vbio_t *vbio = priv;
	int err;

	struct page *page;
	uint_t off;

	while (len > 0) {
		_buf_to_page_and_offset(buf, &page, &off);

		uint_t take = MIN(len, page_size(page)-off);

		err = vbio_add_page(vbio, page, take, off);
		if (err != 0)
			return (err);

		buf += take;
		len -= take;
	}

	return (0);
}

static int
vdev_disk_io_rw(zio_t *zio)
{
	vdev_t *v = zio->io_vd;
	vdev_disk_t *vd = v->vdev_tsd;
	struct block_device *bdev = vd->vd_bdev;
	int flags = 0;

	/*
	 * Accessing outside the block device is never allowed.
	 */
	if (zio->io_offset + zio->io_size > bdev->bd_inode->i_size) {
		vdev_dbgmsg(zio->io_vd,
		    "Illegal access %llu size %llu, device size %llu",
		    (u_longlong_t)io_offset,
		    (u_longlong_t)io_size,
		    (u_longlong_t)i_size_read(bdev->bd_inode));
		return (SET_ERROR(EIO));
	}

	if (!(zio->io_flags & (ZIO_FLAG_IO_RETRY | ZIO_FLAG_TRYHARD)) &&
	    v->vdev_failfast == B_TRUE) {
		bio_set_flags_failfast(bdev, &flags, zfs_vdev_failfast_mask & 1,
		    zfs_vdev_failfast_mask & 2, zfs_vdev_failfast_mask & 4);
	}

	/*
	 * Count the number of pages in the incoming ABD, and check its
	 * alignment. If any part of it would require submitting a page that is
	 * not aligned to the logical block size, then we take a copy into a
	 * linear buffer and submit that instead. This should be impossible on
	 * a 512b LBS, and fairly rare on 4K, usually requiring
	 * abnormally-small data blocks (eg gang blocks) mixed into the same
	 * ABD as larger ones (eg aggregated).
	 */

	/* LBS is guaranteed to be one of 512, 1024, 2048 or 4096 */
	uint_t lbs = bdev_logical_block_size(bdev);

	abd_t *abd = zio->io_abd;
	uint_t npages = vdev_disk_check_pages(abd, zio->io_size, lbs);
	if (npages == 0) {
		void *buf;
		/* We can't safely use pages from this ABD, so use a copy */
		if (zio->io_type == ZIO_TYPE_READ)
			buf = abd_borrow_buf(zio->io_abd, zio->io_size);
		else
			buf = abd_borrow_buf_copy(zio->io_abd, zio->io_size);

		/*
		 * New allocation can still span multiple pages, so we need to
		 * count it properly. We wrap it in an abd_t so we can just use
		 * the iterators to count and to fill the vbio later.
		 */
		abd = abd_get_from_buf(buf, zio->io_size);
		npages = vdev_disk_check_pages(abd, zio->io_size, lbs);

		/*
		 * Zero here would mean the borrowed copy has an invalid
		 * alignment too, which would mean we've somehow been passed a
		 * linear ABD with an interior page that has a non-zero offset
		 * or a size not a multiple of PAGE_SIZE. This is not possible.
		 * It would mean either zio_buf_alloc() or its underlying
		 * allocators have done something extremely strange, or our
		 * math in vdev_disk_check_pages() is wrong. In either case,
		 * something in seriously wrong and its not safe to continue.
		 */
		VERIFY(npages);
	}

	/* Allocate vbio, with a pointer to the borrowed ABD if necessary */
	int error = 0;
	vbio_t *vbio = vbio_alloc(zio, bdev, npages);
	if (abd != zio->io_abd)
		vbio->vbio_abd = abd;

	/* Fill it with pages */
	error = abd_iterate_func(abd, 0, zio->io_size,
	    vdev_disk_fill_vbio_cb, vbio);
	if (error != 0) {
		vbio_free(vbio);
		return (error);
	}

	vbio_submit(vbio, flags);
	return (error);
}

BIO_END_IO_PROTO(vdev_disk_io_flush_completion, bio, error)
{
	zio_t *zio = bio->bi_private;
#ifdef HAVE_1ARG_BIO_END_IO_T
	zio->io_error = BIO_END_IO_ERROR(bio);
#else
	zio->io_error = -error;
#endif

	if (zio->io_error && (zio->io_error == EOPNOTSUPP))
		zio->io_vd->vdev_nowritecache = B_TRUE;

	bio_put(bio);
	ASSERT3S(zio->io_error, >=, 0);
	if (zio->io_error)
		vdev_disk_error(zio);
	zio_interrupt(zio);
}

static int
vdev_disk_io_flush(struct block_device *bdev, zio_t *zio)
{
	struct request_queue *q;
	struct bio *bio;

	q = bdev_get_queue(bdev);
	if (!q)
		return (SET_ERROR(ENXIO));

	bio = vdev_bio_alloc(bdev, GFP_NOIO, 0);
	if (unlikely(bio == NULL))
		return (SET_ERROR(ENOMEM));

	bio->bi_end_io = vdev_disk_io_flush_completion;
	bio->bi_private = zio;
	bio_set_flush(bio);
	vdev_submit_bio(bio);
	invalidate_bdev(bdev);

	return (0);
}

static int
vdev_disk_io_trim(zio_t *zio)
{
	vdev_t *v = zio->io_vd;
	vdev_disk_t *vd = v->vdev_tsd;

#if defined(HAVE_BLKDEV_ISSUE_SECURE_ERASE)
	if (zio->io_trim_flags & ZIO_TRIM_SECURE) {
		return (-blkdev_issue_secure_erase(vd->vd_bdev,
		    zio->io_offset >> 9, zio->io_size >> 9, GFP_NOFS));
	} else {
		return (-blkdev_issue_discard(vd->vd_bdev,
		    zio->io_offset >> 9, zio->io_size >> 9, GFP_NOFS));
	}
#elif defined(HAVE_BLKDEV_ISSUE_DISCARD)
	unsigned long trim_flags = 0;
#if defined(BLKDEV_DISCARD_SECURE)
	if (zio->io_trim_flags & ZIO_TRIM_SECURE)
		trim_flags |= BLKDEV_DISCARD_SECURE;
#endif
	return (-blkdev_issue_discard(vd->vd_bdev,
	    zio->io_offset >> 9, zio->io_size >> 9, GFP_NOFS, trim_flags));
#else
#error "Unsupported kernel"
#endif
}

static void
vdev_disk_io_start(zio_t *zio)
{
	vdev_t *v = zio->io_vd;
	vdev_disk_t *vd = v->vdev_tsd;
	int error;

	/*
	 * If the vdev is closed, it's likely in the REMOVED or FAULTED state.
	 * Nothing to be done here but return failure.
	 */
	if (vd == NULL) {
		zio->io_error = ENXIO;
		zio_interrupt(zio);
		return;
	}

	rw_enter(&vd->vd_lock, RW_READER);

	/*
	 * If the vdev is closed, it's likely due to a failed reopen and is
	 * in the UNAVAIL state.  Nothing to be done here but return failure.
	 */
	if (vd->vd_bdev == NULL) {
		rw_exit(&vd->vd_lock);
		zio->io_error = ENXIO;
		zio_interrupt(zio);
		return;
	}

	switch (zio->io_type) {
	case ZIO_TYPE_IOCTL:

		if (!vdev_readable(v)) {
			rw_exit(&vd->vd_lock);
			zio->io_error = SET_ERROR(ENXIO);
			zio_interrupt(zio);
			return;
		}

		switch (zio->io_cmd) {
		case DKIOCFLUSHWRITECACHE:

			if (zfs_nocacheflush)
				break;

			if (v->vdev_nowritecache) {
				zio->io_error = SET_ERROR(ENOTSUP);
				break;
			}

			error = vdev_disk_io_flush(vd->vd_bdev, zio);
			if (error == 0) {
				rw_exit(&vd->vd_lock);
				return;
			}

			zio->io_error = error;

			break;

		default:
			zio->io_error = SET_ERROR(ENOTSUP);
		}

		rw_exit(&vd->vd_lock);
		zio_execute(zio);
		return;

	case ZIO_TYPE_TRIM:
		zio->io_error = vdev_disk_io_trim(zio);
		rw_exit(&vd->vd_lock);
		zio_interrupt(zio);
		return;

	case ZIO_TYPE_READ:
	case ZIO_TYPE_WRITE:
		zio->io_target_timestamp = zio_handle_io_delay(zio);
		error = vdev_disk_io_rw(zio);
		rw_exit(&vd->vd_lock);
		if (error) {
			zio->io_error = error;
			zio_interrupt(zio);
		}
		return;

	default:
		/*
		 * Getting here means our parent vdev has made a very strange
		 * request of us, and shouldn't happen. Assert here to force a
		 * crash in dev builds, but in production return the IO
		 * unhandled. The pool will likely suspend anyway but that's
		 * nicer than crashing the kernel.
		 */
		ASSERT3S(zio->io_type, ==, -1);

		rw_exit(&vd->vd_lock);
		zio->io_error = SET_ERROR(ENOTSUP);
		zio_interrupt(zio);
		return;
	}

	__builtin_unreachable();
}

static void
vdev_disk_io_done(zio_t *zio)
{
	/*
	 * If the device returned EIO, we revalidate the media.  If it is
	 * determined the media has changed this triggers the asynchronous
	 * removal of the device from the configuration.
	 */
	if (zio->io_error == EIO) {
		vdev_t *v = zio->io_vd;
		vdev_disk_t *vd = v->vdev_tsd;

		if (!zfs_check_disk_status(vd->vd_bdev)) {
			invalidate_bdev(vd->vd_bdev);
			v->vdev_remove_wanted = B_TRUE;
			spa_async_request(zio->io_spa, SPA_ASYNC_REMOVE);
		}
	}
}

static void
vdev_disk_hold(vdev_t *vd)
{
	ASSERT(spa_config_held(vd->vdev_spa, SCL_STATE, RW_WRITER));

	/* We must have a pathname, and it must be absolute. */
	if (vd->vdev_path == NULL || vd->vdev_path[0] != '/')
		return;

	/*
	 * Only prefetch path and devid info if the device has
	 * never been opened.
	 */
	if (vd->vdev_tsd != NULL)
		return;

}

static void
vdev_disk_rele(vdev_t *vd)
{
	ASSERT(spa_config_held(vd->vdev_spa, SCL_STATE, RW_WRITER));

	/* XXX: Implement me as a vnode rele for the device */
}

vdev_ops_t vdev_disk_ops = {
	.vdev_op_init = NULL,
	.vdev_op_fini = NULL,
	.vdev_op_open = vdev_disk_open,
	.vdev_op_close = vdev_disk_close,
	.vdev_op_asize = vdev_default_asize,
	.vdev_op_min_asize = vdev_default_min_asize,
	.vdev_op_min_alloc = NULL,
	.vdev_op_io_start = vdev_disk_io_start,
	.vdev_op_io_done = vdev_disk_io_done,
	.vdev_op_state_change = NULL,
	.vdev_op_need_resilver = NULL,
	.vdev_op_hold = vdev_disk_hold,
	.vdev_op_rele = vdev_disk_rele,
	.vdev_op_remap = NULL,
	.vdev_op_xlate = vdev_default_xlate,
	.vdev_op_rebuild_asize = NULL,
	.vdev_op_metaslab_init = NULL,
	.vdev_op_config_generate = NULL,
	.vdev_op_nparity = NULL,
	.vdev_op_ndisks = NULL,
	.vdev_op_type = VDEV_TYPE_DISK,		/* name of this vdev type */
	.vdev_op_leaf = B_TRUE,			/* leaf vdev */
	.vdev_op_kobj_evt_post = vdev_disk_kobj_evt_post
};

/*
 * The zfs_vdev_scheduler module option has been deprecated. Setting this
 * value no longer has any effect.  It has not yet been entirely removed
 * to allow the module to be loaded if this option is specified in the
 * /etc/modprobe.d/zfs.conf file.  The following warning will be logged.
 */
static int
param_set_vdev_scheduler(const char *val, zfs_kernel_param_t *kp)
{
	int error = param_set_charp(val, kp);
	if (error == 0) {
		printk(KERN_INFO "The 'zfs_vdev_scheduler' module option "
		    "is not supported.\n");
	}

	return (error);
}

static const char *zfs_vdev_scheduler = "unused";
module_param_call(zfs_vdev_scheduler, param_set_vdev_scheduler,
    param_get_charp, &zfs_vdev_scheduler, 0644);
MODULE_PARM_DESC(zfs_vdev_scheduler, "I/O scheduler");

int
param_set_min_auto_ashift(const char *buf, zfs_kernel_param_t *kp)
{
	uint_t val;
	int error;

	error = kstrtouint(buf, 0, &val);
	if (error < 0)
		return (SET_ERROR(error));

	if (val < ASHIFT_MIN || val > zfs_vdev_max_auto_ashift)
		return (SET_ERROR(-EINVAL));

	error = param_set_uint(buf, kp);
	if (error < 0)
		return (SET_ERROR(error));

	return (0);
}

int
param_set_max_auto_ashift(const char *buf, zfs_kernel_param_t *kp)
{
	uint_t val;
	int error;

	error = kstrtouint(buf, 0, &val);
	if (error < 0)
		return (SET_ERROR(error));

	if (val > ASHIFT_MAX || val < zfs_vdev_min_auto_ashift)
		return (SET_ERROR(-EINVAL));

	error = param_set_uint(buf, kp);
	if (error < 0)
		return (SET_ERROR(error));

	return (0);
}

ZFS_MODULE_PARAM(zfs_vdev, zfs_vdev_, open_timeout_ms, UINT, ZMOD_RW,
	"Timeout before determining that a device is missing");

ZFS_MODULE_PARAM(zfs_vdev, zfs_vdev_, failfast_mask, UINT, ZMOD_RW,
	"Defines failfast mask: 1 - device, 2 - transport, 4 - driver");

ZFS_MODULE_PARAM(zfs_vdev_disk, vdev_disk_, max_segs, UINT, ZMOD_RW,
	"Maximum number of data segments to add to an IO request (min 4)");
