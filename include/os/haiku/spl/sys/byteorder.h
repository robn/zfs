#ifndef _SPL_BYTEORDER_H
#define	_SPL_BYTEORDER_H

#include <ByteOrder.h>

#define	htonl(x)	B_HOST_TO_BENDIAN_INT32(x)
#define	htonll(x)	B_HOST_TO_BENDIAN_INT64(x)

#define ntohll(x)	B_BENDIAN_TO_HOST_INT64(x)

#define BE_32(x)	B_HOST_TO_BENDIAN_INT32(x)
#define BE_64(x)	B_HOST_TO_BENDIAN_INT64(x)

#endif
