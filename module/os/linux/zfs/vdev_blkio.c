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
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2011, 2020 by Delphix. All rights reserved.
 * Copyright (c) 2023, Rob Norris <robn@despairlabs.com>
 */

#include <sys/zfs_context.h>
#include <sys/vdev_impl.h>
#include <sys/vdev_blkio.h>
#include <blkio.h>

#define	MAX_COMPLETIONS	(16)
#define	MAX_THREADS	(4)

typedef struct {
	zio_t	*vbr_zio;
	void	*vbr_buf;
} vdev_blkio_req_t;

typedef struct {
	struct blkio	*vbt_blkio;
	struct blkioq	*vbt_blkio_q;
	kmutex_t	vbt_lock;
	kthread_t	*vbt_thread;
} vdev_blkio_thread_t;

static kmem_cache_t *vdev_blkio_req_cache;

void
vdev_blkio_init(void)
{
	vdev_blkio_req_cache = kmem_cache_create("vdev_blkio_req",
	    sizeof(vdev_blkio_req_t), 0, NULL, NULL, NULL, NULL, NULL, 0);
}

void
vdev_blkio_fini(void)
{
	kmem_cache_destroy(vdev_blkio_req_cache);
}

static void vdev_blkio_completion_thread(void *arg);

static int
vdev_blkio_open(vdev_t *vd, uint64_t *psize, uint64_t *max_psize,
    uint64_t *logical_ashift, uint64_t *physical_ashift)
{
	struct blkio *b;
	int error;

	/* XXX I bet we can find out something about these */
	vd->vdev_nonrot = B_TRUE;
	vd->vdev_nowritecache = B_FALSE;
	vd->vdev_has_trim = B_FALSE;
	vd->vdev_has_securetrim = B_FALSE;

	if (vd->vdev_path == NULL || vd->vdev_path[0] != '/') {
		vd->vdev_stat.vs_aux = VDEV_AUX_BAD_LABEL;
		return (SET_ERROR(EINVAL));
	}

	/* XXX reopen */
	ASSERT0(vd->vdev_tsd);

	/* XXX need to hook blkio_get_error_msg() through here */
	/* XXX make blkio driver configurable */
	error = -blkio_create("io_uring", &b);
	if (error != 0) {
		vd->vdev_stat.vs_aux = VDEV_AUX_OPEN_FAILED;
		return (SET_ERROR(error));
	}

	/* set device path; force O_DIRECT */
	error = -blkio_set_str(b, "path", vd->vdev_path);
	if (error == 0)
		error = -blkio_set_bool(b, "direct", true);
	if (error != 0) {
		blkio_destroy(&b);
		vd->vdev_stat.vs_aux = VDEV_AUX_OPEN_FAILED;
		return (SET_ERROR(error));
	}

	error = blkio_connect(b);
	if (error != 0) {
		blkio_destroy(&b);
		vd->vdev_stat.vs_aux = VDEV_AUX_OPEN_FAILED;
		return (SET_ERROR(error));
	}

	/* XXX we can configure queue and other things here, see PROPERTIES */

	error = blkio_start(b);
	if (error != 0) {
		blkio_destroy(&b);
		vd->vdev_stat.vs_aux = VDEV_AUX_OPEN_FAILED;
		return (SET_ERROR(error));
	}

	vdev_blkio_thread_t *vbts =
	    kmem_zalloc(sizeof (vdev_blkio_thread_t) * MAX_THREADS, KM_SLEEP);

	int nq = 1;
	bool can_add_queues;
	blkio_get_bool(b, "can-add-queues", &can_add_queues);
	if (can_add_queues) {
		blkio_get_int(b, "max-queues", &nq);
		nq = MIN(MAX_THREADS, nq);
		for (int i = 0; i < nq; i++)
			blkio_add_queue(b);
	}

	for (int i = 0; i < MAX_THREADS; i++) {
		vdev_blkio_thread_t *vbt = &vbts[i];
		vbt->vbt_blkio = b;
		vbt->vbt_blkio_q = blkio_get_queue(b, i % nq);
		mutex_init(&vbt->vbt_lock, NULL, MUTEX_DEFAULT, NULL);
		vbt->vbt_thread = thread_create(NULL, 0,
		    vdev_blkio_completion_thread, vbt, 0, &p0, TS_RUN, 0);
	}

	vd->vdev_tsd = vbts;

	blkio_get_uint64(b, "capacity", psize);
	*max_psize = *psize;

	int block_size = 0;
	blkio_get_int(b, "request-alignment", &block_size);
	*logical_ashift = highbit64(MAX(block_size, SPA_MINBLOCKSIZE)) - 1;
	block_size = 0;
	blkio_get_int(b, "optimal-io-alignment", &block_size);
	*physical_ashift = highbit64(MAX(block_size, SPA_MINBLOCKSIZE)) - 1;

	cmn_err(CE_WARN, "vdev_blkio_open: opened %s: psize %lu lshift %lu pshift %lu; queues %d threads %d", vd->vdev_path, *psize, *logical_ashift, *physical_ashift, nq, MAX_THREADS);

	/* XXX sanity; I think mem regions will be needed for performance but
	 *     for now I just don't want to run into them */
	bool needs_mem_regions;
	blkio_get_bool(b, "needs-mem-regions", &needs_mem_regions);
	ASSERT0(needs_mem_regions);

	/*
	{
		uint64_t max_mem_regions;
		bool may_pin_mem_regions;
		uint64_t mem_region_alignment;
		bool needs_mem_region_fd;

		blkio_get_uint64(b, "max-mem-regions", &max_mem_regions);
		blkio_get_bool(b, "may-pin-mem-regions", &may_pin_mem_regions);
		blkio_get_uint64(b, "mem-region-alignment", &mem_region_alignment);
		blkio_get_bool(b, "needs-mem-regions", &needs_mem_regions);
		blkio_get_bool(b, "needs-mem-region-fd", &needs_mem_region_fd);

		cmn_err(CE_WARN, "vdev_blkio_open: mem_regions: max %lu may_pin %d alignment %lu need %d need_fd %d", max_mem_regions, may_pin_mem_regions, mem_region_alignment, needs_mem_regions, needs_mem_region_fd);
	}
	*/

	return (0);
}

