#include <stdio.h>
#include <stdlib.h>
#include <syslog.h>
#include <stdarg.h>
#include <clover_utils.h>
#include <clover_log.h>

#ifdef DEBUG
static s32 debug_level = CLV_DEBUG;
#else
static s32 debug_level = CLV_WARNING;
#endif

void clv_log(s32 priority, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	if (priority <= debug_level)
		vprintf(fmt, ap);
	va_end(ap);
}

