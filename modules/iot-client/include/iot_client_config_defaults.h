/*
 * iot_client_config_defaults.h -- iot-client compile-time configuration,
 * one home: the AGENTIC_KIT_* build-time knobs, the release-managed SDK
 * version data, the per-region ATOP/MQTT service endpoints, and the
 * module's log-facade binding with its small PAL helpers. It absorbed the
 * former src/iot_config_defaults.h, so the module no longer keeps two
 * one-word-apart config headers. Lives beside the module's public headers;
 * module sources include it directly.
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

#include <stdarg.h>
#include <string.h>

#include "log.h"
#include "pal.h"

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

/* =========================================================================
 * iot-client: release-managed version data (tools/bump_version rewrites
 * these; they ride releases, not integrator overrides -- not build knobs)
 * ========================================================================= */

#ifndef IOT_SDK_SW_VER
#define IOT_SDK_SW_VER  "1.0.0"
#endif
#ifndef IOT_SDK_PV
#define IOT_SDK_PV      "2.3"
#endif
#ifndef IOT_SDK_BV
#define IOT_SDK_BV      "2.0"
#endif
#define SDK_VERSION "agentic-kit_0.4.0"

/* =========================================================================
 * iot-client: service endpoints & protocol constants (fixed by the Tuya
 * cloud; not tunable per product)
 * ========================================================================= */

#define IOT_HTTP_TIMEOUT_MS_DEFAULT 5000

/* Default Tuya cloud server configuration */
#define IOT_DEFAULT_HOST "a1.tuyacn.com"
#define IOT_DEFAULT_PRE_HOST "a1-cn.wgine.com"
#define IOT_CN_HOST "a1.tuyacn.com"
#define IOT_CN_PRE_HOST "a1-cn.wgine.com"
#define IOT_AZ_HOST "a1.tuyaus.com"
#define IOT_AZ_PRE_HOST "a1-us.wgine.com"
#define IOT_UEAZ_HOST "a1-ueaz.tuyaus.com"
#define IOT_UEAZ_PRE_HOST "a1-ueaz.wgine.com"
#define IOT_EU_HOST "a1.tuyaeu.com"
#define IOT_EU_PRE_HOST "a1-eu.wgine.com"
#define IOT_WEAZ_HOST "a1-weaz.tuyaeu.com"
#define IOT_WEAZ_PRE_HOST "a1-weaz.wgine.com"
#define IOT_IN_HOST "a1.tuyain.com"
#define IOT_IN_PRE_HOST "a1-in.wgine.com"
#define IOT_SG_HOST "a1-sg.iotbing.com"
#define IOT_TEST_HOST "https://127.0.0.1:8443"

#define IOT_DEFAULT_MQTT_URL "mqtts://a6.tuyacn.com:8883"

#define IOT_DEFAULT_PORT 443

/* =========================================================================
 * iot-client: host-side PAL default (src/iot_pal_defaults.c defines it;
 * each ESP-IDF app provides its own get_default_pal)
 * ========================================================================= */

/**
 * @brief Get the built-in default PAL adapter (POSIX or FreeRTOS).
 * @return Pointer to a static pal_t with all required function pointers set
 */
const pal_t *get_default_pal(void);

/* Module-tagged dispatch into the global log facade.  Tag is folded
 * into the format string at compile time, so the log handler stays
 * tag-agnostic and the call site reads exactly like printf(). */
#define log_error(fmt, ...) log_emit(LOG_ERROR, "[iot] " fmt, ##__VA_ARGS__)
#define log_warn(fmt,  ...) log_emit(LOG_WARN,  "[iot] " fmt, ##__VA_ARGS__)
#define log_info(fmt,  ...) log_emit(LOG_INFO,  "[iot] " fmt, ##__VA_ARGS__)
#define log_debug(fmt, ...) log_emit(LOG_DEBUG, "[iot] " fmt, ##__VA_ARGS__)

/**
 * @brief Duplicate a string using PAL's allocator.
 *
 * @param pal  PAL adapter providing malloc
 * @param str  Source string (NULL returns NULL)
 * @return Newly allocated copy, or NULL on failure. Caller must free via pal->free.
 */
static inline char *pal_strdup(const pal_t *pal, const char *str)
{
    if (!str) return NULL;
    size_t len = strlen(str) + 1;
    char *copy = (char *)pal->malloc(len);
    if (copy) memcpy(copy, str, len);
    return copy;
}

#endif /* AGENTIC_KIT_IOT_CLIENT_CONFIG_DEFAULTS_H */
