/**
 * @file iot_config_defaults.h
 * @brief IoT SDK configuration defaults and logging macros
 *
 * This file provides default configuration values for the IoT SDK library
 * and defines logging macros that can be customized by the application.
 *
 * @copyright Copyright (c) 2021-2025 Tuya Inc. All Rights Reserved.
 */

#ifndef __IOT_CONFIG_DEFAULTS_H__
#define __IOT_CONFIG_DEFAULTS_H__

#include <stdarg.h>
#include <string.h>
#include "iot_client.h"
#include "log.h"

#define IOT_HTTP_TIMEOUT_MS_DEFAULT 5000

// Default Tuya cloud server configuration
#define IOT_DEFAULT_HOST "a1.tuyacn.com"
#define IOT_DEFAULT_PRE_HOST "a1-cn.wgine.com"
#define IOT_CN_HOST "a1.tuyacn.com"
#define IOT_CN_PRE_HOST "a1-cn.wgine.com"
#define IOT_US_HOST "a1.tuyaus.com"
#define IOT_US_PRE_HOST "a1-us.wgine.com"
#define IOT_UEAZ_HOST "a1-ueaz.tuyaeu.com"
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

#define IOT_SDK_SW_VER  "1.0.0"
#define IOT_SDK_PV      "2.3"
#define IOT_SDK_BV      "2.0"
#define SDK_VERSION "agentic-kit_0.1.0"

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

#endif /* ifndef __IOT_CONFIG_DEFAULTS_H__ */
