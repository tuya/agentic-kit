#include "tuya_ai_role.h"
#include "service_internal.h"
#include "bounded_string.h"
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define ROLE_API "thing.ai.agent.role."

static const cJSON *list_array(const cJSON *root)
{
    if (cJSON_IsArray(root)) return root;
    if (!cJSON_IsObject(root)) return NULL;
    const char *keys[] = {"datas", "list", "records", "rows", "content", "data", "items"};
    for (size_t i = 0; i < sizeof(keys)/sizeof(keys[0]); ++i) {
        const cJSON *v = cJSON_GetObjectItemCaseSensitive(root, keys[i]);
        if (cJSON_IsArray(v)) return v;
    }
    return NULL;
}

static int total_count(const cJSON *root, int *out)
{
    *out = -1;
    if (!cJSON_IsObject(root)) return OPRT_OK;
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(root, "totalCount");
    if (!v) return OPRT_OK;
    if (!cJSON_IsNumber(v) || !isfinite(v->valuedouble) || v->valuedouble < 0 ||
        v->valuedouble > INT_MAX || floor(v->valuedouble) != v->valuedouble) return OPRT_INVALID_RESULT;
    *out = v->valueint;
    return OPRT_OK;
}

typedef int (*list_decoder_t)(const cJSON *, void *, const void *);

typedef struct {
    void *items;
    size_t count;
    int total_count;
} decoded_list_t;

/* Publish only a complete list. The decoder never owns the staging allocation. */
static int query_list(tuya_biz_client_t *ctx, const char *api, cJSON *body,
                      size_t item_size, list_decoder_t decode, const void *arg,
                      bool read_total_count, decoded_list_t *out, iot_atop_rejection_t *err)
{
    cJSON *root = NULL;
    int rc = biz_call(ctx, api, body, &root, err);
    if (rc) return rc;

    decoded_list_t list = { .total_count = -1 };
    const cJSON *array = list_array(root);
    size_t n = array ? (size_t)cJSON_GetArraySize(array) : 0;
    if (!array || (read_total_count && total_count(root, &list.total_count))) {
        rc = OPRT_INVALID_RESULT;
    } else if (n) {
        if (n > SIZE_MAX / item_size || !(list.items = ctx->pal->malloc(n * item_size))) {
            rc = OPRT_MALLOC_FAILED;
        } else {
            memset(list.items, 0, n * item_size);
            const cJSON *entry = NULL;
            cJSON_ArrayForEach(entry, array) {
                rc = decode(entry, (unsigned char *)list.items + list.count * item_size, arg);
                if (rc) break;
                ++list.count;
            }
        }
    }
    cJSON_Delete(root);
    if (rc) ctx->pal->free(list.items);
    else *out = list;
    return rc;
}

static int parse_role(const cJSON *obj, tuya_ai_role_t *out, int source_type)
{
    if (!cJSON_IsObject(obj)) return OPRT_INVALID_RESULT;
    memset(out, 0, sizeof(*out));
    out->speed = 1.0;
    out->type = TUYA_AI_ROLE_SINGLE;
#define FIELD(member, key, required) \
    do { if (biz_copy_string(obj, key, out->member, sizeof(out->member), required)) return OPRT_INVALID_RESULT; } while (0)
    FIELD(role_id, "roleId", true);
    FIELD(template_id, "templateId", false);
    FIELD(name, "roleName", false);
    FIELD(description, "roleDesc", false);
    FIELD(image_url, "roleImgUrl", false);
    FIELD(lang_code, "useLangCode", false);
    FIELD(lang_name, "useLangName", false);
    FIELD(timbre_id, "useTimbreId", false);
    FIELD(timbre_name, "useTimbreName", false);
#undef FIELD
    const cJSON *type = cJSON_GetObjectItemCaseSensitive(obj, "bindRoleType");
    if (source_type >= 0) out->type = (tuya_ai_role_type_t)source_type;
    else if (type) {
        if (!cJSON_IsNumber(type) || type->valuedouble < 0 || type->valuedouble > 2 ||
            type->valuedouble != type->valueint) return OPRT_INVALID_RESULT;
        out->type = (tuya_ai_role_type_t)type->valueint;
    }
    const cJSON *speed = cJSON_GetObjectItemCaseSensitive(obj, "speed");
    if (speed) {
        if (!cJSON_IsNumber(speed) || !isfinite(speed->valuedouble) || speed->valuedouble <= 0)
            return OPRT_INVALID_RESULT;
        out->speed = speed->valuedouble;
    }
    return OPRT_OK;
}

