/* LAN_LOG* — the tuya-lan module's log family on the shared facade, named
 * after the module-prefix convention of the other modules (IOT_LOG*, TAI_LOG*).
 * Ceilings: the SDK-wide AGENTIC_KIT_LOG_LEVEL gates once in log.h;
 * AGENTIC_KIT_LAN_LOG_LEVEL (defaults to that; tuya_lan_config_defaults.h)
 * optionally lowers just this module further -- never raises. */
#ifndef TUYA_LAN_LOG_H
#define TUYA_LAN_LOG_H

#include "tuya_lan_config_defaults.h"

#if AGENTIC_KIT_LAN_LOG_LEVEL >= 1
#define LAN_LOGE(fmt, ...) log_tag_error("lan", fmt, ##__VA_ARGS__)
#else
#define LAN_LOGE(...) ((void)0)
#endif
#if AGENTIC_KIT_LAN_LOG_LEVEL >= 2
#define LAN_LOGW(fmt, ...) log_tag_warn("lan", fmt, ##__VA_ARGS__)
#else
#define LAN_LOGW(...) ((void)0)
#endif
#if AGENTIC_KIT_LAN_LOG_LEVEL >= 3
#define LAN_LOGI(fmt, ...) log_tag_info("lan", fmt, ##__VA_ARGS__)
#else
#define LAN_LOGI(...) ((void)0)
#endif
#if AGENTIC_KIT_LAN_LOG_LEVEL >= 4
#define LAN_LOGD(fmt, ...) log_tag_debug("lan", fmt, ##__VA_ARGS__)
#else
#define LAN_LOGD(...) ((void)0)
#endif

#endif /* TUYA_LAN_LOG_H */
