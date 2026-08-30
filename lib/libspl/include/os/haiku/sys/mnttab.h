/*
 * HAIKU PORTING NOTES:
 * - I don't even want to think about mounts yet. probably this will be stubs
 */

#ifndef _SYS_MNTTAB_H
#define	_SYS_MNTTAB_H

#include <sys/stat.h>
#include <sys/param.h> /* stat64 */

#define	MNTTAB		"/dev/zero"
#define	MNT_LINE_MAX	4108

struct mnttab {
	char *mnt_special;
	char *mnt_mountp;
	char *mnt_fstype;
	char *mnt_mntopts;
};

extern int getextmntent(const char *path, struct mnttab *entry,
    struct stat64 *statbuf);
extern char *hasmntopt(struct mnttab *mnt, const char *opt);
extern int getmntent(FILE *fp, struct mnttab *mp);

#endif
