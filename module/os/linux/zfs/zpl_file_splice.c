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
 * Copyright (c) 2024, Rob Norris <robn@despairlabs.com>
 */

#ifdef CONFIG_COMPAT
#include <linux/compat.h>
#endif
#include <linux/fs.h>
#include <linux/pipe_fs_i.h>

#include <sys/dbuf.h>
#include <sys/dmu_objset.h>
#include <sys/zfs_znode.h>
#include <sys/zfs_vnops.h>

int zfs_splice_enable = 1;

#ifndef	HAVE_PIPE_HEAD_BUF
#define	pipe_head_buf(pipe)	\
	(&pipe->bufs[pipe->head & (pipe->ring_size - 1)])
#endif

/*
 * Take an additional reference to the dbuf. tee() calls this to "duplicate"
 * a pipe buffer.
 */
static bool
zpl_fill_pipe_buf_get(struct pipe_inode_info *pipe, struct pipe_buffer *buf)
{
	dmu_buf_t *db = (dmu_buf_t *)buf->private;
	dmu_buf_add_ref(db, FTAG);
	return (true);
}

/*
 * Drop dbuf reference when the pipe buf is released. If it was the last one,
 * the dbuf will be freed (returned to the cache).
 */
static void
zpl_fill_pipe_buf_release(struct pipe_inode_info *pipe, struct pipe_buffer *buf)
{
	dmu_buf_t *db = (dmu_buf_t *)buf->private;
	dmu_buf_rele(db, FTAG);
}

static const struct pipe_buf_operations zpl_splice_pipe_buf_ops = {
	.get		= zpl_fill_pipe_buf_get,
	.release	= zpl_fill_pipe_buf_release,
};

typedef struct {
	struct pipe_inode_info *pipe;
	size_t count;
	dmu_buf_t *db;
} zpl_fill_pipe_t;

/* Load each page in the abd into the pipe. */
static int
zpl_fill_pipe_cb(struct page *page, size_t off, size_t len, void *priv)
{
	zpl_fill_pipe_t *zfp = priv;
	struct pipe_inode_info *pipe = zfp->pipe;

	/* If the pipe is full, we can't continue */
	if (pipe_full(pipe->head, pipe->tail, pipe->max_usage))
		return (1);

	/*
	 * Take an extra reference to the dbuf. This will be released in
	 * zpl_fill_pipe_buf_release when the other end of the pipe is done
	 * with it.
	 */
	dmu_buf_add_ref(zfp->db, FTAG);

	/* Get the head buffer */
	struct pipe_buffer *buf = pipe_head_buf(pipe);

	/* Load the page into the buffer */
	*buf = (struct pipe_buffer) {
		.ops =	&zpl_splice_pipe_buf_ops,
		.page = page,
		.offset = off,
		.len = len,
		.private = (unsigned long)zfp->db,
	};

	/* Advance the head */
	pipe->head++;

	/* Track how much we loaded in */
	zfp->count += len;

	return (0);
}

/*
 * Handler for VFS splice_read operation. This is the underlying implementation
 * for splice(), sendfile() and tee(). It's purpose is to load struct page
 * pointers into a pipe ring buffer, to be accepted by another task in the
 * system without the need to copy the data within.
 *
 * To do this, we pass in dbuf pages. These are already allocated and contain
 * the raw uncompressed data.
 *
 * Most Linux filesystems implement this by taking additional references to
 * the underlying pages. Because dbufs are our "atomic" unit of data here, and
 * may span multiple pages, we instead take additional holds on the dbuf, one
 * per page or page segment we load into the pipe. Linux will call the release
 * function once per page (pipe buffer), so as many releases will occur as
 * holds, balancing it out.
 */
ssize_t
zpl_splice_read(struct file *filp, loff_t *ppos,
    struct pipe_inode_info *pipe, size_t len, unsigned int flags)
{
	if (!zfs_splice_enable)
#ifdef HAVE_COPY_SPLICE_READ
		return (copy_splice_read(filp, ppos, pipe, len, flags));
#else
		return (generic_file_splice_read(filp, ppos, pipe, len, flags));
#endif

