#include <sys/debug.h>
#include <stdio.h>
#include <KernelExport.h>

void
spl_panic(const char *file, const char *func, int line, const char *fmt, ...)
{
	char msg[256]; // XXX MAXMSGLEN?
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(msg, sizeof (msg), fmt, ap);
	va_end(ap);

	panic("PANIC: %s:%d:%s(): %s\n", file, line, func, msg);

	__builtin_unreachable();
}

