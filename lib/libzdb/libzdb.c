#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <ctype.h>
#include <getopt.h>
#include <openssl/evp.h>
#include <zfs/zfs_context.h>
#include <zfs/spa.h>
#include <zfs/spa_impl.h>
#include <zfs/dmu.h>
#include <zfs/zap.h>
#include <zfs/fs/zfs.h>
#include <zfs/zfs_znode.h>
#include <zfs/zfs_sa.h>
#include <zfs/sa.h>
#include <zfs/sa_impl.h>
#include <zfs/vdev.h>
#include <zfs/vdev_impl.h>
#include <zfs/metaslab_impl.h>
#include <zfs/dmu_objset.h>
#include <zfs/dsl_dir.h>
#include <zfs/dsl_dataset.h>
#include <zfs/dsl_pool.h>
#include <zfs/dsl_bookmark.h>
#include <zfs/dbuf.h>
#include <zfs/zil.h>
#include <zfs/zil_impl.h>
#include <spl/stat.h>
#include <sys/resource.h>
#include <zfs/dmu_send.h>
#include <zfs/dmu_traverse.h>
#include <zfs/zio_checksum.h>
#include <zfs/zio_compress.h>
#include <zfs/zfs_fuid.h>
#include <zfs/arc.h>
#include <zfs/arc_impl.h>
#include <zfs/ddt.h>
#include <zfs/zfeature.h>
#include <zfs/abd.h>
#include <zfs/blkptr.h>
#include <zfs/dsl_crypt.h>
#include <zfs/dsl_scan.h>
#include <zfs/btree.h>
#include <zfs/brt.h>
#include <zfs/brt_impl.h>
#include <zfs_comutil.h>
#include <zfs/zstd/zstd.h>

#include <libnvpair.h>
#include <libzutil.h>

#include <libzdb.h>

const char *
zdb_ot_name(dmu_object_type_t type)
{
	if (type < DMU_OT_NUMTYPES)
		return (dmu_ot[type].ot_name);
	else if ((type & DMU_OT_NEWTYPE) &&
	    ((type & DMU_OT_BYTESWAP_MASK) < DMU_BSWAP_NUMFUNCS))
		return (dmu_ot_byteswap[type & DMU_OT_BYTESWAP_MASK].ob_name);
	else
		return ("UNKNOWN");
}

int
livelist_compare(const void *larg, const void *rarg)
{
	const blkptr_t *l = larg;
	const blkptr_t *r = rarg;
	int cmp = 0;

	/* Sort them according to dva[0] */
	cmp = TREE_CMP(DVA_GET_VDEV(&l->blk_dva[0]),
	    DVA_GET_VDEV(&r->blk_dva[0]));
	if (cmp != 0)
		return (cmp);

	/* if vdevs are equal, sort by offsets. */
	cmp = TREE_CMP(DVA_GET_OFFSET(&l->blk_dva[0]),
	    DVA_GET_OFFSET(&r->blk_dva[0]));
	if (cmp != 0)
		return (cmp);

	/*
	 * Since we're storing blkptrs without cancelling FREE/ALLOC pairs,
	 * it's possible the offsets are equal. In that case, sort by txg
	 */
	return (TREE_CMP(BP_GET_BIRTH(l), BP_GET_BIRTH(r)));
}
