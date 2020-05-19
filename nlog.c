#include <syslog.h>
#include <stdarg.h>
#include <stdlib.h>

#include "nlog.h"

static int nlog_dbg;

void nlog_init(void)
{
	if (getenv("NLOG_DEBUG"))
		nlog_dbg = 1;
}

void nlog(int lvl, const char *frmt, ...)
{
	va_list args;

	if (lvl == LOG_DEBUG && !nlog_dbg)
		return;

	va_start(args, frmt);

	vsyslog(lvl, frmt, args);

	va_end(args);
}
