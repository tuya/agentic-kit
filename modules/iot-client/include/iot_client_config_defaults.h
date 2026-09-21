/*
 * iot_client_config_defaults.h -- iot-client build-time knobs: the
 * AGENTIC_KIT_* #ifndef defaults, nothing else. Non-knob module data --
 * release-managed version strings, the per-region ATOP/MQTT endpoints, the
 * log-facade binding with its PAL helpers -- lives in src/iot_internal.h
 * (which pulls this file in), mirroring tai_config_defaults.h's split.
 *
 * It pulls common/log.h FIRST: that header is where integrator overrides
 * (agentic_kit_config.h on the include path, -D, AGENTIC_KIT_USER_CONFIG)
 * are applied -- so overrides win over every knob below. Not a public
 * API header.
 *
 * @copyright Copyright (c) 2021-2025 Tuya Inc. All Rights Reserved.
 */

#ifndef AGENTIC_KIT_IOT_CLIENT_CONFIG_DEFAULTS_H
#define AGENTIC_KIT_IOT_CLIENT_CONFIG_DEFAULTS_H

#include "log.h"

/* =========================================================================
 * iot-client: logging (src/iot_internal.h)
 * ========================================================================= */

/* Optional per-module log ceiling. Defaults to the SDK-wide ceiling and can
 * only LOWER iot-client below it: the effective ceiling is the smaller of
 * the two, so a value above AGENTIC_KIT_LOG_LEVEL is clamped to it right
 * below. Typical use: keep the SDK-wide ceiling open for debugging while
 * silencing one chatty module. Gates the IOT_LOG* vocabulary where it is
 * defined (src/iot_internal.h). */
#ifndef AGENTIC_KIT_IOT_LOG_LEVEL
#define AGENTIC_KIT_IOT_LOG_LEVEL AGENTIC_KIT_LOG_LEVEL
#endif
/* Lower-only, enforced ONCE here so any future block-level gate keyed on
 * this knob sees the effective ceiling; the vocabulary itself already
 * collapses at the facade, the clamp keeps the knob value honest. */
#if AGENTIC_KIT_IOT_LOG_LEVEL > AGENTIC_KIT_LOG_LEVEL
#undef AGENTIC_KIT_IOT_LOG_LEVEL
#define AGENTIC_KIT_IOT_LOG_LEVEL AGENTIC_KIT_LOG_LEVEL
#endif

/* =========================================================================
 * iot-client: MQTT transport (modules/iot-client/src/mqtt.c)
 * ========================================================================= */

/* coreMQTT fixed network buffer for one packet (CONNECT/SUBSCRIBE/PUBLISH).
 * PAL-allocated, so raising it lands in PSRAM on targets that route large
 * allocations there. The DP publish gate (DP_MQTT_MAX_PAYLOAD in iot_dp.c)
 * derives from this, so it follows automatically. */
#ifndef AGENTIC_KIT_MQTT_MAX_PACKET_SIZE
#define AGENTIC_KIT_MQTT_MAX_PACKET_SIZE 4096
#endif

/* Per transport-write send timeout, handed to the shared TLS/TCP layer. */
#ifndef AGENTIC_KIT_MQTT_SEND_TIMEOUT_MS
#define AGENTIC_KIT_MQTT_SEND_TIMEOUT_MS 2000U
#endif

/* Receive poll timeout driving the MQTT process loop; a receive that gets
 * no bytes within it polls again rather than failing. */
#ifndef AGENTIC_KIT_MQTT_RECV_TIMEOUT_MS
#define AGENTIC_KIT_MQTT_RECV_TIMEOUT_MS 1000U
#endif

/* Whole CONNECT handshake budget (TCP + TLS + MQTT CONNECT). */
#ifndef AGENTIC_KIT_MQTT_CONNECT_TIMEOUT_MS
#define AGENTIC_KIT_MQTT_CONNECT_TIMEOUT_MS 10000U
#endif

/* =========================================================================
 * iot-client: ATOP-over-HTTP transport (http_client_interface.c)
 * ========================================================================= */

/* Assembled ATOP request headers (request line + headers), one buffer. */
#ifndef AGENTIC_KIT_REQUEST_HEADER_BUFFER_SIZE
#define AGENTIC_KIT_REQUEST_HEADER_BUFFER_SIZE 1024
#endif

/* Status line + response headers + the ENCRYPTED response body share this
 * single fixed buffer -- there is no streamed/continued read. A larger
 * response makes coreHTTP return HTTPInsufficientMemory, which the SDK
 * folds into OPRT_COMMUNICATION_ERROR (indistinguishable from a broken
 * socket at the call site). This is not hypothetical: a product hit it in
 * production with contentLength 6558 against the 4096 default, on an
 * activation that had already SUCCEEDED server-side. Both buffers come
 * from the PAL allocator, so raising this lands in PSRAM where routed.
 * Net of headers, base64 expansion and the ATOP envelope, the decrypted
 * JSON payload cap is roughly 2.8 KB -- see
 * docs-site/docs/guides/atop-generic-call.md. */
#ifndef AGENTIC_KIT_RESPONSE_BUFFER_SIZE
#define AGENTIC_KIT_RESPONSE_BUFFER_SIZE 4096
#endif

#endif /* AGENTIC_KIT_IOT_CLIENT_CONFIG_DEFAULTS_H */
