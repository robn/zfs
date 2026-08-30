/*
 * HAIKU PORTING NOTES:
 * - stubs for libzfs
 */

#include <stdio.h>
#include <sys/stat.h>
#include <sys/mnttab.h>

int
getmntent(FILE *fp, struct mnttab *mgetp)
{
	(void) fp, (void) mgetp;
	return (ENOSYS);
}

int
getextmntent(const char *path, struct mnttab *entry, struct stat64 *statbuf)
{
	(void) path, (void) entry, (void) statbuf;
	return (ENOSYS);
}

char *
hasmntopt(struct mnttab *mnt, const char *opt)
{
	(void) mnt, (void) opt;
	return (NULL);
}
