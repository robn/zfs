/*
 * HAIKU PORTING NOTES:
 * - no-op stubs, won't hurt if they stay that way.
 */

#include <libzutil.h>

void
zfs_setproctitle_init(int argc, char *argv[], char *envp[])
{
	(void) argc, (void) argv, (void) envp;
}

void
zfs_setproctitle(const char *fmt, ...)
{
	(void) fmt;
}
