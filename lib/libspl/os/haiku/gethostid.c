/*
 * HAIKU PORTING NOTES:
 * - what even is a hostid on this machine?
 */

#include <sys/systeminfo.h>

unsigned long
get_system_hostid(void)
{
	return (0);
}
