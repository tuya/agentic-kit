#ifndef BIZ_SERVICE_INTERNAL_H
#define BIZ_SERVICE_INTERNAL_H
#include "tuya_biz_client.h"
#include "iot_atop.h"
#include "cJSON.h"

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
bool biz_valid_string(const char *, size_t capacity);
#endif
