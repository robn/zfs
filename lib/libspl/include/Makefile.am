# SPDX-License-Identifier: CDDL-1.0
libspldir = $(includedir)/libspl
libspl_HEADERS = \
	%D%/assert.h \
	%D%/atomic.h \
	%D%/libgen.h \
	%D%/libspl.h \
	%D%/statcommon.h \
	%D%/stdlib.h \
	%D%/string.h \
	%D%/umem.h \
	%D%/unistd.h \
	%D%/zone.h

if BUILD_FREEBSD
libspl_HEADERS += \
	%D%/os/freebsd/fcntl.h
endif


libspl_rpcdir = $(libspldir)/rpc
libspl_rpc_HEADERS = \
	%D%/rpc/xdr.h


libspl_spldir = $(libspldir)/spl
libspl_spl_HEADERS = \
	%D%/spl/acl.h \
	%D%/spl/acl_impl.h \
	%D%/spl/asm_linkage.h \
	%D%/spl/backtrace.h \
	%D%/spl/callb.h \
	%D%/spl/cmn_err.h \
	%D%/spl/condvar.h \
	%D%/spl/cred.h \
	%D%/spl/debug.h \
	%D%/spl/dkio.h \
	%D%/spl/dklabel.h \
	%D%/spl/feature_tests.h \
	%D%/spl/inttypes.h \
	%D%/spl/isa_defs.h \
	%D%/spl/kmem.h \
	%D%/spl/kstat.h \
	%D%/spl/list.h \
	%D%/spl/list_impl.h \
	%D%/spl/misc.h \
	%D%/spl/mhd.h \
	%D%/spl/mkdev.h \
	%D%/spl/mod.h \
	%D%/spl/mutex.h \
	%D%/spl/poll.h \
	%D%/spl/processor.h \
	%D%/spl/procfs_list.h \
	%D%/spl/random.h \
	%D%/spl/rwlock.h \
	%D%/spl/sid.h \
	%D%/spl/simd.h \
	%D%/spl/stack.h \
	%D%/spl/stdtypes.h \
	%D%/spl/string.h \
	%D%/spl/sunddi.h \
	%D%/spl/sysmacros.h \
	%D%/spl/systeminfo.h \
	%D%/spl/systm.h \
	%D%/spl/thread.h \
	%D%/spl/taskq.h \
	%D%/spl/time.h \
	%D%/spl/timer.h \
	%D%/spl/trace.h \
	%D%/spl/tsd.h \
	%D%/spl/tunables.h \
	%D%/spl/types.h \
	%D%/spl/types32.h \
	%D%/spl/uio.h \
	%D%/spl/vnode.h \
	%D%/spl/wmsum.h \
	%D%/spl/zone.h

libspl_ia32dir = $(libspl_spldir)/ia32

if BUILD_LINUX
libspl_spl_HEADERS += \
	%D%/os/linux/spl/byteorder.h \
	%D%/os/linux/spl/errno.h \
	%D%/os/linux/spl/mnttab.h \
	%D%/os/linux/spl/mount.h \
	%D%/os/linux/spl/param.h \
	%D%/os/linux/spl/stat.h \
	%D%/os/linux/spl/vfs.h

libspl_ia32_HEADERS = \
	%D%/os/linux/spl/ia32/asm_linkage.h
endif

if BUILD_FREEBSD
libspl_spl_HEADERS += \
	%D%/os/freebsd/spl/byteorder.h \
	%D%/os/freebsd/spl/fcntl.h \
	%D%/os/freebsd/spl/file.h \
	%D%/os/freebsd/spl/mnttab.h \
	%D%/os/freebsd/spl/mount.h \
	%D%/os/freebsd/spl/param.h \
	%D%/os/freebsd/spl/stat.h \
	%D%/os/freebsd/spl/vfs.h

libspl_ia32_HEADERS = \
	%D%/os/freebsd/spl/ia32/asm_linkage.h
endif


libspl_dktpdir = $(libspl_spldir)/dktp
libspl_dktp_HEADERS = \
	%D%/spl/dktp/fdisk.h

