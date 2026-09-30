#ifndef _SPL_CRED_H
#define	_SPL_CRED_H

/*
 * XXX haiku does have per-process ("team") credentials, but no discrete
 *     representation of the current calling credential. nbd; we'll just
 *     need to build our own that captures the necessary pieces on demand.
 */
typedef uintptr_t cred_t;

#endif