static int decode_role(const cJSON *obj, void *out, const void *source_type)
{
    return parse_role(obj, out, *(const int *)source_type);
}

void tuya_ai_role_list_free(tuya_biz_client_t *ctx, tuya_ai_role_list_t *out)
{
    if (ctx && out) { ctx->pal->free(out->items); memset(out, 0, sizeof(*out)); }
}
void tuya_ai_language_list_free(tuya_biz_client_t *ctx, tuya_ai_language_list_t *out)
{
    if (ctx && out) { ctx->pal->free(out->items); memset(out, 0, sizeof(*out)); }
}
void tuya_ai_timbre_list_free(tuya_biz_client_t *ctx, tuya_ai_timbre_list_t *out)
{
    if (ctx && out) { ctx->pal->free(out->items); memset(out, 0, sizeof(*out)); }
}

int tuya_ai_role_get_bound(tuya_biz_client_t *ctx, tuya_ai_role_t *out, iot_atop_rejection_t *err)
{
    int rc = biz_prepare(ctx, err);
    if (out) memset(out, 0, sizeof(*out));
    if (rc || !out) return OPRT_INVALID_PARAMETER;
    cJSON *root = NULL;
    rc = biz_call(ctx, ROLE_API "get-bind-role", biz_query_body(ctx), &root, err);
    if (!rc) {
        rc = parse_role(root, out, -1);
        if (rc) memset(out, 0, sizeof(*out));
    }
    cJSON_Delete(root);
    return rc;
}

static cJSON *paged_body(tuya_biz_client_t *ctx, int page, int size)
{
    cJSON *body = biz_query_body(ctx);
    if (!body || !cJSON_AddNumberToObject(body, "pageNo", page) ||
        !cJSON_AddNumberToObject(body, "pageSize", size)) {
        cJSON_Delete(body);
        return NULL;
    }
    return body;
}

static int roles_query(tuya_biz_client_t *ctx, const char *api, cJSON *body,
                        int type, tuya_ai_role_list_t *out, iot_atop_rejection_t *err)
{
    decoded_list_t list;
    int rc = query_list(ctx, api, body, sizeof(*out->items), decode_role, &type, true, &list, err);
    if (!rc) {
        out->items = list.items;
        out->count = list.count;
        out->total_count = list.total_count;
    }
    return rc;
}

int tuya_ai_role_list_templates(tuya_biz_client_t *ctx, tuya_ai_role_list_t *out, iot_atop_rejection_t *err)
{
    int rc = biz_prepare(ctx, err);
    if (out) memset(out, 0, sizeof(*out));
    if (rc || !out) return OPRT_INVALID_PARAMETER;
    return roles_query(ctx, ROLE_API "role-template.list", biz_query_body(ctx),
                        TUYA_AI_ROLE_TEMPLATE, out, err);
}

int tuya_ai_role_list_custom(tuya_biz_client_t *ctx, int page, int size,
                             tuya_ai_role_list_t *out, iot_atop_rejection_t *err)
{
    int rc = biz_prepare(ctx, err);
    if (out) memset(out, 0, sizeof(*out));
    if (rc || !out || page < 1 || size < 1) return OPRT_INVALID_PARAMETER;
    return roles_query(ctx, ROLE_API "custom-role.page", paged_body(ctx, page, size),
                        TUYA_AI_ROLE_CUSTOM, out, err);
}

static int mutate(tuya_biz_client_t *ctx, const char *api, cJSON *body, iot_atop_rejection_t *err)
{
    cJSON *root = NULL;
    int rc = biz_call(ctx, api, body, &root, err);
    cJSON_Delete(root);
    return rc;
}

