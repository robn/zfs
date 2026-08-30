/*
 * HAIKU PORTING NOTES:
 * - stubs for zpool
 */

#include "zpool_util.h"

int
check_device(const char *name, boolean_t force, boolean_t isspare,
    boolean_t iswholedisk)
{
	(void) name, (void) force, (void) isspare, (void) iswholedisk;
	return (ENOSYS);
}

boolean_t
check_sector_size_database(char *path, int *sector_size)
{
	(void) path, (void) sector_size;
	return (B_FALSE);
}

void
after_zpool_upgrade(zpool_handle_t *zhp)
{
	(void) zhp;
}

int
check_file(const char *file, boolean_t force, boolean_t isspare)
{
	return (check_file_generic(file, force, isspare));
}

int
zpool_power_current_state(zpool_handle_t *zhp, char *vdev)
{
	(void) zhp, (void) vdev;
	return (-1);
}

int
zpool_power(zpool_handle_t *zhp, char *vdev, boolean_t turn_on)
{
	(void) zhp, (void) vdev, (void) turn_on;
	return (ENOTSUP);
}
