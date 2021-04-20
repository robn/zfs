#ifndef _SYS_CCOMPILE_H
#define _SYS_CCOMPILE_H

#define	EREMOTEIO	EREMOTE
#define	ECKSUM		EBADMSG
#define	ENOTACTIVE	ECANCELED

#define	P2PHASE(x, align)		((x) & ((align) - 1))

#endif	/* _SYS_CCOMPILE_H */
