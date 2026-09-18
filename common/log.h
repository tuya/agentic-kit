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

/* -------------------------------------------------------------------------
 * Build-time config: the integrator-override pickup + the SDK-wide log
 * ceiling. Every SDK subsystem keeps its knob defaults in its own
 * *_config_defaults.h (modules/<m>/include/, pal/pal_config_defaults.h);
 * the one thing they all share is this header, because every SDK
 * translation unit includes it.  That makes log.h the right home for the
 * override pickup: it must run BEFORE any #ifndef knob default, whichever
 * file that default lives in -- and each defaults file includes this
 * header first for exactly that reason.
 *
 * Integrators override by defining a knob FIRST -- pick whichever fits the
 * build system (all of them must apply to every target that compiles SDK
 * sources, so no translation unit sees a different value):
 *   1. Create your own agentic_kit_config.h holding only the knobs you
 *      want to change (plain #define, no #ifndef), and put its directory
 *      on the include path (-I / target_include_directories). It is
 *      picked up automatically, before every default.
 *   2. Add -D<NAME>=<value> to the compile options.
 *   3. -DAGENTIC_KIT_USER_CONFIG='"my_kit_opts.h"' names an override
 *      header with an arbitrary file name (its directory still needs to
 *      be on the include path) -- for toolchains without __has_include.
 *      When set, it wins and the agentic_kit_config.h search is skipped.
 *
 * Why the SDK never owns a file named agentic_kit_config.h: a quoted
 * include (and __has_include with quotes) searches the includer's own
 * directory (common/) BEFORE the -I path, so an SDK-owned
 * common/agentic_kit_config.h could never be shadowed by an integrator's
 * same-named file.  Reserving that name for the integrator is the whole
 * trick (lwIP lwipopts.h / mbedTLS mbedtls_config.h / FreeRTOS
 * FreeRTOSConfig.h pattern).
 *
 * All SDK knobs are prefixed AGENTIC_KIT_ to keep them out of the
 * integrator's namespace (coreMQTT's own core_mqtt_config_defaults.h
 * defines a same-named MQTT_SEND_TIMEOUT_MS that collided for real).
 * Tables, per-knob rationale and migration live in
 * docs-site/docs/guides/compile-time-knobs.md.
 *
 * The pickup runs BEFORE the extern "C" opener: it pulls in the
 * integrator's override header, and a C++ translation unit's config must
 * not silently acquire C linkage.
 * ------------------------------------------------------------------------- */

/* Integrator overrides come first, so every #ifndef knob default -- here
 * and in every *_config_defaults.h -- loses to them. */
#ifdef AGENTIC_KIT_USER_CONFIG
#include AGENTIC_KIT_USER_CONFIG
#else
#if defined(__has_include)
#if __has_include("agentic_kit_config.h")
#include "agentic_kit_config.h"
#endif
#endif
#endif

/* The single compile-time log ceiling for the whole SDK: every log macro
 * (log_tag_* below; iot-client's log_error family, TAI_LOG* and
 * TUYA_BLE_HAL_LOG* re-tagged on top of those) compiles out above it --
 * no call, no argument evaluation, no format string in the image.
 *   0 = none, 1 = error, 2 = +warn, 3 = +info, 4 = +debug (default).
 * Below the ceiling, log_emit()'s runtime filter (log_set_level()) still
 * applies on top. The former per-module TAI_LOG_LEVEL gate is absorbed
 * here: one knob for the whole SDK. */
#ifndef AGENTIC_KIT_LOG_LEVEL
#define AGENTIC_KIT_LOG_LEVEL 4 /* LOG_DEBUG */
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define LOG_NONE   0
#define LOG_ERROR  1
#define LOG_WARN   2
#define LOG_INFO   3
#define LOG_DEBUG  4

/* Compile-time ceiling — the single gate for SDK logging.
 *
 * AGENTIC_KIT_LOG_LEVEL (default 4 = debug) is the maximum level compiled
 * into the SDK. Every log macro the SDK itself uses dispatches through one
 * of the four below — the per-module families (iot-client's log_error,
 * TAI_LOG*, TUYA_BLE_HAL_LOG*) are thin re-tags of these — and expands to
 * ((void)0) above the ceiling: no call, no argument evaluation, no format
 * string in the image. Below the ceiling, log_emit()'s runtime filter
 * (log_set_level()) still applies on top.
 *
 * `tag` must be a string literal (folded into the format so the facade
 * stays tag-agnostic). Direct log_emit() calls bypass the ceiling and are
 * runtime-filtered only — reserved for runtime-chosen levels. */
#if AGENTIC_KIT_LOG_LEVEL >= 1
#define log_tag_error(tag, fmt, ...) log_emit(LOG_ERROR, "[" tag "] " fmt, ##__VA_ARGS__)
#else
#define log_tag_error(tag, fmt, ...) ((void)0)
#endif
#if AGENTIC_KIT_LOG_LEVEL >= 2
#define log_tag_warn(tag, fmt, ...)  log_emit(LOG_WARN,  "[" tag "] " fmt, ##__VA_ARGS__)
#else
#define log_tag_warn(tag, fmt, ...)  ((void)0)
#endif
#if AGENTIC_KIT_LOG_LEVEL >= 3
#define log_tag_info(tag, fmt, ...)  log_emit(LOG_INFO,  "[" tag "] " fmt, ##__VA_ARGS__)
#else
#define log_tag_info(tag, fmt, ...)  ((void)0)
#endif
#if AGENTIC_KIT_LOG_LEVEL >= 4
#define log_tag_debug(tag, fmt, ...) log_emit(LOG_DEBUG, "[" tag "] " fmt, ##__VA_ARGS__)
#else
#define log_tag_debug(tag, fmt, ...) ((void)0)
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
