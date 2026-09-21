/*
 * log.c -- Default implementation of the log facade.
 *
 * Stateless by design: no handler pointer to swap, no level to set,
 * nothing mutable at runtime. The destination is a compile-time fact
 * (the AGENTIC_KIT_LOG remap in log.h); this file is the default one.
 */

#include "log.h"

#include <stdarg.h>
#include <stdio.h>
#include <time.h>

/* The default destination: "HH:MM:SS [L] [tag] msg" to stderr (the tag
 * is already folded into fmt by the macro layer).  No intermediate
 * buffer: format directly to stderr.  Take stderr's stdio lock so the
 * prefix + body + newline emit as one atomic line even when worker
 * threads log concurrently. */
static void default_output(log_level_t level, const char *fmt, va_list args)
{
    static const char *L[] = { "-", "E", "W", "I", "D" };
    const char *lc = (level >= LOG_ERROR && level <= LOG_DEBUG) ? L[level] : "?";

    char ts[16];
    time_t t = time(NULL);
    struct tm tm_buf;
#if defined(_WIN32)
    localtime_s(&tm_buf, &t);
#else
    localtime_r(&t, &tm_buf);
#endif
    strftime(ts, sizeof ts, "%H:%M:%S", &tm_buf);

#if defined(_WIN32)
    _lock_file(stderr);
#else
    flockfile(stderr);
#endif
    fprintf(stderr, "%s [%s] ", ts, lc);
    vfprintf(stderr, fmt ? fmt : "", args);
    fputc('\n', stderr);
#if defined(_WIN32)
    _unlock_file(stderr);
#else
    funlockfile(stderr);
#endif
}

void log_emit_valist(log_level_t level, const char *fmt, va_list args)
{
    default_output(level, fmt, args);
}

void log_emit(log_level_t level, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    default_output(level, fmt, args);
    va_end(args);
}
