#ifndef _SPL_KSTAT_H
#define	_SPL_KSTAT_H

typedef struct {
	void		*ks_data;		/* kstat type-specific data */
	uint_t		ks_ndata;		/* # of data records */
	uchar_t		ks_flags;		/* kstat flags */
} kstat_t;

typedef uintptr_t kstat_named_t;

#define	KSTAT_TYPE_RAW		0

#define	KSTAT_FLAG_VIRTUAL	0x01
#define	KSTAT_FLAG_NO_HEADERS	0x80

extern kstat_t *kstat_create(const char *ks_module, int ks_instance,
    const char *ks_name, const char *ks_class, uchar_t ks_type,
    uint_t ks_ndata, uchar_t ks_flags);

extern void kstat_set_raw_ops(kstat_t *ksp,
    int (*headers)(char *buf, size_t size),
    int (*data)(char *buf, size_t size, void *data),
    void* (*addr)(kstat_t *ksp, off_t index));

extern void kstat_install(kstat_t *ksp);
extern void kstat_delete(kstat_t *ksp);

#endif
