#ifndef BIZ_SERVICE_INTERNAL_H
#define BIZ_SERVICE_INTERNAL_H
#include "biz_client_config_defaults.h"
#include "tuya_biz_client.h"
#include "iot_atop.h"
#include "cJSON.h"

#if AGENTIC_KIT_BIZ_LOG_LEVEL >= LOG_ERROR
#define BIZ_LOGE(...) log_tag_error("biz", __VA_ARGS__)
#else
#define BIZ_LOGE(...) ((void)0)
#endif
#if AGENTIC_KIT_BIZ_LOG_LEVEL >= LOG_WARN
#define BIZ_LOGW(...) log_tag_warn("biz", __VA_ARGS__)
#else
#define BIZ_LOGW(...) ((void)0)
#endif
#if AGENTIC_KIT_BIZ_LOG_LEVEL >= LOG_INFO
#define BIZ_LOGI(...) log_tag_info("biz", __VA_ARGS__)
#else
#define BIZ_LOGI(...) ((void)0)
#endif
#if AGENTIC_KIT_BIZ_LOG_LEVEL >= LOG_DEBUG
#define BIZ_LOGD(...) log_tag_debug("biz", __VA_ARGS__)
#else
#define BIZ_LOGD(...) ((void)0)
#endif

struct tuya_biz_client {
    iot_client_t *iot;
    const pal_t *pal;
    char request_lang[32];
};

int biz_prepare(tuya_biz_client_t *, iot_atop_rejection_t *);
cJSON *biz_query_body(const tuya_biz_client_t *);
/* Consumes body on ALL paths. Returns a caller-owned parsed result. */
int biz_call(tuya_biz_client_t *, const char *api, cJSON *body,
              cJSON **result, iot_atop_rejection_t *);
int biz_copy_string(const cJSON *, const char *, char *, size_t, bool required);
#endif