int tuya_ai_role_bind(tuya_biz_client_t *ctx, tuya_ai_role_type_t type,
                      const char *id, iot_atop_rejection_t *err)
{
    int rc = biz_prepare(ctx, err);
    if (rc || (type != TUYA_AI_ROLE_CUSTOM && type != TUYA_AI_ROLE_TEMPLATE) ||
        !bounded_string_nonempty(id, TUYA_AI_ROLE_ID_LEN)) return OPRT_INVALID_PARAMETER;
    cJSON *body = cJSON_CreateObject();
    if (!body || !cJSON_AddStringToObject(body, "roleId", id) ||
        !cJSON_AddNumberToObject(body, "bindRoleType", type)) {
        cJSON_Delete(body); return OPRT_MALLOC_FAILED;
    }
    return mutate(ctx, ROLE_API "bind-with-role", body, err);
}

int tuya_ai_role_delete_custom(tuya_biz_client_t *ctx, const char *id, iot_atop_rejection_t *err)
{
    int rc = biz_prepare(ctx, err);
    if (rc || !bounded_string_nonempty(id, TUYA_AI_ROLE_ID_LEN)) return OPRT_INVALID_PARAMETER;
    cJSON *body = cJSON_CreateObject();
    if (!body || !cJSON_AddStringToObject(body, "roleId", id)) {
        cJSON_Delete(body); return OPRT_MALLOC_FAILED;
    }
    return mutate(ctx, ROLE_API "custom-role.delete", body, err);
}

static bool valid_edit(const tuya_ai_role_edit_t *edit)
{
    return edit && (edit->name || edit->lang_code || edit->timbre_id || edit->set_speed) &&
        (!edit->name || bounded_string_nonempty(edit->name, TUYA_AI_ROLE_NAME_LEN)) &&
        (!edit->lang_code || bounded_string_nonempty(edit->lang_code, TUYA_AI_ROLE_LANG_LEN)) &&
        (!edit->timbre_id || bounded_string_nonempty(edit->timbre_id, TUYA_AI_ROLE_ID_LEN)) &&
        (!edit->set_speed || (isfinite(edit->speed) && edit->speed > 0));
}

static cJSON *edit_body(const char *id, const tuya_ai_role_edit_t *edit, bool create)
{
    cJSON *body = cJSON_CreateObject();
    if (!body || !cJSON_AddStringToObject(body, "roleId", id)) goto fail;
    if (edit->name && !cJSON_AddStringToObject(body, "roleName", edit->name)) goto fail;
    if (edit->lang_code && !cJSON_AddStringToObject(body, "useLangCode", edit->lang_code)) goto fail;
    if (edit->timbre_id && !cJSON_AddStringToObject(body, "useTimbreId", edit->timbre_id)) goto fail;
    if (edit->set_speed) {
        char speed[64];
        int n = snprintf(speed, sizeof(speed), "%.2f", edit->speed);
        if (n < 0 || (size_t)n >= sizeof(speed) || !cJSON_AddStringToObject(body, "speed", speed)) goto fail;
    }
    if (create && !cJSON_AddBoolToObject(body, "needBind", true)) goto fail;
    return body;
fail:
    cJSON_Delete(body);
    return NULL;
}

int tuya_ai_role_apply_edit(tuya_biz_client_t *ctx, const tuya_ai_role_t *cur,
                            const tuya_ai_role_edit_t *edit, char out_id[TUYA_AI_ROLE_ID_LEN],
                            iot_atop_rejection_t *err)
{
    int rc = biz_prepare(ctx, err);
    if (out_id) out_id[0] = '\0';
    if (rc || !out_id || !cur || !bounded_string_nonempty(cur->role_id, sizeof(cur->role_id)) ||
        (cur->type != TUYA_AI_ROLE_TEMPLATE && cur->type != TUYA_AI_ROLE_CUSTOM) ||
        !valid_edit(edit)) return OPRT_INVALID_PARAMETER;
    bool create = cur->type == TUYA_AI_ROLE_TEMPLATE;
    cJSON *root = NULL;
    rc = biz_call(ctx, create ? ROLE_API "custom-role.create-from-template" : ROLE_API "custom-role.update",
                  edit_body(cur->role_id, edit, create), &root, err);
    if (!rc) {
        if (create) {
            if (!cJSON_IsString(root) || !bounded_string_nonempty(root->valuestring, TUYA_AI_ROLE_ID_LEN))
                rc = OPRT_INVALID_RESULT;
            else strcpy(out_id, root->valuestring);
        } else strcpy(out_id, cur->role_id);
    }
    cJSON_Delete(root);
    return rc;
}

