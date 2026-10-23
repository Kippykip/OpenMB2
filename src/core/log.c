#include "log.h"

#include <stdarg.h>
#include <stdio.h>

static LogLevel g_min_level = LOG_INFO;

void log_set_level(LogLevel level)
{
    g_min_level = level;
}

void log_message(LogLevel level, const char *fmt, ...)
{
    static const char *const prefixes[] = { "debug", "info", "warn", "error" };
    va_list args;

    if (level < g_min_level)
        return;

    fprintf(stderr, "[%s] ", prefixes[level]);
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fputc('\n', stderr);
    fflush(stderr); /* stderr can be buffered on Windows when redirected */
}
