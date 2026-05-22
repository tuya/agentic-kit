/*
 * log.c -- Default implementation of the log facade.
 *
 * State (handler + runtime level) is kept in plain globals.  Updates are
 * unsynchronised: setters are expected to run during startup before worker
 * threads emit logs.  Read-side races are harmless on word-sized loads.
 */

#include "log.h"

#include <stdarg.h>
#include <stdio.h>
#include <time.h>

static log_fn_t    g_handler = NULL;          /* NULL => default */
static log_level_t g_level   = LOG_INFO;      /* runtime ceiling */

void log_set_handler(log_fn_t fn)
{
    g_handler = fn;
}

void log_set_level(log_level_t level)
{
    if (level < LOG_NONE)  level = LOG_NONE;
    if (level > LOG_DEBUG) level = LOG_DEBUG;
    g_level = level;
}

log_level_t log_get_level(void)
{
    return g_level;
}

void log_default_handler(log_level_t level, const char *fmt, va_list args)
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

    /* No intermediate buffer: format directly to stderr.  Take stderr's
     * stdio lock so the prefix + body + newline emit as one atomic line
     * even when worker threads log concurrently. */
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

void log_emit(log_level_t level, const char *fmt, ...)
{
    if (level > g_level)
        return;

    log_fn_t handler = g_handler ? g_handler : log_default_handler;

    va_list args;
    va_start(args, fmt);
    handler(level, fmt, args);
    va_end(args);
}
