/*
 * HAIKU PORTING NOTES:
 * - this appears to be the official way to do it. I did a thing!
 * - I wonder why this is hidden, also how often do we need it?
 */

#include <OS.h>
#include <image.h>
#include <string.h>
#include "../../libspl_impl.h"

__attribute__((visibility("hidden"))) ssize_t
getexecname_impl(char *execname)
{
	image_info info;
	int32 cookie = 0;

	while (get_next_image_info(B_CURRENT_TEAM, &cookie, &info) == B_OK) {
		if (info.type == B_APP_IMAGE) {
			strncpy(execname, info.name, PATH_MAX);
			return (strlen(execname));
		}
	}

	return (-1);
}
