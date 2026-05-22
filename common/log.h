/*
 * log.h -- Process-wide logging facade for agentic-kit.
 *
 * One handler, one runtime level, one tagged dispatch — shared by ai-tcp,
 * iot-client, tuya-ble, and any caller that wants to plug in.  The default
 * handler writes "HH:MM:SS [L] msg" to stderr; replace it via
 * log_set_handler() to redirect to syslog, a UART, etc.
 *
 * Module tags (e.g. "[iot] ", "[tai] ") are folded into the format string
 * by per-module wrapper macros, so the handler signature stays minimal:
 * level + fmt + va_list, mirroring ESP-IDF's esp_log_set_vprintf().
 *
 * Levels:
 *   1 = error, 2 = warn, 3 = info, 4 = debug.
 */

#ifndef COMMON_LOG_H
#define COMMON_LOG_H

#include <stdarg.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LOG_NONE   0
#define LOG_ERROR  1
#define LOG_WARN   2
#define LOG_INFO   3
#define LOG_DEBUG  4

#ifndef LOG_LEVEL
#  define LOG_LEVEL LOG_DEBUG  /* compile-time ceiling */
#endif

/* Runtime level type — int-typedef so the LOG_* macros above remain
 * usable in preprocessor `#if` checks (which can't see enum values). */
typedef int log_level_t;

/* Handler signature: receives the (already level-filtered) format string
 * and va_list.  Handlers may vsnprintf into their own buffer or forward
 * to a structured sink.  Implementations must be reentrant or self-locking. */
typedef void (*log_fn_t)(log_level_t level,
                         const char *fmt, va_list args);

/* Replace the active handler.  Pass NULL to restore the default. */
void log_set_handler(log_fn_t fn);

/* Runtime ceiling — messages with level > this are dropped before formatting. */
void        log_set_level(log_level_t level);
log_level_t log_get_level(void);

/* Default handler: writes "HH:MM:SS [L] msg" to stderr.  Holds stderr's
 * stdio lock across the prefix + body + newline so concurrent threads
 * never interleave a single line.  Used as the fallback when no handler
 * has been registered. */
void log_default_handler(log_level_t level,
                         const char *fmt, va_list args);

/* Format and dispatch one log line.  Safe to call before set_handler/set_level. */
#if defined(__GNUC__) || defined(__clang__)
void log_emit(log_level_t level, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));
#else
void log_emit(log_level_t level, const char *fmt, ...);
#endif

#ifdef __cplusplus
}
#endif

#endif /* COMMON_LOG_H */
