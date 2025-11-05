include $(srcdir)/%D%/include/Makefile.am

#
# we build two variants of the libspl convenience archive:
#
# - libspl.la, for direct use by internal programs and libraries
#
# - libzpl_reexport.la, for use by shared libraries that want to reexport
#   libspl's symbols to consumers
#
# this is necessary because -fvisibility=hidden is a link-time option, and
# libtool will "remember" it and apply it when creating an ELF object
# incorporating the convenience archive. we want -fvisibility=hidden for
# most shared objects that use libspl but don't export its API, however
# libzpool.so provides the complete API by design, and so -fvisibility=hidden
# should not be applied there.
#
# almost always, libspl.la is the right choice.
#

libspl_CFLAGS = $(AM_CFLAGS) $(LIBRARY_CFLAGS) $(LIBUNWIND_CFLAGS)
if TARGET_CPU_I386
libspl_CFLAGS += $(NO_ATOMIC_ALIGNMENT)
endif

libspl_la_CFLAGS = $(libspl_CFLAGS) -fvisibility=hidden
libspl_reexport = $(libspl_CFLAGS)

noinst_LTLIBRARIES += libspl.la libspl_reexport.la
CPPCHECKTARGETS    += libspl.la

libspl_la_SOURCES = \
	%D%/libspl_impl.h \
	%D%/assert.c \
	%D%/atomic.c \
	%D%/backtrace.c \
	%D%/condvar.c \
	%D%/cred.c \
	%D%/getexecname.c \
	%D%/kmem.c \
	%D%/kstat.c \
	%D%/libspl.c \
	%D%/list.c \
	%D%/mkdirp.c \
	%D%/mutex.c \
	%D%/page.c \
	%D%/procfs_list.c \
	%D%/random.c \
	%D%/rwlock.c \
	%D%/sid.c \
	%D%/strlcat.c \
	%D%/strlcpy.c \
	%D%/taskq.c \
	%D%/thread.c \
	%D%/timestamp.c \
	%D%/tunables.c \
	%D%/include/sys/list.h \
	%D%/include/sys/list_impl.h

if BUILD_LINUX
libspl_la_SOURCES += \
	%D%/os/linux/getexecname.c \
	%D%/os/linux/gethostid.c \
	%D%/os/linux/getmntany.c \
	%D%/os/linux/zone.c
endif

if BUILD_FREEBSD
libspl_la_SOURCES += \
	%D%/os/freebsd/getexecname.c \
	%D%/os/freebsd/gethostid.c \
	%D%/os/freebsd/getmntany.c \
	%D%/os/freebsd/mnttab.c \
	%D%/os/freebsd/zone.c
endif

libspl_la_LDFLAGS = -pthread

libspl_la_LIBADD  = $(LIBATOMIC_LIBS) $(LIBCLOCK_GETTIME)
libspl_la_LIBADD += $(BACKTRACE_LIBS) $(LIBUNWIND_LIBS)

libspl_reexport_la_SOURCES = $(libspl_la_SOURCES)
libspl_reexport_la_LDFLAGS = $(libspl_la_LDFLAGS)
libspl_reexport_la_LIBADD = $(libspl_la_LIBADD)
