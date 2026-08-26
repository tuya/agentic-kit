/**
 * @file core_http_config.h
 * @brief Route coreHTTP's internal Error/Warn logging into the SDK log facade.
 *
 * The counterpart to core_mqtt_config.h, for the same reason: without it
 * coreHTTP's `Log*` macros expand to nothing (core_http_config_defaults.h) and
 * its own account of a failure never reaches anyone. That matters most on the
 * ATOP path, which is the fallback diagnosis the MQTT refusal docs point at —
 * when a device cannot connect, ATOP over HTTP is how you ask the cloud whether
 * the credentials are still valid, and it was the half with no diagnostics.
 *
 * Only Error and Warn are routed, as with coreMQTT: LogDebug fires per response
 * chunk, so wiring it would cost flash on an embedded target and bury the
 * useful lines. LogInfo/LogDebug stay compiled out, leaving this a
 * diagnostics-only change with nothing added to the hot path.
 */

#ifndef CORE_HTTP_CONFIG_H
#define CORE_HTTP_CONFIG_H

#include "log.h"

/* coreHTTP invokes these with a doubly-parenthesised argument --
 * LogError( ( "fmt", args ) ) -- so `message` arrives complete with its own
 * parentheses, which then serve as the call parentheses of the macro below. */
#define CORE_HTTP_LOG_ERROR( ... ) log_emit(LOG_ERROR, "[http] " __VA_ARGS__)
#define CORE_HTTP_LOG_WARN( ... )  log_emit(LOG_WARN,  "[http] " __VA_ARGS__)

/* #undef first, mirroring core_mqtt_config.h: whichever of the two vendored
 * config headers a translation unit reaches second must win rather than warn.
 * The two libraries compile separately, so in practice each sees only its own
 * prefix; this only keeps a TU that includes both headers well-defined. */
#undef LogError
#undef LogWarn
#define LogError( message ) CORE_HTTP_LOG_ERROR message
#define LogWarn( message )  CORE_HTTP_LOG_WARN message

/* LogInfo / LogDebug deliberately left undefined -- core_http_config_defaults.h
 * guards each with #ifndef and falls back to an empty definition. */

#endif /* CORE_HTTP_CONFIG_H */
