/*
 * test_log.c -- the test build's log destination. See test_log.h.
 *
 * The dispatch itself is decided where it should be: in
 * log_config/agentic_kit_config.h, at compile time. This file is just
 * the target it names -- one plain variadic function with a mode.
 *
 * Twin: modules/iot-client/test/test_log.c. The two differ only in
 * this banner and the tai variant's test_log_env_default() -- keep the
 * sink bodies in sync.
 */
#include "test_log.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static test_log_mode_t g_mode = TEST_LOG_PASSTHROUGH;

static char *g_cap;
static size_t g_cap_left;
static test_log_mode_t g_saved_mode = TEST_LOG_PASSTHROUGH;

static unsigned g_count;

void test_log_set_mode(test_log_mode_t mode)
{
    g_mode = mode;
}

void test_log_capture_begin(char *buf, size_t cap)
{
    g_saved_mode = g_mode;
    g_cap = buf;
    g_cap_left = cap;
    buf[0] = '\0';
    g_mode = TEST_LOG_CAPTURE;
}

void test_log_capture_end(void)
{
    g_mode = g_saved_mode;
}

void test_log_count_reset(void)
{
    g_count = 0;
}

unsigned test_log_count_get(void)
{
    return g_count;
}

void test_log_env_default(void)
{
    const char *v = getenv("TAI_LOOPBACK_VERBOSE");
    g_mode = (v && v[0] && v[0] != '0') ? TEST_LOG_PASSTHROUGH : TEST_LOG_QUIET;
}

/* Rebuild the tagged format the macro layer normally folds together
 * ("[tag] " + fmt): captured lines keep the "[tag] " prefix tests
 * strstr() for, and the va_list stays unconsumed for the caller. */
static void build_tagged(char *out, size_t cap, const char *tag, const char *fmt)
{
    snprintf(out, cap, "[%s] %s", tag ? tag : "", fmt ? fmt : "");
}

void test_log_sink(log_level_t level, const char *tag, const char *fmt, ...)
{
    va_list args;

    switch (g_mode) {
    case TEST_LOG_QUIET:
        return;
    case TEST_LOG_COUNT:
        g_count++;
        return;
    case TEST_LOG_CAPTURE: {
        /* va_copy discipline: vsnprintf() below consumes the va_list, so
         * it gets a copy -- the passthrough afterwards needs the original
         * (a consumed va_list re-vfprintf()ed is UB, C11 7.16.1; it
         * happened to work on macOS/arm64 and produced parameter-shifted
         * garbage on Linux/x86-64). vsnprintf returns the length it
         * WOULD have written, so it exceeds the buffer on a truncated
         * message: clamp before memcpy or it reads past `line`. The
         * buffers must stay comfortably larger than any fmt template:
         * build_tagged() truncates BEFORE formatting, so an overlong
         * template would silently lose its tail here (routed templates
         * do get long -- one coreHTTP parse error interpolates up to a
         * whole response buffer). */
        va_start(args, fmt);
        char tagged[1024];
        build_tagged(tagged, sizeof tagged, tag, fmt);
        va_list copy;
        va_copy(copy, args);
        char line[1024];
        int n = vsnprintf(line, sizeof line, tagged, copy);
        va_end(copy);
        size_t len = (n > 0 && (size_t)n < sizeof line) ? (size_t)n : sizeof line - 1;
        if (n > 0 && g_cap && len + 2 < g_cap_left) {
            memcpy(g_cap, line, len);
            g_cap += len;
            *g_cap++ = '\n';
            *g_cap = '\0';
            g_cap_left -= len + 1;
        }
        /* Keep the default output too when the window opened on
         * passthrough, so a failing run stays readable; a window opened
         * on quiet (a suite that opted into silence) stays silent. */
        if (g_saved_mode == TEST_LOG_PASSTHROUGH) {
            log_emit_valist(level, tagged, args);
        }
        va_end(args);
        return;
    }
    default:
        break;
    }

    va_start(args, fmt);
    char tagged[512];
    build_tagged(tagged, sizeof tagged, tag, fmt);
    log_emit_valist(level, tagged, args);
    va_end(args);
}
