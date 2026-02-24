# SPDX-License-Identifier: CDDL-1.0
if BUILD_LINUX
include $(srcdir)/%D%/os/linux/Makefile.am
endif
if BUILD_FREEBSD
include $(srcdir)/%D%/os/freebsd/Makefile.am
endif


COMMON_H = \
	cityhash.h \
	zfeature_common.h \
	zfs_comutil.h \
	zfs_crrd.h \
	zfs_deleg.h \
	zfs_fletcher.h \
	zfs_namecheck.h \
	zfs_prop.h \
	zfs_valstr.h \
	\
	zfs/abd.h \
	zfs/abd_impl.h \
	zfs/aggsum.h \
	zfs/arc.h \
	zfs/arc_impl.h \
	zfs/asm_linkage.h \
	zfs/avl.h \
	zfs/avl_impl.h \
	zfs/bitmap.h \
	zfs/bitops.h \
	zfs/blake3.h \
	zfs/blkptr.h \
	zfs/bplist.h \
	zfs/bpobj.h \
	zfs/bptree.h \
	zfs/bqueue.h \
	zfs/brt.h \
	zfs/brt_impl.h \
	zfs/btree.h \
	zfs/dataset_kstats.h \
	zfs/dbuf.h \
	zfs/ddt.h \
	zfs/ddt_impl.h \
	zfs/dmu.h \
	zfs/dmu_impl.h \
	zfs/dmu_objset.h \
	zfs/dmu_recv.h \
	zfs/dmu_redact.h \
	zfs/dmu_send.h \
	zfs/dmu_traverse.h \
	zfs/dmu_tx.h \
	zfs/dmu_zfetch.h \
	zfs/dnode.h \
	zfs/dsl_bookmark.h \
	zfs/dsl_crypt.h \
	zfs/dsl_dataset.h \
	zfs/dsl_deadlist.h \
	zfs/dsl_deleg.h \
	zfs/dsl_destroy.h \
	zfs/dsl_dir.h \
	zfs/dsl_pool.h \
	zfs/dsl_prop.h \
	zfs/dsl_scan.h \
	zfs/dsl_synctask.h \
	zfs/dsl_userhold.h \
	zfs/edonr.h \
	zfs/efi_partition.h \
	zfs/frame.h \
	zfs/hkdf.h \
	zfs/metaslab.h \
	zfs/metaslab_impl.h \
	zfs/mmp.h \
	zfs/mntent.h \
	zfs/multilist.h \
	zfs/nvpair.h \
	zfs/nvpair_impl.h \
	zfs/objlist.h \
	zfs/pathname.h \
	zfs/qat.h \
	zfs/range_tree.h \
	zfs/rrwlock.h \
	zfs/sa.h \
	zfs/sa_impl.h \
	zfs/sha2.h \
	zfs/simd_config.h \
	zfs/skein.h \
	zfs/space_map.h \
	zfs/space_reftree.h \
	zfs/spa_checkpoint.h \
	zfs/spa_checksum.h \
	zfs/spa.h \
	zfs/spa_impl.h \
	zfs/spa_log_spacemap.h \
	zfs/sysevent.h \
	zfs/txg.h \
	zfs/txg_impl.h \
	zfs/u8_textprep_data.h \
	zfs/u8_textprep.h \
	zfs/uberblock.h \
	zfs/uberblock_impl.h \
	zfs/uio_impl.h \
	zfs/unique.h \
	zfs/uuid.h \
	zfs/vdev_disk.h \
	zfs/vdev_draid.h \
	zfs/vdev_file.h \
	zfs/vdev.h \
	zfs/vdev_impl.h \
	zfs/vdev_indirect_births.h \
	zfs/vdev_indirect_mapping.h \
	zfs/vdev_initialize.h \
	zfs/vdev_raidz.h \
	zfs/vdev_raidz_impl.h \
	zfs/vdev_rebuild.h \
	zfs/vdev_removal.h \
	zfs/vdev_trim.h \
	zfs/xvattr.h \
	zfs/zap.h \
	zfs/zap_impl.h \
	zfs/zap_leaf.h \
	zfs/zcp_global.h \
	zfs/zcp.h \
	zfs/zcp_iter.h \
	zfs/zcp_prop.h \
	zfs/zcp_set.h \
	zfs/zfeature.h \
	zfs/zfs_acl.h \
	zfs/zfs_bootenv.h \
	zfs/zfs_chksum.h \
	zfs/zfs_context.h \
	zfs/zfs_debug.h \
	zfs/zfs_delay.h \
	zfs/zfs_file.h \
	zfs/zfs_fuid.h \
	zfs/zfs_impl.h \
	zfs/zfs_project.h \
	zfs/zfs_quota.h \
	zfs/zfs_racct.h \
	zfs/zfs_ratelimit.h \
	zfs/zfs_refcount.h \
	zfs/zfs_rlock.h \
	zfs/zfs_sa.h \
	zfs/zfs_stat.h \
	zfs/zfs_sysfs.h \
	zfs/zfs_vfsops.h \
	zfs/zfs_vnops.h \
	zfs/zfs_znode.h \
	zfs/zil.h \
	zfs/zil_impl.h \
	zfs/zio_checksum.h \
	zfs/zio_compress.h \
	zfs/zio_crypt.h \
	zfs/zio.h \
	zfs/zio_impl.h \
	zfs/zrlock.h \
	zfs/zthr.h \
	\
	zfs/crypto/api.h \
	zfs/crypto/common.h \
	zfs/crypto/icp.h \
	\
	zfs/fm/protocol.h \
	zfs/fm/util.h \
	zfs/fm/fs/zfs.h \
	\
	zfs/fs/zfs.h \
	\
	zfs/lua/lauxlib.h \
	zfs/lua/lua.h \
	zfs/lua/luaconf.h \
	zfs/lua/lualib.h \
	\
	zfs/sysevent/dev.h \
	zfs/sysevent/eventdefs.h \
	\
	zfs/zstd/zstd.h


KERNEL_H = \
	zfs/zfs_ioctl.h \
	zfs/zfs_ioctl_impl.h \
	zfs/zfs_onexit.h \
	zfs/zvol.h \
	zfs/zvol_impl.h


USER_H = \
	libnvpair.h \
	libzdb.h \
	libzfs.h \
	libzfs_core.h \
	libzfsbootenv.h \
	libzpool.h \
	libzutil.h


if CONFIG_USER
libzfsdir = $(includedir)/libzfs
nobase_libzfs_HEADERS = $(COMMON_H) $(USER_H)
endif

kerneldir = $(prefix)/src/zfs-$(VERSION)/include
if CONFIG_KERNEL
if BUILD_LINUX
nobase_kernel_HEADERS = $(COMMON_H) $(KERNEL_H)
endif
endif