static void
vdev_blkio_close(vdev_t *vd)
{
	vdev_blkio_thread_t *vbts = vd->vdev_tsd, *vbt;

	if (vd->vdev_reopening || vbts == NULL)
		return;

	for (int i = 0; i < MAX_THREADS; i++) {
		vbt = &vbts[i];
		/* XXX tear down the thread */
		mutex_destroy(&vbt->vbt_lock);
	}

	blkio_destroy(&vbts[0].vbt_blkio);
	kmem_free(vbts, sizeof (vdev_blkio_thread_t) * MAX_THREADS);

	vd->vdev_tsd = NULL;
}

static __attribute__((noreturn)) void
vdev_blkio_completion_thread(void *arg)
{
	vdev_blkio_thread_t *vbt = arg;
	struct blkioq *q = vbt->vbt_blkio_q;
	int fd = blkioq_get_completion_fd(q);
	char event_data[8];
	struct blkio_completion *completion = kmem_zalloc(
	    sizeof (struct blkio_completion) * MAX_COMPLETIONS, KM_SLEEP);
	vdev_blkio_req_t *vbr;
	zio_t *zio;
	int nc;

	fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, NULL) & ~O_NONBLOCK);
	blkioq_set_completion_fd_enabled(q, true);

	for (;;) {
		read(fd, event_data, sizeof(event_data));

		mutex_enter(&vbt->vbt_lock);
		nc = blkioq_do_io(q, completion, 0, MAX_COMPLETIONS, NULL);
		if (nc < 0)
			ASSERT3U(nc, ==, -EINVAL);
		if (nc <= 0) {
			mutex_exit(&vbt->vbt_lock);
			continue;
		}
		mutex_exit(&vbt->vbt_lock);

		for (int i = 0; i < nc; i++) {
			vbr = completion[i].user_data;
			zio = vbr->vbr_zio;

			switch (zio->io_type) {
			case ZIO_TYPE_READ:
				abd_return_buf_copy(zio->io_abd,
				    vbr->vbr_buf, zio->io_size);
				break;

			default:
				VERIFY0("got completion for a io type we don't issue, impossible");
			}

			kmem_cache_free(vdev_blkio_req_cache, vbr);

			if (completion[i].ret < 0)
				zio->io_error =
				    SET_ERROR(-completion[i].ret);

			zio_interrupt(zio);
		}
	}
}

static void
vdev_blkio_io_start(zio_t *zio)
{
	vdev_t *vd = zio->io_vd;
	vdev_blkio_thread_t *vbts = vd->vdev_tsd;
	vdev_blkio_thread_t *vbt = &vbts[random_in_range(MAX_THREADS)];

	switch (zio->io_type) {
	case ZIO_TYPE_READ:
		vdev_blkio_req_t *req =
		    kmem_cache_alloc(vdev_blkio_req_cache, KM_SLEEP);
		req->vbr_zio = zio;
		req->vbr_buf = abd_borrow_buf(zio->io_abd, zio->io_size);

		mutex_enter(&vbt->vbt_lock);
		blkioq_read(vbt->vbt_blkio_q, zio->io_offset,
		    req->vbr_buf, zio->io_size, req, 0);
		blkioq_do_io(vbt->vbt_blkio_q, NULL, 0, 0, NULL);
		mutex_exit(&vbt->vbt_lock);
		break;

	default:
		cmn_err(CE_WARN, "no support for zio type %d", zio->io_type);
		zio->io_error = SET_ERROR(ENOTSUP);
		zio_interrupt(zio);
		return;
	}
}

static void
vdev_blkio_io_done(zio_t *zio)
{
	(void) zio;
}

static void
vdev_blkio_hold(vdev_t *vd)
{
	(void) vd;
}

static void
vdev_blkio_rele(vdev_t *vd)
{
	(void) vd;
}

/* Alternate userspace block driver */
vdev_ops_t vdev_disk_ops = {
	.vdev_op_init = NULL,
	.vdev_op_fini = NULL,
	.vdev_op_open = vdev_blkio_open,
	.vdev_op_close = vdev_blkio_close,
	.vdev_op_asize = vdev_default_asize,
	.vdev_op_min_asize = vdev_default_min_asize,
	.vdev_op_min_alloc = NULL,
	.vdev_op_io_start = vdev_blkio_io_start,
	.vdev_op_io_done = vdev_blkio_io_done,
	.vdev_op_state_change = NULL,
	.vdev_op_need_resilver = NULL,
	.vdev_op_hold = vdev_blkio_hold,
	.vdev_op_rele = vdev_blkio_rele,
	.vdev_op_remap = NULL,
	.vdev_op_xlate = vdev_default_xlate,
	.vdev_op_rebuild_asize = NULL,
	.vdev_op_metaslab_init = NULL,
	.vdev_op_config_generate = NULL,
	.vdev_op_nparity = NULL,
	.vdev_op_ndisks = NULL,
	.vdev_op_type = VDEV_TYPE_DISK,		/* name of this vdev type */
	.vdev_op_leaf = B_TRUE			/* leaf vdev */
};
