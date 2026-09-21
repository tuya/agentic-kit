/*
 * log.h -- Process-wide logging facade for agentic-kit.
 *
 * One compile-time log ceiling, one tagged dispatch, one destination
 * decided at build time — shared by ai-tcp, iot-client, tuya-ble, and
 * any caller that wants to plug in. Where lines land is not runtime
 * state: define AGENTIC_KIT_LOG (below) and the dispatch lands in your
 * own macro — no function, no va_list round trip; the default expansion
 * calls log_emit, which writes "HH:MM:SS [L] [tag] msg" to stderr. A
 * sink implemented as a variadic function can route back through
 * log_emit_valist to reuse that output.
 *
 * Module tags (e.g. "[iot] ", "[tai] ") are folded into the format string
 * by per-module wrapper macros, so the dispatch stays tag-agnostic:
 * level + fmt + va_list, mirroring ESP-IDF's esp_log_writev().
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
 * (log_tag_* below; iot-client's IOT_LOG*, TAI_LOG* and
 * TUYA_BLE_HAL_LOG* re-tagged on top of those) compiles out above it --
 * no call, no argument evaluation, no format string in the image.
 *   0 = none, 1 = error, 2 = +warn, 3 = +info, 4 = +debug (default).
 * This ceiling is the only SDK-wide level filter: nothing is held back
 * for runtime, so what compiles in is what emits. Quieting a build is
 * itself a build decision (-DAGENTIC_KIT_LOG_LEVEL=2 for error+warn); so
 * is redirecting or dropping lines wholesale -- define AGENTIC_KIT_LOG
 * (below) and the dispatch lands in your own macro. Optional per-module
 * ceilings (AGENTIC_KIT_IOT_LOG_LEVEL, AGENTIC_KIT_TAI_LOG_LEVEL,
 * AGENTIC_KIT_TUYA_BLE_LOG_LEVEL; defaults = this one, in each module's
 * config file) lower a single module further: they ride the same pickup,
 * gate the module vocabulary where it is defined, and can never raise a
 * line above this ceiling. */
#ifndef AGENTIC_KIT_LOG_LEVEL
#define AGENTIC_KIT_LOG_LEVEL 4 /* LOG_DEBUG */
#endif

/* The compile-time remap point for the SDK's log dispatch. Define
 * AGENTIC_KIT_LOG in your agentic_kit_config.h (or -D it, like any knob)
 * and every log line dispatches into YOUR macro instead of the facade's
 * log_emit — macro-to-macro, which is what bridges into macro-based
 * logging systems (ESP-IDF's ESP_LOGx, Zephyr's LOG_*) with no
 * va_list -> buffer -> "%s" round trip. You receive the level
 * (LOG_ERROR..LOG_DEBUG), the bare module-tag literal ("iot", "ble",
 * "mqtt", "http", "tls", "rng", "pal", ...; rtc-tcp-client passes
 * per-source-file tags such as "client" and "pkt"), a printf format
 * and its arguments — the tag as its own token is what makes
 * structured sinks and per-tag filtering possible.
 *
 * Two rules, binding both directions:
 *   - the ceiling above still gates: log_tag_* collapse to ((void)0) at
 *     levels above it, so a remap cannot resurrect compiled-out lines;
 *   - the remap names the destination for EVERY translation unit of the
 *     build — the same iron rule as the knobs (apply it to all targets
 *     that compile SDK sources). There is no runtime dispatch left to
 *     fall back on and no second switch: what compiles in is what prints.
 * The default expansion keeps log_emit's format(printf) checking; your
 * own macro opts out unless you re-add the attribute.
 */
#ifndef AGENTIC_KIT_LOG
#define AGENTIC_KIT_LOG(level, tag, fmt, ...) \
    log_emit(level, "[" tag "] " fmt, ##__VA_ARGS__)
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
 * of the four below — the per-module families (iot-client's IOT_LOG*,
 * TAI_LOG*, TUYA_BLE_HAL_LOG*) are thin re-tags of these — and expands to
 * ((void)0) above the ceiling: no call, no argument evaluation, no format
 * string in the image. Below the ceiling the line emits unconditionally —
 * there is no runtime level, so what compiles in is what prints.
 *
 * `tag` must be a string literal (folded into the format so the facade
 * stays tag-agnostic). Don't call log_emit() directly: it bypasses the
 * ceiling (CI rejects new raw call sites); runtime-chosen levels dispatch
 * over these macros instead, as tai_pkt_log.c does. The dispatch target
 * itself is customer-remappable: AGENTIC_KIT_LOG, above. */
#if AGENTIC_KIT_LOG_LEVEL >= 1
#define log_tag_error(tag, fmt, ...) AGENTIC_KIT_LOG(LOG_ERROR, tag, fmt, ##__VA_ARGS__)
#else
#define log_tag_error(tag, fmt, ...) ((void)0)
#endif
#if AGENTIC_KIT_LOG_LEVEL >= 2
#define log_tag_warn(tag, fmt, ...)  AGENTIC_KIT_LOG(LOG_WARN,  tag, fmt, ##__VA_ARGS__)
#else
#define log_tag_warn(tag, fmt, ...)  ((void)0)
#endif
#if AGENTIC_KIT_LOG_LEVEL >= 3
#define log_tag_info(tag, fmt, ...)  AGENTIC_KIT_LOG(LOG_INFO,  tag, fmt, ##__VA_ARGS__)
#else
#define log_tag_info(tag, fmt, ...)  ((void)0)
#endif
#if AGENTIC_KIT_LOG_LEVEL >= 4
#define log_tag_debug(tag, fmt, ...) AGENTIC_KIT_LOG(LOG_DEBUG, tag, fmt, ##__VA_ARGS__)
#else
#define log_tag_debug(tag, fmt, ...) ((void)0)
#endif

/* Log level type — int-typedef so the LOG_* macros above remain
 * usable in preprocessor `#if` checks (which can't see enum values). */
typedef int log_level_t;

/* va_list entry into the facade (the esp_log_writev pattern): emit a
 * line you already hold as a va_list. This is for a custom destination
 * implemented as a variadic function — it forwards here to reuse the
 * default stderr output instead of re-implementing it. The remap and
 * the log_tag_* macros above are the normal way in; nothing filters
 * here (the ceiling has already applied at the macro layer). */
void log_emit_valist(log_level_t level, const char *fmt, va_list args);

/* Format and dispatch one log line to the default stderr output — the
 * facade's own floor, not an escape hatch around the ceiling (CI
 * rejects raw call sites in modules). */
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
