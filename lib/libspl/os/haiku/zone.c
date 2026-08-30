/*
 * HAIKU PORTING NOTES:
 * - stubs for libzfs
 */

#include <zone.h>

zoneid_t
getzoneid(void)
{
	return (GLOBAL_ZONEID);
}