static int decode_language(const cJSON *entry, void *out, const void *arg)
{
    (void)arg;
    tuya_ai_language_t *item = out;
    if (!cJSON_IsObject(entry) ||
        biz_copy_string(entry, "langCode", item->code, sizeof(item->code), true) ||
        biz_copy_string(entry, "langName", item->name, sizeof(item->name), false))
        return OPRT_INVALID_RESULT;
    const cJSON *def = cJSON_GetObjectItemCaseSensitive(entry, "hasDefault");
    if (def && !cJSON_IsBool(def)) return OPRT_INVALID_RESULT;
    item->has_default = cJSON_IsTrue(def);
    return OPRT_OK;
}

int tuya_ai_role_list_languages(tuya_biz_client_t *ctx, tuya_ai_language_list_t *out,
                                iot_atop_rejection_t *err)
{
    int rc = biz_prepare(ctx, err);
    if (out) memset(out, 0, sizeof(*out));
    if (rc || !out) return OPRT_INVALID_PARAMETER;
    decoded_list_t list;
    rc = query_list(ctx, "thing.ai.agent.config.languages.list", biz_query_body(ctx),
                    sizeof(*out->items), decode_language, NULL, false, &list, err);
    if (!rc) {
        out->items = list.items;
        out->count = list.count;
    }
    return rc;
}

static int decode_timbre(const cJSON *obj, void *item, const void *arg)
{
    (void)arg;
    tuya_ai_timbre_t *out = item;
    if (!cJSON_IsObject(obj) || biz_copy_string(obj, "voiceId", out->id, sizeof(out->id), true) ||
        biz_copy_string(obj, "voiceName", out->name, sizeof(out->name), false)) return OPRT_INVALID_RESULT;
    const cJSON *langs = cJSON_GetObjectItemCaseSensitive(obj, "supportLangs");
    if (!langs || cJSON_IsNull(langs)) return OPRT_OK;
    if (!cJSON_IsArray(langs)) return OPRT_INVALID_RESULT;
    const cJSON *v = NULL;
    size_t used = 0;
    cJSON_ArrayForEach(v, langs) {
        if (!cJSON_IsString(v) || !v->valuestring[0]) return OPRT_INVALID_RESULT;
        size_t len = strlen(v->valuestring);
        size_t sep = used ? 1 : 0;
        if (len >= sizeof(out->support_langs) - used - sep) return OPRT_INVALID_RESULT;
        if (sep) out->support_langs[used++] = ',';
        memcpy(out->support_langs + used, v->valuestring, len + 1);
        used += len;
    }
    return OPRT_OK;
}

int tuya_ai_role_list_timbres(tuya_biz_client_t *ctx, const char *lang, int page, int size,
                             tuya_ai_timbre_list_t *out, iot_atop_rejection_t *err)
{
    int rc = biz_prepare(ctx, err);
    if (out) memset(out, 0, sizeof(*out));
    if (rc || !out || page < 1 || size < 1 ||
        (lang && !bounded_string_nonempty(lang, TUYA_AI_ROLE_LANG_LEN))) return OPRT_INVALID_PARAMETER;
    cJSON *body = paged_body(ctx, page, size);
    if (body && lang && !cJSON_AddStringToObject(body, "lang", lang)) {
        cJSON_Delete(body); return OPRT_MALLOC_FAILED;
    }
    decoded_list_t list;
    rc = query_list(ctx, "thing.ai.timbre.market.page", body, sizeof(*out->items),
                    decode_timbre, NULL, true, &list, err);
    if (!rc) {
        out->items = list.items;
        out->count = list.count;
        out->total_count = list.total_count;
    }
    return rc;
}