	/* XXX consider flags */

	cred_t *cr = CRED();
	fstrans_cookie_t cookie;
	ssize_t bytes = 0;
	int err = 0;

	crhold(cr);
	cookie = spl_fstrans_mark();

	znode_t *zp = ITOZ(file_inode(filp));
	zfsvfs_t *zfsvfs = ZTOZSB(zp);
	err = zfs_enter_verify_zp(zfsvfs, zp, FTAG);
	if (err != 0)
		goto out;

	/* XXX pipe capacity, to keep the holds as small as possible */
	uint64_t off = *ppos;
	if (off >= zp->z_size || len == 0) {
		zfs_exit(zfsvfs, FTAG);
		goto out;
	}

	if (len > zp->z_size - off)
		len = zp->z_size - off;

	dmu_buf_impl_t *zdb = (dmu_buf_impl_t *)sa_get_db(zp->z_sa_hdl);
	DB_DNODE_ENTER(zdb);
	dnode_t *dn = DB_DNODE(zdb);

	/*
	 * Start loading the whole range. We don't yet know how much of it
	 * we're going to use, since it depends on how much room is left in the
	 * pipe. Prefetching keeps the time spent in dmu_buf_hold_by_dnode()
	 * minimum.
	 */
	/* XXX but maybe the speculative prefetcher will do a better job? */
	dmu_prefetch_by_dnode(dn, 0, off, len, ZIO_PRIORITY_ASYNC_READ);

	/* Lock the range against changes. */
	/*
	 * XXX unclear if needed. if we do need it, then we probably need to
	 *     be rangelocking over each page as we submit it, and releasing
	 *     the rangelock as the pipe empties.
	 *
	 *     hmm, perhaps not though, as then a stalled reader would be able
	 *     to block writes? hmm, I can sorta make the case either way
	zfs_locked_range_t *lr =
	    zfs_rangelock_enter(&zp->z_rangelock, off, len, RL_READER);
	*/

	dmu_buf_t *db;
	abd_t abd;
	while (len > 0) {
		err = dmu_buf_hold_by_dnode(dn, off, FTAG, &db,
		    DMU_READ_PREFETCH);
		if (err != 0)
			break;

		/* offset and length within this dbuf */
		uint64_t boff = off - db->db_offset;
		uint64_t blen = MIN(db->db_size - boff, len);

		/* wrap it in an abd so we can iterate */
		abd_get_from_buf_struct(&abd, db->db_data, db->db_size);

		/* iterator callback state */
		zpl_fill_pipe_t zfp = {
			.pipe = pipe,
			.count = 0,
			.db = db,
		};

		/* walk the pages */
		boolean_t full = abd_iterate_page_func(&abd, boff, blen,
		    zpl_fill_pipe_cb, &zfp);

		abd_free(&abd);

		dmu_buf_rele(db, FTAG);

		/* update position */
		off += zfp.count;
		len -= zfp.count;

		/* Count how much we loaded into the pipe */
		bytes += zfp.count;

		/* abd_iterate_page_func() ran out of room in the pipe. */
		if (full) {
			err = SET_ERROR(EAGAIN);
			break;
		}

		if (issig()) {
			err = SET_ERROR(EINTR);
			break;
		}
	}

	DB_DNODE_EXIT(zdb);

	/* Release rangelock. */
	/*
	 * XXX only if taken, see above
	zfs_rangelock_exit(lr);
	*/

	zfs_exit(zfsvfs, FTAG);

	/* Update caller file offset */
	*ppos = off;

	dataset_kstats_update_read_kstats(&zfsvfs->z_kstat, bytes);

out:
	spl_fstrans_unmark(cookie);
	crfree(cr);

	return (err != 0 ? -err : bytes);
}

ZFS_MODULE_PARAM(zfs, zfs_, splice_enable, INT, ZMOD_RW,
	"Enable direct handling of splice() system call");
