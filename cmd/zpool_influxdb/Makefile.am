zfsexec_PROGRAMS += zpool_influxdb
CPPCHECKTARGETS  += zpool_influxdb

zpool_influxdb_SOURCES = \
	%D%/zpool_influxdb.c

zpool_influxdb_LDADD = \
	libspl.la \
	libnvpair.la \
	libzfs.la

zpool_influxdb_LDADD += $(JEMALLOC_LIBS)
