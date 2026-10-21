#include "service_internal.h"
#include "log.h"
#include <string.h>
#include <time.h>

bool biz_valid_string(const char *text, size_t capacity)
{
    if (!text || !text[0]) return false;
    /* Bounded even for an unterminated public fixed-size field. */
    for (size_t i = 1; i < capacity; ++i) if (!text[i]) return true;
    return false;
}

int tuya_biz_client_create(iot_client_t *iot, const char *lang, tuya_biz_client_t **out)
{
    if (!out) return OPRT_INVALID_PARAMETER;
    *out = NULL;
    if (!iot || !iot->pal || !iot->pal->malloc || !iot->pal->free ||
        (lang && !biz_valid_string(lang, 32))) return OPRT_INVALID_PARAMETER;
    tuya_biz_client_t *ctx = iot->pal->malloc(sizeof(*ctx));
    if (!ctx) return OPRT_MALLOC_FAILED;
    memset(ctx, 0, sizeof(*ctx));
    ctx->iot = iot;
    ctx->pal = iot->pal;
    strcpy(ctx->request_lang, lang ? lang : "zh");
    *out = ctx;
    return OPRT_OK;
}

void tuya_biz_client_destroy(tuya_biz_client_t *ctx)
{
    if (ctx) ctx->pal->free(ctx);
}

int biz_prepare(tuya_biz_client_t *ctx, iot_atop_rejection_t *rejection)
{
    if (rejection) memset(rejection, 0, sizeof(*rejection));
    return ctx && ctx->iot && ctx->pal ? OPRT_OK : OPRT_INVALID_PARAMETER;
}

cJSON *biz_query_body(const tuya_biz_client_t *ctx)
{
    cJSON *body = cJSON_CreateObject();
    if (!body || !cJSON_AddStringToObject(body, "requestLang", ctx->request_lang)) {
        cJSON_Delete(body);
        return NULL;
    }
    return body;
}

int biz_call(tuya_biz_client_t *ctx, const char *api, cJSON *body,
              cJSON **result, iot_atop_rejection_t *rejection)
{
    *result = NULL;
    if (!body) return OPRT_MALLOC_FAILED;
    time_t now = time(NULL);
    if (now < 1700000000) {
        log_tag_warn("biz", "clock is not synchronized; refusing signed role request");
        cJSON_Delete(body);
        return OPRT_UNINITIALIZED;
    }
    if (!cJSON_AddNumberToObject(body, "t", (double)now)) {
        cJSON_Delete(body);
        return OPRT_MALLOC_FAILED;
    }
    char *text = cJSON_PrintUnformatted(body);
    cJSON_Delete(body);
    if (!text) return OPRT_MALLOC_FAILED;
    iot_atop_request_t request = { .api = api, .version = "1.0", .data = text };
    iot_atop_response_t response = {0};
    int rc = iot_atop_call(ctx->iot, &request, &response);
    cJSON_free(text);
    if (rc == OPRT_ATOP_BUSINESS_ERROR && rejection) {
        memcpy(rejection->code, response.error_code, sizeof(rejection->code));
        memcpy(rejection->msg, response.error_msg, sizeof(rejection->msg));
        rejection->code[sizeof(rejection->code) - 1] = '\0';
        rejection->msg[sizeof(rejection->msg) - 1] = '\0';
    }
    if (rc == OPRT_OK) {
        if (response.result) *result = cJSON_ParseWithOpts(response.result, NULL, 1);
        if (!*result || cJSON_IsNull(*result) || cJSON_IsFalse(*result)) {
            cJSON_Delete(*result);
            *result = NULL;
            rc = OPRT_INVALID_RESULT;
        }
    }
    iot_atop_response_free(ctx->iot, &response);
    if (rc != OPRT_OK) log_tag_warn("biz", "%s failed: %d", api, rc);
    return rc;
}

int biz_copy_string(const cJSON *obj, const char *key, char *out, size_t cap, bool required)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(obj, key);
    out[0] = '\0';
    if (!v || cJSON_IsNull(v)) return required ? OPRT_INVALID_RESULT : OPRT_OK;
    if (!cJSON_IsString(v) || strlen(v->valuestring) >= cap ||
        (required && !v->valuestring[0])) return OPRT_INVALID_RESULT;
    strcpy(out, v->valuestring);
    return OPRT_OK;
}
