# SPDX-License-Identifier: CDDL-1.0
if CONFIG_KERNEL
kernel_linuxdir = $(kerneldir)/linux
kernel_linux_HEADERS = \
	%D%/kernel/linux/blkdev_compat.h \
	%D%/kernel/linux/compiler_compat.h \
	%D%/kernel/linux/dcache_compat.h \
	%D%/kernel/linux/kmap_compat.h \
	%D%/kernel/linux/mm_compat.h \
	%D%/kernel/linux/mod_compat.h \
	%D%/kernel/linux/page_compat.h \
	%D%/kernel/linux/pagemap_compat.h \
	%D%/kernel/linux/simd.h \
	%D%/kernel/linux/simd_aarch64.h \
	%D%/kernel/linux/simd_arm.h \
	%D%/kernel/linux/simd_powerpc.h \
	%D%/kernel/linux/simd_x86.h \
	%D%/kernel/linux/utsname_compat.h \
	%D%/kernel/linux/vfs_compat.h \
	%D%/kernel/linux/xattr_compat.h

kernel_zfsdir = $(kerneldir)/zfs
kernel_zfs_HEADERS = \
	%D%/zfs/abd_os.h \
	%D%/zfs/abd_impl_os.h \
	%D%/zfs/policy.h \
	%D%/zfs/trace_acl.h \
	%D%/zfs/trace_arc.h \
	%D%/zfs/trace_common.h \
	%D%/zfs/trace_dbgmsg.h \
	%D%/zfs/trace_dbuf.h \
	%D%/zfs/trace_dmu.h \
	%D%/zfs/trace_dnode.h \
	%D%/zfs/trace_multilist.h \
	%D%/zfs/trace_rrwlock.h \
	%D%/zfs/trace_txg.h \
	%D%/zfs/trace_vdev.h \
	%D%/zfs/trace_zfs.h \
	%D%/zfs/trace_zil.h \
	%D%/zfs/trace_zio.h \
	%D%/zfs/trace_zrlock.h \
	%D%/zfs/zfs_bootenv_os.h \
	%D%/zfs/zfs_context_os.h \
	%D%/zfs/zfs_ctldir.h \
	%D%/zfs/zfs_debug_os.h \
	%D%/zfs/zfs_dir.h \
	%D%/zfs/zfs_vfsops_os.h \
	%D%/zfs/zfs_vnops_os.h \
	%D%/zfs/zfs_znode_impl.h \
	%D%/zfs/zpl.h

kernel_spl_rpcdir = $(kerneldir)/spl/rpc
kernel_spl_rpc_HEADERS = \
	%D%/spl/rpc/types.h \
	%D%/spl/rpc/xdr.h

kernel_spldir = $(kerneldir)/spl
kernel_spl_HEADERS = \
	%D%/spl/acl.h \
	%D%/spl/atomic.h \
	%D%/spl/byteorder.h \
	%D%/spl/callb.h \
	%D%/spl/callo.h \
	%D%/spl/cmn_err.h \
	%D%/spl/condvar.h \
	%D%/spl/cred.h \
	%D%/spl/ctype.h \
	%D%/spl/debug.h \
	%D%/spl/disp.h \
	%D%/spl/errno.h \
	%D%/spl/fcntl.h \
	%D%/spl/file.h \
	%D%/spl/inttypes.h \
	%D%/spl/isa_defs.h \
	%D%/spl/kmem.h \
	%D%/spl/kmem_cache.h \
	%D%/spl/kstat.h \
	%D%/spl/list.h \
	%D%/spl/misc.h \
	%D%/spl/mod.h \
	%D%/spl/mutex.h \
	%D%/spl/param.h \
	%D%/spl/proc.h \
	%D%/spl/processor.h \
	%D%/spl/procfs_list.h \
	%D%/spl/random.h \
	%D%/spl/rwlock.h \
	%D%/spl/shrinker.h \
	%D%/spl/sid.h \
	%D%/spl/signal.h \
	%D%/spl/simd.h \
	%D%/spl/stat.h \
	%D%/spl/string.h \
	%D%/spl/sunddi.h \
	%D%/spl/sysmacros.h \
	%D%/spl/systeminfo.h \
	%D%/spl/taskq.h \
	%D%/spl/thread.h \
	%D%/spl/time.h \
	%D%/spl/timer.h \
	%D%/spl/trace.h \
	%D%/spl/trace_taskq.h \
	%D%/spl/tsd.h \
	%D%/spl/types.h \
	%D%/spl/types32.h \
	%D%/spl/uio.h \
	%D%/spl/user.h \
	%D%/spl/vfs.h \
	%D%/spl/vmem.h \
	%D%/spl/vmsystm.h \
	%D%/spl/vnode.h \
	%D%/spl/wait.h \
	%D%/spl/wmsum.h \
	%D%/spl/zmod.h \
	%D%/spl/zone.h

kernel_spl_ia32dir = $(kernel_spldir)/ia32
kernel_spl_ia32_HEADERS = \
	%D%/spl/ia32/asm_linkage.h
endif
