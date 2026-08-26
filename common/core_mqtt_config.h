/**
 * @file core_mqtt_config.h
 * @brief Route coreMQTT's internal Error/Warn logging into the SDK log facade.
 *
 * coreMQTT was built with MQTT_DO_NOT_USE_CUSTOM_CONFIG, which leaves its
 * `Log*` macros expanding to nothing (core_mqtt_config_defaults.h). That
 * silence has a concrete cost: a broker that refuses a CONNECT surfaces at the
 * call site only as `MQTTServerRefused` (6) — while coreMQTT knew, and threw
 * away, which of the five MQTT 3.1.1 refusal reasons the broker actually sent.
 * `logConnackResponse()` in core_mqtt_serializer.c holds the answer
 * ("bad user name or password", "identifier rejected", "not authorized", …) and
 * logs it through exactly these macros.
 *
 * Only Error and Warn are routed on purpose. LogInfo/LogDebug fire per packet;
 * wiring them would cost flash on an embedded target and bury the useful lines
 * in per-publish chatter. They stay compiled out, exactly as before this file
 * existed, so the only behavioural change here is that coreMQTT's failures
 * become visible.
 */

#ifndef CORE_MQTT_CONFIG_H
#define CORE_MQTT_CONFIG_H

#include "log.h"

/* coreMQTT invokes these with a doubly-parenthesised argument --
 * LogError( ( "fmt", args ) ) -- so `message` arrives complete with its own
 * parentheses, which then serve as the call parentheses of the macro below.
 * Same shape as the log_* wrappers in iot_config_defaults.h. */
#define CORE_MQTT_LOG_ERROR( ... ) log_emit(LOG_ERROR, "[mqtt] " __VA_ARGS__)
#define CORE_MQTT_LOG_WARN( ... )  log_emit(LOG_WARN,  "[mqtt] " __VA_ARGS__)

/* #undef first: coreHTTP's core_http_config_defaults.h defines both as empty
 * under its own #ifndef. A translation unit that reached that header first
 * would otherwise take the empty definition and silently lose these logs (or
 * warn about a redefinition). No SDK source includes both today; this keeps the
 * header order-independent for integrators who do. */
#undef LogError
#undef LogWarn
#define LogError( message ) CORE_MQTT_LOG_ERROR message
#define LogWarn( message )  CORE_MQTT_LOG_WARN message

/* LogInfo / LogDebug deliberately left undefined -- core_mqtt_config_defaults.h
 * guards each with #ifndef and falls back to an empty definition. */

#endif /* CORE_MQTT_CONFIG_H */
