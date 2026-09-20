/*
 * iot_internal.h -- internal (src-private) definitions for iot-client:
 * everything the module's sources and host tests share that is NOT a
 * build-time knob. It restores, renamed, the former src/iot_config_defaults.h
 * (which had been absorbed into include/iot_client_config_defaults.h) and
 * absorbs the former src/iot_client_internal.h. The AGENTIC_KIT_* knob
 * defaults stay in include/iot_client_config_defaults.h, pulled in below --
 * mirroring tai_internal.h; that file includes common/log.h first, the
 * integrator-override pickup. The public client API lives in
 * include/iot_client.h.
 *
 * @copyright Copyright (c) 2021-2025 Tuya Inc. All Rights Reserved.
 */

#ifndef IOT_INTERNAL_H
#define IOT_INTERNAL_H

#include "iot_client_config_defaults.h"

#include <stdarg.h>
#include <string.h>

#include "iot_client.h"
#include "log.h"
#include "pal.h"

/* =========================================================================
 * iot-client: release-managed version data (tools/bump_version rewrites
 * SDK_VERSION below; these ride releases, not integrator overrides --
 * not build knobs)
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
#ifdef __cplusplus
extern "C" {
#endif

const pal_t *get_default_pal(void);

#ifdef __cplusplus
}
#endif

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

/* =========================================================================
 * iot-client: src-private hooks for the client core, used by iot_client.c
 * and the host test suites.
 * ========================================================================= */

/**
 * @brief Issue the two init-time version reports — SDK meta save and firmware
 *        version update — unless config->skip_version_report is set.
 *
 * Non-fatal by design: each failed report logs a warning and client init
 * proceeds, exactly as when this lived inline in iot_client_init(). Split out
 * so host tests can pin the skip / no-skip contract against the ATOP mock
 * with a hand-shaped client, without the DNS / activation path in front of it
 * (iot_client_init() with a non-empty devid queries the real IoT-DNS host).
 *
 * @return OPRT_OK when both reports were skipped or both succeeded;
 *         OPRT_INVALID_PARAMETER for a NULL client or config;
 *         otherwise the first failure (both reports are still attempted).
 */
int iot_client_report_init_versions(iot_client_t *client,
                                    const iot_client_config_t *config);

#endif /* IOT_INTERNAL_H */
