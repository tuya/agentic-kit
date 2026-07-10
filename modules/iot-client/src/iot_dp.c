/*
 * Data Point (DP) management layer implementation.
 *
 * Public API: include/iot_dp.h. Internal hooks: src/iot_dp_internal.h.
 * Design: see ~/.claude/plans/file-...-iot-stateful-eclipse.md.
 *
 * All dynamic memory goes through client->pal->malloc/free (or pal_strdup);
 * all DP state is serialized by a single recursive PAL mutex held in the
 * per-client iot_dp_context. cJSON allocations use the global pal hooks set in
 * iot_init(). Uplink reuses iot_client_publish(); no new crypto path.
 */

#include "iot_dp.h"
#include "iot_dp_internal.h"
#include "iot_config_defaults.h"   /* pal_strdup, log_*, IOT_DEFAULT_PORT */
#include "atop.h"                  /* atop_schema_newest_get */
#include "iot_dns.h"               /* iot_region_to_host (via resolve helper) */
#include "cJSON.h"
#include "mbedtls/base64.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>

/* ---- Tuya MQTT protocol numbers (centralised here on purpose) ---- */
#define DP_PROTO_REPORT  4   /* uplink   DP report (device -> cloud) */
#define DP_PROTO_DOWN    5   /* downlink DP set    (cloud -> device) */

/* Mirror of mqtt.c MQTT_MAX_PACKET_SIZE and iot_client_message.c PV23_OVERHEAD. */
#define DP_MQTT_MAX_PAYLOAD     4096
#define DP_PV23_OVERHEAD        40

typedef struct {
    uint8_t       id;
    iot_dp_type_t type;
    int32_t       vmin, vmax;      /* value range (enforced) */
    size_t        maxlen;          /* string/raw max length (0 = unbounded) */
    char        **enum_range;      /* enum labels (owned) */
    size_t        enum_count;
    bool          dirty;
    bool          has_value;
    iot_dp_value_t cur;            /* cur.value.string/raw.data point into str_buf/raw_buf */
    char         *str_buf;         /* owned string storage */
    uint8_t      *raw_buf;         /* owned raw storage */
    size_t        raw_len;
} iot_dp_entry_t;

struct iot_dp_context {
    void           *mutex;         /* recursive */
    iot_dp_entry_t *entries;
    size_t          entry_count;
    bool            loose;         /* schema NULL/empty/[] -> passthrough */
    iot_dp_callback_t            dp_cb;     void *dp_cb_user;
    iot_schema_update_callback_t schema_cb; void *schema_cb_user;
    iot_dp_save_callback_t       save_cb;   void *save_cb_user;
};

/* dp_storage in iot_client_t holds this struct inline; fail the build if it grows past it. */
_Static_assert(sizeof(struct iot_dp_context) <= IOT_DP_CONTEXT_STORAGE,
               "iot_dp_context outgrew IOT_DP_CONTEXT_STORAGE (bump it in iot_client.h)");

typedef enum { DP_SEL_ALL, DP_SEL_DIRTY, DP_SEL_ONE } dp_sel_t;

/* ============================================================================
 * Small helpers
 * ============================================================================ */

static iot_dp_entry_t *dp_find(struct iot_dp_context *ctx, uint8_t id)
{
    for (size_t i = 0; i < ctx->entry_count; i++) {
        if (ctx->entries[i].id == id) return &ctx->entries[i];
    }
    return NULL;
}

static void dp_entry_free_buffers(const pal_t *pal, iot_dp_entry_t *e)
{
    if (e->str_buf) { pal->free(e->str_buf); e->str_buf = NULL; }
    if (e->raw_buf) { pal->free(e->raw_buf); e->raw_buf = NULL; }
    e->raw_len = 0;
}

static void dp_entries_free(const pal_t *pal, iot_dp_entry_t *entries, size_t count)
{
    if (!entries) return;
    for (size_t i = 0; i < count; i++) {
        dp_entry_free_buffers(pal, &entries[i]);
        if (entries[i].enum_range) {
            for (size_t j = 0; j < entries[i].enum_count; j++) {
                pal->free(entries[i].enum_range[j]);
            }
            pal->free(entries[i].enum_range);
        }
    }
    pal->free(entries);
}

static struct iot_dp_context *dp_ensure_context(iot_client_t *client)
{
    if (!client) return NULL;
    if (client->dp) return client->dp;

    const pal_t *pal = client->pal;
    struct iot_dp_context *ctx = (struct iot_dp_context *)client->dp_storage;
    memset(ctx, 0, sizeof(*ctx));
    ctx->loose = true;
    ctx->mutex = pal->mutex_create();
    if (!ctx->mutex) {
        log_error("dp: mutex creation failed");
        return NULL;   /* dp_storage is inline; nothing to free */
    }
    client->dp = ctx;
    return ctx;
}

/* base64 encode raw bytes -> NUL-terminated pal-allocated string. */
static char *dp_base64_encode(const pal_t *pal, const uint8_t *data, size_t len)
{
    const uint8_t *src = data ? data : (const uint8_t *)"";
    size_t olen = 0;
    mbedtls_base64_encode(NULL, 0, &olen, src, len);   /* olen = required size (incl NUL) */
    if (olen == 0) olen = 1;
    char *out = (char *)pal->malloc(olen);
    if (!out) return NULL;
    if (mbedtls_base64_encode((unsigned char *)out, olen, &olen, src, len) != 0) {
        pal->free(out);
        return NULL;
    }
    out[olen] = '\0';
    return out;
}

/* base64 decode string -> pal-allocated buffer. *out may be NULL when empty. */
static int dp_base64_decode(const pal_t *pal, const char *b64, uint8_t **out, size_t *out_len)
{
    *out = NULL;
    *out_len = 0;
    if (!b64 || b64[0] == '\0') return OPRT_OK;
    size_t slen = strlen(b64);
    size_t olen = 0;
    /* Size query. On invalid base64 mbedtls returns an error WITHOUT setting olen,
     * so the old "ignore the return, check olen==0" path silently accepted garbage
     * as an empty value. Reject anything that is not valid base64. */
    int q = mbedtls_base64_decode(NULL, 0, &olen, (const uint8_t *)b64, slen);
    if (q != 0 && q != MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL) return OPRT_DP_TYPE_MISMATCH;
    if (olen == 0) return OPRT_OK;   /* validly decodes to empty */
    uint8_t *buf = (uint8_t *)pal->malloc(olen);
    if (!buf) return OPRT_MALLOC_FAILED;
    int rt = mbedtls_base64_decode(buf, olen, &olen, (const uint8_t *)b64, slen);
    if (rt != 0) {
        pal->free(buf);
        return OPRT_DP_TYPE_MISMATCH;
    }
    *out = buf;
    *out_len = olen;
    return OPRT_OK;
}

/* ============================================================================
 * Schema parsing & registry rebuild
 * ============================================================================ */

static bool dp_type_from_str(const char *s, iot_dp_type_t *out)
{
    if (!s) return false;
    if      (strcmp(s, "bool")   == 0) *out = IOT_DP_TYPE_BOOL;
    else if (strcmp(s, "value")  == 0) *out = IOT_DP_TYPE_VALUE;
    else if (strcmp(s, "string") == 0) *out = IOT_DP_TYPE_STRING;
    else if (strcmp(s, "enum")   == 0) *out = IOT_DP_TYPE_ENUM;
    else if (strcmp(s, "raw")    == 0) *out = IOT_DP_TYPE_RAW;
    else return false;
    return true;
}

/* Parse a schema JSON array into a fresh entries array. Returns NULL (and
 * *count_out = 0) for loose mode (NULL/empty/[]/parse error/no valid entries). */
static iot_dp_entry_t *dp_parse_schema(const pal_t *pal, const char *schema, size_t *count_out)
{
    *count_out = 0;
    if (!schema || schema[0] == '\0') return NULL;

    cJSON *root = cJSON_Parse(schema);
    if (!root) {
        log_warn("dp: schema parse failed -> loose mode");
        return NULL;
    }
    if (!cJSON_IsArray(root)) {
        log_warn("dp: schema is not a JSON array -> loose mode");
        cJSON_Delete(root);
        return NULL;
    }
    int n = cJSON_GetArraySize(root);
    if (n <= 0) {
        cJSON_Delete(root);
        return NULL;
    }

    iot_dp_entry_t *entries = (iot_dp_entry_t *)pal->malloc(sizeof(iot_dp_entry_t) * (size_t)n);
    if (!entries) {
        cJSON_Delete(root);
        return NULL;
    }
    memset(entries, 0, sizeof(iot_dp_entry_t) * (size_t)n);

    size_t idx = 0;
    cJSON *item = NULL;
    cJSON_ArrayForEach(item, root) {
        if (!cJSON_IsObject(item)) continue;
        cJSON *jid   = cJSON_GetObjectItem(item, "id");
        cJSON *prop  = cJSON_GetObjectItem(item, "property");
        /* The Tuya schema nests the data type under property.type — the top-level
         * "type" is a transfer category such as "obj". The simplified form used by
         * some tooling/tests puts the data type at the top level instead. Prefer
         * property.type, fall back to the top-level type. */
        cJSON *jptype = cJSON_IsObject(prop) ? cJSON_GetObjectItem(prop, "type") : NULL;
        cJSON *jtype  = cJSON_GetObjectItem(item, "type");
        const char *type_str = cJSON_IsString(jptype) ? jptype->valuestring
                             : (cJSON_IsString(jtype) ? jtype->valuestring : NULL);
        iot_dp_type_t type;
        if (!cJSON_IsNumber(jid) || !type_str || !dp_type_from_str(type_str, &type)) continue;
        int id = jid->valueint;
        if (id < 0 || id > 255) continue;

        iot_dp_entry_t *e = &entries[idx];
        e->id    = (uint8_t)id;
        e->type  = type;
        e->cur.type = type;
        e->vmin  = INT32_MIN;
        e->vmax  = INT32_MAX;

        if (cJSON_IsObject(prop)) {
            cJSON *p;
            if ((p = cJSON_GetObjectItem(prop, "min"))    && cJSON_IsNumber(p)) e->vmin   = p->valueint;
            if ((p = cJSON_GetObjectItem(prop, "max"))    && cJSON_IsNumber(p)) e->vmax   = p->valueint;
            if ((p = cJSON_GetObjectItem(prop, "maxlen")) && cJSON_IsNumber(p) && p->valueint > 0)
                e->maxlen = (size_t)p->valueint;

            cJSON *range = cJSON_GetObjectItem(prop, "range");
            if (type == IOT_DP_TYPE_ENUM && cJSON_IsArray(range)) {
                int rc = cJSON_GetArraySize(range);
                if (rc > 0) {
                    e->enum_range = (char **)pal->malloc(sizeof(char *) * (size_t)rc);
                    if (e->enum_range) {
                        memset(e->enum_range, 0, sizeof(char *) * (size_t)rc);
                        size_t k = 0;
                        cJSON *rv = NULL;
                        cJSON_ArrayForEach(rv, range) {
                            if (cJSON_IsString(rv)) {
                                char *dup = pal_strdup(pal, rv->valuestring);
                                if (dup) e->enum_range[k++] = dup;
                            }
                        }
                        e->enum_count = k;
                    }
                }
            }
        }
        idx++;
    }
    cJSON_Delete(root);

    if (idx == 0) {
        pal->free(entries);
        return NULL;
    }
    *count_out = idx;
    return entries;
}

void iot_dp_rebuild(iot_client_t *client)
{
    struct iot_dp_context *ctx = dp_ensure_context(client);
    if (!ctx) return;
    const pal_t *pal = client->pal;

    /* Build the new registry OUTSIDE the lock, then swap under it. */
    size_t new_count = 0;
    iot_dp_entry_t *new_entries = dp_parse_schema(pal, client->schema, &new_count);
    bool loose = (new_entries == NULL);

    pal->mutex_lock(ctx->mutex);
    iot_dp_entry_t *old_entries = ctx->entries;
    size_t          old_count   = ctx->entry_count;
    ctx->entries     = new_entries;
    ctx->entry_count = new_count;
    ctx->loose       = loose;
    pal->mutex_unlock(ctx->mutex);

    dp_entries_free(pal, old_entries, old_count);
    /* Use the local copy, not ctx->loose, which is shared state read here without
     * the lock. */
    log_info("dp: registry rebuilt (%zu DPs, %s)", new_count, loose ? "loose" : "schema");
}

/* ============================================================================
 * Validation + store (must be called with the lock held)
 * ============================================================================ */

static int dp_validate(const iot_dp_entry_t *e, const iot_dp_value_t *v)
{
    if (e->type != v->type) return OPRT_DP_TYPE_MISMATCH;
    switch (e->type) {
    case IOT_DP_TYPE_VALUE:
        if (v->value.integer < e->vmin || v->value.integer > e->vmax)
            return OPRT_DP_VALUE_OUT_OF_RANGE;
        break;
    case IOT_DP_TYPE_STRING:
        if (!v->value.string) return OPRT_DP_TYPE_MISMATCH;
        if (e->maxlen > 0 && strlen(v->value.string) > e->maxlen)
            return OPRT_DP_VALUE_OUT_OF_RANGE;
        break;
    case IOT_DP_TYPE_ENUM:
        /* A negative index is never valid (even for a range-less enum, which would
         * otherwise pass the strict iot_dp_validate_json gate); upper-bound only
         * when the schema carries a range[]. */
        if (v->value.enum_index < 0)
            return OPRT_DP_VALUE_OUT_OF_RANGE;
        if (e->enum_count > 0 && (size_t)v->value.enum_index >= e->enum_count)
            return OPRT_DP_VALUE_OUT_OF_RANGE;
        break;
    case IOT_DP_TYPE_RAW:
        if (!v->value.raw.data && v->value.raw.len > 0) return OPRT_DP_TYPE_MISMATCH;
        if (e->maxlen > 0 && v->value.raw.len > e->maxlen)
            return OPRT_DP_VALUE_OUT_OF_RANGE;
        break;
    case IOT_DP_TYPE_BOOL:
    default:
        break;
    }
    return OPRT_OK;
}

/* Deep-copy a (pre-validated) value into the entry's current slot. */
static int dp_store(const pal_t *pal, iot_dp_entry_t *e, const iot_dp_value_t *v)
{
    switch (e->type) {
    case IOT_DP_TYPE_BOOL:
        e->cur.value.boolean = v->value.boolean;
        break;
    case IOT_DP_TYPE_VALUE:
        e->cur.value.integer = v->value.integer;
        break;
    case IOT_DP_TYPE_ENUM:
        e->cur.value.enum_index = v->value.enum_index;
        break;
    case IOT_DP_TYPE_STRING: {
        char *copy = pal_strdup(pal, v->value.string ? v->value.string : "");
        if (!copy) return OPRT_MALLOC_FAILED;
        if (e->str_buf) pal->free(e->str_buf);
        e->str_buf = copy;
        e->cur.value.string = e->str_buf;
        break;
    }
    case IOT_DP_TYPE_RAW: {
        uint8_t *copy = NULL;
        if (v->value.raw.len > 0) {
            copy = (uint8_t *)pal->malloc(v->value.raw.len);
            if (!copy) return OPRT_MALLOC_FAILED;
            memcpy(copy, v->value.raw.data, v->value.raw.len);
        }
        if (e->raw_buf) pal->free(e->raw_buf);
        e->raw_buf = copy;
        e->raw_len = v->value.raw.len;
        e->cur.value.raw.data = e->raw_buf;
        e->cur.value.raw.len  = e->raw_len;
        break;
    }
    }
    e->cur.type   = e->type;
    e->has_value  = true;
    return OPRT_OK;
}

/* Apply a typed value (lock held). Schema "mode" is not enforced: the device may
 * set/report any DP, and downlink may set any DP. */
static int dp_apply_value(struct iot_dp_context *ctx, const pal_t *pal, uint8_t id,
                          const iot_dp_value_t *v, bool mark_dirty)
{
    iot_dp_entry_t *e = dp_find(ctx, id);
    if (!e) return OPRT_DP_INVALID_ID;
    int rt = dp_validate(e, v);
    if (rt != OPRT_OK) return rt;
    rt = dp_store(pal, e, v);
    if (rt != OPRT_OK) return rt;
    if (mark_dirty) e->dirty = true;
    return OPRT_OK;
}

/* Coerce a cJSON value into the entry type and apply it (lock held). When
 * check_only is true, the value is coerced and validated against the schema but
 * NOT stored (no cache mutation, no dirty) — used to vet a persisted dp_state. */
static int dp_apply_json(struct iot_dp_context *ctx, const pal_t *pal, uint8_t id,
                         const cJSON *jv, bool mark_dirty, bool check_only)
{
    iot_dp_entry_t *e = dp_find(ctx, id);
    if (!e) return OPRT_DP_INVALID_ID;

    iot_dp_value_t v;
    memset(&v, 0, sizeof(v));
    v.type = e->type;
    uint8_t *raw_tmp = NULL;
    int rt = OPRT_OK;

    switch (e->type) {
    case IOT_DP_TYPE_BOOL:
        if (!cJSON_IsBool(jv)) return OPRT_DP_TYPE_MISMATCH;
        v.value.boolean = cJSON_IsTrue(jv);
        break;
    case IOT_DP_TYPE_VALUE: {
        if (!cJSON_IsNumber(jv)) return OPRT_DP_TYPE_MISMATCH;
        /* Range-check the full-precision number before narrowing. cJSON's valueint
         * saturates/truncates, so reading it directly would silently clamp an
         * out-of-int32 (or fractional) input into the valid band instead of
         * rejecting it. */
        double d = jv->valuedouble;
        if (d < (double)e->vmin || d > (double)e->vmax) return OPRT_DP_VALUE_OUT_OF_RANGE;
        v.value.integer = (int32_t)d;
        break;
    }
    case IOT_DP_TYPE_STRING:
        if (!cJSON_IsString(jv)) return OPRT_DP_TYPE_MISMATCH;
        v.value.string = jv->valuestring;
        break;
    case IOT_DP_TYPE_ENUM:
        if (cJSON_IsString(jv)) {
            int found = -1;
            for (size_t k = 0; k < e->enum_count; k++) {
                if (e->enum_range[k] && strcmp(e->enum_range[k], jv->valuestring) == 0) {
                    found = (int)k;
                    break;
                }
            }
            if (found < 0) return OPRT_DP_VALUE_OUT_OF_RANGE;
            v.value.enum_index = found;
        } else if (cJSON_IsNumber(jv)) {
            v.value.enum_index = jv->valueint;
        } else {
            return OPRT_DP_TYPE_MISMATCH;
        }
        break;
    case IOT_DP_TYPE_RAW: {
        if (!cJSON_IsString(jv)) return OPRT_DP_TYPE_MISMATCH;
        size_t rlen = 0;
        rt = dp_base64_decode(pal, jv->valuestring, &raw_tmp, &rlen);
        if (rt != OPRT_OK) return rt;
        v.value.raw.data = raw_tmp;
        v.value.raw.len  = rlen;
        break;
    }
    }

    rt = dp_validate(e, &v);
    if (rt == OPRT_OK && !check_only) rt = dp_store(pal, e, &v);
    if (rt == OPRT_OK && !check_only && mark_dirty) e->dirty = true;
    if (raw_tmp) pal->free(raw_tmp);
    return rt;
}

/* ============================================================================
 * JSON builders
 * ============================================================================ */

/* Add the entry's current value to a dps object, keyed by decimal id (lock held).
 * Returns OPRT_OK, or OPRT_MALLOC_FAILED if any cJSON insertion or base64 encode
 * fails — so callers abort the build instead of emitting a silently truncated
 * report/snapshot. */
static int dp_add_value(cJSON *dps, const char *key, const iot_dp_entry_t *e, const pal_t *pal)
{
    switch (e->type) {
    case IOT_DP_TYPE_BOOL:
        if (!cJSON_AddBoolToObject(dps, key, e->cur.value.boolean)) return OPRT_MALLOC_FAILED;
        break;
    case IOT_DP_TYPE_VALUE:
        if (!cJSON_AddNumberToObject(dps, key, (double)e->cur.value.integer)) return OPRT_MALLOC_FAILED;
        break;
    case IOT_DP_TYPE_STRING:
        if (!cJSON_AddStringToObject(dps, key, e->cur.value.string ? e->cur.value.string : ""))
            return OPRT_MALLOC_FAILED;
        break;
    case IOT_DP_TYPE_ENUM: {
        int ix = e->cur.value.enum_index;
        cJSON *added = (e->enum_range && ix >= 0 && (size_t)ix < e->enum_count && e->enum_range[ix])
                     ? cJSON_AddStringToObject(dps, key, e->enum_range[ix])
                     : cJSON_AddNumberToObject(dps, key, (double)ix);
        if (!added) return OPRT_MALLOC_FAILED;
        break;
    }
    case IOT_DP_TYPE_RAW: {
        char *b64 = dp_base64_encode(pal, e->raw_buf, e->raw_len);
        if (!b64) return OPRT_MALLOC_FAILED;
        cJSON *added = cJSON_AddStringToObject(dps, key, b64);
        pal->free(b64);
        if (!added) return OPRT_MALLOC_FAILED;
        break;
    }
    }
    return OPRT_OK;
}

/* Build {"dps":{...}} of all has_value DPs (lock held). pal-allocated; caller frees.
 * When include_raw is false, raw DPs are omitted: raw carries transient binary
 * frames that must not be persisted/restored. They are still reported uplink (see
 * dp_build_report_json) and preserved across a schema upgrade (include_raw=true). */
static char *dp_build_state_json(iot_client_t *client, bool include_raw)
{
    struct iot_dp_context *ctx = client->dp;
    const pal_t *pal = client->pal;

    cJSON *root = cJSON_CreateObject();
    if (!root) return NULL;
    cJSON *dps = cJSON_CreateObject();
    if (!dps) { cJSON_Delete(root); return NULL; }
    if (!cJSON_AddItemToObject(root, "dps", dps)) { cJSON_Delete(dps); cJSON_Delete(root); return NULL; }

    for (size_t i = 0; i < ctx->entry_count; i++) {
        iot_dp_entry_t *e = &ctx->entries[i];
        if (!e->has_value) continue;
        if (!include_raw && e->type == IOT_DP_TYPE_RAW) continue;
        char key[8];
        snprintf(key, sizeof(key), "%u", (unsigned)e->id);
        if (dp_add_value(dps, key, e, pal) != OPRT_OK) { cJSON_Delete(root); return NULL; }
    }

    /* cJSON's allocator is the pal allocator (cJSON_InitHooks in iot_init), so the
     * printed string is already pal-owned -- return it directly; callers free it
     * via pal->free. Avoids a second malloc + copy per state build. */
    char *s = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return s;
}

/* Build a {"protocol":3,"t":..,"data":{"dps":{...}}} report (lock held). */
static char *dp_build_report_json(iot_client_t *client, dp_sel_t sel, uint8_t one_id, size_t *count_out)
{
    struct iot_dp_context *ctx = client->dp;
    const pal_t *pal = client->pal;
    *count_out = 0;

    cJSON *root = cJSON_CreateObject();
    if (!root) return NULL;
    cJSON_AddNumberToObject(root, "protocol", DP_PROTO_REPORT);
    cJSON_AddNumberToObject(root, "t", (double)(uint32_t)time(NULL));
    cJSON *data = cJSON_CreateObject();
    if (!data) { cJSON_Delete(root); return NULL; }
    if (!cJSON_AddItemToObject(root, "data", data)) { cJSON_Delete(data); cJSON_Delete(root); return NULL; }
    cJSON *dps = cJSON_CreateObject();
    if (!dps) { cJSON_Delete(root); return NULL; }
    if (!cJSON_AddItemToObject(data, "dps", dps)) { cJSON_Delete(dps); cJSON_Delete(root); return NULL; }

    size_t cnt = 0;
    for (size_t i = 0; i < ctx->entry_count; i++) {
        iot_dp_entry_t *e = &ctx->entries[i];
        if (!e->has_value) continue;
        if (sel == DP_SEL_DIRTY && !e->dirty) continue;
        if (sel == DP_SEL_ONE && e->id != one_id) continue;
        char key[8];
        snprintf(key, sizeof(key), "%u", (unsigned)e->id);
        if (dp_add_value(dps, key, e, pal) != OPRT_OK) { cJSON_Delete(root); return NULL; }
        cnt++;
    }

    /* cJSON allocs via the pal allocator -- return the printed string directly
     * (caller frees via pal->free); avoids a second malloc + copy per report. */
    char *s = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!s) return NULL;
    *count_out = cnt;
    return s;
}

/* Build the current snapshot under the lock, then fire the save callback OUTSIDE it. */
static void dp_fire_save(iot_client_t *client)
{
    struct iot_dp_context *ctx = client->dp;
    if (!ctx) return;
    const pal_t *pal = client->pal;

    pal->mutex_lock(ctx->mutex);
    iot_dp_save_callback_t cb = ctx->save_cb;
    void *user = ctx->save_cb_user;
    char *json = cb ? dp_build_state_json(client, /*include_raw*/false) : NULL;
    pal->mutex_unlock(ctx->mutex);

    if (cb && json) cb(json, user);
    if (json) pal->free(json);
}

/* ============================================================================
 * Public: callback registration
 * ============================================================================ */

int iot_dp_set_callback(iot_client_t *client, iot_dp_callback_t cb, void *user_data)
{
    if (!client) return OPRT_INVALID_PARAMETER;
    struct iot_dp_context *ctx = dp_ensure_context(client);
    if (!ctx) return OPRT_UNINITIALIZED;
    client->pal->mutex_lock(ctx->mutex);
    ctx->dp_cb = cb;
    ctx->dp_cb_user = user_data;
    client->pal->mutex_unlock(ctx->mutex);
    return OPRT_OK;
}

int iot_dp_set_schema_update_callback(iot_client_t *client, iot_schema_update_callback_t cb, void *user_data)
{
    if (!client) return OPRT_INVALID_PARAMETER;
    struct iot_dp_context *ctx = dp_ensure_context(client);
    if (!ctx) return OPRT_UNINITIALIZED;
    client->pal->mutex_lock(ctx->mutex);
    ctx->schema_cb = cb;
    ctx->schema_cb_user = user_data;
    client->pal->mutex_unlock(ctx->mutex);
    return OPRT_OK;
}

int iot_dp_set_save_callback(iot_client_t *client, iot_dp_save_callback_t cb, void *user_data)
{
    if (!client) return OPRT_INVALID_PARAMETER;
    struct iot_dp_context *ctx = dp_ensure_context(client);
    if (!ctx) return OPRT_UNINITIALIZED;
    client->pal->mutex_lock(ctx->mutex);
    ctx->save_cb = cb;
    ctx->save_cb_user = user_data;
    client->pal->mutex_unlock(ctx->mutex);
    return OPRT_OK;
}

/* ============================================================================
 * Public: local state
 * ============================================================================ */

int iot_dp_set(iot_client_t *client, uint8_t dp_id, const iot_dp_value_t *value)
{
    if (!client || !value) return OPRT_INVALID_PARAMETER;
    struct iot_dp_context *ctx = client->dp;
    if (!ctx) return OPRT_UNINITIALIZED;
    const pal_t *pal = client->pal;

    pal->mutex_lock(ctx->mutex);
    int rt = dp_apply_value(ctx, pal, dp_id, value, /*dirty*/true);
    pal->mutex_unlock(ctx->mutex);

    if (rt == OPRT_OK) dp_fire_save(client);
    return rt;
}

int iot_dp_get(iot_client_t *client, uint8_t dp_id, iot_dp_value_t *out)
{
    if (!client || !out) return OPRT_INVALID_PARAMETER;
    struct iot_dp_context *ctx = client->dp;
    if (!ctx) return OPRT_UNINITIALIZED;
    const pal_t *pal = client->pal;

    pal->mutex_lock(ctx->mutex);
    iot_dp_entry_t *e = dp_find(ctx, dp_id);
    int rt;
    if (!e)               rt = OPRT_DP_INVALID_ID;
    else if (!e->has_value) rt = OPRT_INVALID_RESULT;
    else { *out = e->cur; rt = OPRT_OK; }
    pal->mutex_unlock(ctx->mutex);
    return rt;
}

/* ============================================================================
 * Public: reporting (uplink)
 * ============================================================================ */

/* Publish a pre-built report JSON with the MQTT packet-size guard. */
static int dp_publish_report(iot_client_t *client, char *json)
{
    const pal_t *pal = client->pal;
    size_t jl = strlen(json);
    if (jl + DP_PV23_OVERHEAD > DP_MQTT_MAX_PAYLOAD) {
        log_error("dp: report payload too large (%zu bytes, max %d) — split via iot_dp_report",
                  jl, DP_MQTT_MAX_PAYLOAD - DP_PV23_OVERHEAD);
        pal->free(json);
        return OPRT_DP_PAYLOAD_TOO_LARGE;
    }
    int rt = iot_client_publish(client, (const uint8_t *)json, jl);
    pal->free(json);
    return rt;
}

int iot_dp_report(iot_client_t *client, uint8_t dp_id, const iot_dp_value_t *value)
{
    if (!client || !value) return OPRT_INVALID_PARAMETER;
    struct iot_dp_context *ctx = client->dp;
    if (!ctx) return OPRT_UNINITIALIZED;
    const pal_t *pal = client->pal;

    pal->mutex_lock(ctx->mutex);
    /* Mark dirty up front: if the publish below fails, the value stays pending and
     * is retried by the next iot_dp_report_all_dirty(). The dirty bit is cleared
     * only on a successful publish. */
    int rt = dp_apply_value(ctx, pal, dp_id, value, /*dirty*/true);
    char *json = NULL;
    size_t cnt = 0;
    if (rt == OPRT_OK) {
        json = dp_build_report_json(client, DP_SEL_ONE, dp_id, &cnt);
        if (!json) rt = OPRT_MALLOC_FAILED;
    }
    pal->mutex_unlock(ctx->mutex);

    if (rt != OPRT_OK) {
        if (json) pal->free(json);
        return rt;
    }

    /* Value changed in the cache regardless of publish outcome -> notify save. */
    dp_fire_save(client);

    rt = dp_publish_report(client, json);
    if (rt == OPRT_OK) {
        pal->mutex_lock(ctx->mutex);
        iot_dp_entry_t *e = dp_find(ctx, dp_id);
        if (e) e->dirty = false;
        pal->mutex_unlock(ctx->mutex);
    }
    return rt;
}

static int dp_report_impl(iot_client_t *client, dp_sel_t sel)
{
    struct iot_dp_context *ctx = client->dp;
    if (!ctx) return OPRT_UNINITIALIZED;
    const pal_t *pal = client->pal;

    /* Ids whose dirty bit we optimistically clear before publishing, so a publish
     * failure can roll them back. (DP ids are 0..255, so at most 256 entries.) */
    uint8_t cleared[256];
    size_t  ncleared = 0;

    pal->mutex_lock(ctx->mutex);
    size_t cnt = 0;
    char *json = dp_build_report_json(client, sel, 0, &cnt);
    /* Clear the dirty bits we are about to publish, under the SAME lock that built
     * the payload, recording them for rollback. A concurrent iot_dp_set during the
     * unlocked publish below then re-dirties independently and is never lost —
     * unlike a blanket post-publish clear, which would discard that update. */
    if (json && cnt > 0) {
        for (size_t i = 0; i < ctx->entry_count; i++) {
            iot_dp_entry_t *e = &ctx->entries[i];
            if (!e->has_value || !e->dirty) continue;
            if (ncleared < sizeof(cleared)) cleared[ncleared++] = e->id;
            e->dirty = false;
        }
    }
    pal->mutex_unlock(ctx->mutex);

    if (!json) return OPRT_MALLOC_FAILED;            /* build alloc failure (was masked as OK) */
    if (cnt == 0) { pal->free(json); return OPRT_OK; } /* nothing dirty / nothing known: no-op */

    int rt = dp_publish_report(client, json);
    if (rt != OPRT_OK) {
        /* Publish failed: roll back the dirty bits we cleared so they are retried.
         * Any entry re-dirtied concurrently stays dirty regardless. */
        pal->mutex_lock(ctx->mutex);
        for (size_t i = 0; i < ncleared; i++) {
            iot_dp_entry_t *e = dp_find(ctx, cleared[i]);
            if (e) e->dirty = true;
        }
        pal->mutex_unlock(ctx->mutex);
    }
    return rt;
}

int iot_dp_report_all_dirty(iot_client_t *client)
{
    if (!client) return OPRT_INVALID_PARAMETER;
    return dp_report_impl(client, DP_SEL_DIRTY);
}

int iot_dp_report_all(iot_client_t *client)
{
    if (!client) return OPRT_INVALID_PARAMETER;
    return dp_report_impl(client, DP_SEL_ALL);
}

/* ============================================================================
 * Public: persistence mechanism
 * ============================================================================ */

int iot_dp_dump_json(iot_client_t *client, char **out_json)
{
    if (!client || !out_json) return OPRT_INVALID_PARAMETER;
    *out_json = NULL;
    struct iot_dp_context *ctx = client->dp;
    if (!ctx) return OPRT_UNINITIALIZED;
    const pal_t *pal = client->pal;

    pal->mutex_lock(ctx->mutex);
    char *json = dp_build_state_json(client, /*include_raw*/false);
    pal->mutex_unlock(ctx->mutex);

    if (!json) return OPRT_MALLOC_FAILED;
    *out_json = json;
    return OPRT_OK;
}

int iot_dp_validate_json(iot_client_t *client, const char *dp_state_json)
{
    if (!client || !dp_state_json) return OPRT_INVALID_PARAMETER;
    struct iot_dp_context *ctx = client->dp;
    if (!ctx) return OPRT_UNINITIALIZED;
    const pal_t *pal = client->pal;

    cJSON *root = cJSON_Parse(dp_state_json);
    if (!root) return OPRT_DP_SCHEMA_PARSE_FAILED;
    cJSON *dps = cJSON_GetObjectItem(root, "dps");
    if (!cJSON_IsObject(dps)) {
        cJSON_Delete(root);
        return OPRT_INVALID_PARAMETER;
    }

    int rt = OPRT_OK;
    pal->mutex_lock(ctx->mutex);
    cJSON *kv = NULL;
    cJSON_ArrayForEach(kv, dps) {
        if (!kv->string) continue;
        int id = atoi(kv->string);
        if (id < 0 || id > 255) { rt = OPRT_DP_INVALID_ID; break; }
        rt = dp_apply_json(ctx, pal, (uint8_t)id, kv, /*dirty*/false, /*check_only*/true);
        if (rt != OPRT_OK) {
            log_warn("dp: validate dp %d failed (%d)", id, rt);
            break;
        }
    }
    pal->mutex_unlock(ctx->mutex);

    cJSON_Delete(root);
    return rt;
}

int iot_dp_restore_json(iot_client_t *client, const char *dp_state_json)
{
    if (!client || !dp_state_json) return OPRT_INVALID_PARAMETER;
    struct iot_dp_context *ctx = client->dp;
    if (!ctx) return OPRT_UNINITIALIZED;
    const pal_t *pal = client->pal;

    cJSON *root = cJSON_Parse(dp_state_json);
    if (!root) {
        log_warn("dp: restore JSON parse failed");
        return OPRT_DP_SCHEMA_PARSE_FAILED;
    }
    cJSON *dps = cJSON_GetObjectItem(root, "dps");
    if (!cJSON_IsObject(dps)) {
        cJSON_Delete(root);
        return OPRT_INVALID_PARAMETER;
    }

    pal->mutex_lock(ctx->mutex);
    cJSON *kv = NULL;
    cJSON_ArrayForEach(kv, dps) {
        if (!kv->string) continue;
        int id = atoi(kv->string);
        if (id < 0 || id > 255) continue;
        int rt = dp_apply_json(ctx, pal, (uint8_t)id, kv, /*dirty*/false, /*check_only*/false);
        if (rt != OPRT_OK) log_warn("dp: restore dp %d skipped (%d)", id, rt);
    }
    pal->mutex_unlock(ctx->mutex);

    cJSON_Delete(root);
    return OPRT_OK;   /* lenient: never fails on individual entries */
}

/* ============================================================================
 * Public: schema upgrade
 * ============================================================================ */

int iot_dp_schema_check_update(iot_client_t *client)
{
    if (!client) return OPRT_INVALID_PARAMETER;
    const pal_t *pal = client->pal;
    if (client->schema_id[0] == '\0') {
        log_warn("dp: schema_id is empty; cannot check for schema update");
        return OPRT_INVALID_PARAMETER;
    }

    char host[64] = {0};
    uint16_t port = IOT_DEFAULT_PORT;
    iot_client_resolve_atop_host(client, host, sizeof(host), &port);

    schema_newest_request_t req = {
        .devid     = client->devid,
        .key       = client->secret_key,
        .schema_id = client->schema_id,
        .version   = "",
        .node_id   = NULL,
        .host      = host[0] ? host : NULL,
        .port      = port,
        .cacert    = client->cacert,
        .cert_bundle_attach = client->cert_bundle_attach,
    };
    schema_newest_response_t resp = {0};
    int rt = atop_schema_newest_get(pal, &req, &resp);
    if (rt != OPRT_OK) {
        atop_schema_newest_response_free(pal, &resp);
        return rt;
    }

    if (!resp.updated || !resp.schema) {
        atop_schema_newest_response_free(pal, &resp);
        return OPRT_OK;   /* no newer schema */
    }

    char *new_schema = pal_strdup(pal, resp.schema);
    if (!new_schema) {
        atop_schema_newest_response_free(pal, &resp);
        return OPRT_MALLOC_FAILED;
    }
    atop_schema_newest_response_free(pal, &resp);

    /* Non-destructive upgrade: snapshot current values, swap to the new schema,
     * rebuild, restore the snapshot (DPs that still exist keep their values;
     * removed/retyped ids are skipped), then default any newly-added DPs.
     * include_raw=true: this is an in-memory round-trip, not persistence, so live
     * raw values survive the rebuild even though they are never persisted.
     *
     * The whole free/swap/rebuild/restore/defaults sequence runs under the DP lock
     * so a concurrent get/report never observes a half-built registry and
     * client->schema is never freed while iot_dp_rebuild reads it. The PAL mutex is
     * recursive, so the nested locks taken by rebuild/restore/init_defaults are
     * fine, and none of them fires a user callback under the lock. */
    struct iot_dp_context *ctx = client->dp;   /* NULL only pre-init (single-threaded) */
    if (ctx) pal->mutex_lock(ctx->mutex);

    char *snapshot = ctx ? dp_build_state_json(client, /*include_raw*/true) : NULL;

    if (client->schema) pal->free(client->schema);
    client->schema = new_schema;
    iot_dp_rebuild(client);
    if (snapshot) {
        iot_dp_restore_json(client, snapshot);
        pal->free(snapshot);
    }
    iot_dp_init_defaults(client);            /* only fills the new (still value-less) DPs */

    iot_schema_update_callback_t cb = NULL;
    void *user = NULL;
    if (client->dp) {
        cb = client->dp->schema_cb;
        user = client->dp->schema_cb_user;
    }
    if (ctx) pal->mutex_unlock(ctx->mutex);

    /* Notify the application to persist the new schema (fire outside the lock). */
    if (cb) cb(client->schema_id, client->schema, user);
    return OPRT_OK;
}

/* ============================================================================
 * Internal: downlink dispatch
 * ============================================================================ */

/* Deep-copy an entry's current value into caller-owned storage, so it can be handed
 * to dp_cb AFTER the lock is released without aliasing the registry's str_buf/raw_buf
 * (which a concurrent set would free). Pair with dp_value_snapshot_free. On a string/
 * raw alloc failure the snapshot degrades to an empty value rather than a dangling one. */
static void dp_value_snapshot(const pal_t *pal, const iot_dp_entry_t *e, iot_dp_value_t *out)
{
    *out = e->cur;   /* type + scalar union; STRING/RAW pointers fixed up below */
    if (e->type == IOT_DP_TYPE_STRING) {
        out->value.string = pal_strdup(pal, e->cur.value.string ? e->cur.value.string : "");
    } else if (e->type == IOT_DP_TYPE_RAW) {
        uint8_t *cp = NULL;
        if (e->raw_len > 0 && e->raw_buf) {
            cp = (uint8_t *)pal->malloc(e->raw_len);
            if (cp) memcpy(cp, e->raw_buf, e->raw_len);
        }
        out->value.raw.data = cp;
        out->value.raw.len  = cp ? e->raw_len : 0;
    }
}

static void dp_value_snapshot_free(const pal_t *pal, iot_dp_value_t *v)
{
    if (v->type == IOT_DP_TYPE_STRING && v->value.string)
        pal->free((void *)v->value.string);
    else if (v->type == IOT_DP_TYPE_RAW && v->value.raw.data)
        pal->free((void *)v->value.raw.data);
}

bool iot_dp_dispatch_downlink(iot_client_t *client, const char *topic, size_t topic_len,
                              const uint8_t *bytes, size_t len)
{
    (void)topic;
    (void)topic_len;
    struct iot_dp_context *ctx = client ? client->dp : NULL;
    if (!ctx) return false;
    const pal_t *pal = client->pal;

    bool loose;
    pal->mutex_lock(ctx->mutex);
    loose = ctx->loose;
    pal->mutex_unlock(ctx->mutex);
    if (loose) return false;
    if (!bytes || len == 0) return false;

    cJSON *root = cJSON_ParseWithLength((const char *)bytes, len);
    if (!root) return false;

    cJSON *jproto = cJSON_GetObjectItem(root, "protocol");
    if (!cJSON_IsNumber(jproto)) { cJSON_Delete(root); return false; }
    int proto = jproto->valueint;
    cJSON *data = cJSON_GetObjectItem(root, "data");

    bool consumed = false;

    if (proto == DP_PROTO_DOWN) {
        cJSON *dps = cJSON_IsObject(data) ? cJSON_GetObjectItem(data, "dps") : NULL;
        uint8_t changed[256];
        size_t  nchanged = 0;
        iot_dp_callback_t dp_cb = NULL;
        void *dp_user = NULL;

        if (cJSON_IsObject(dps)) {
            iot_dp_value_t *snap = NULL;

            pal->mutex_lock(ctx->mutex);
            cJSON *kv = NULL;
            cJSON_ArrayForEach(kv, dps) {
                if (!kv->string) continue;
                int id = atoi(kv->string);
                if (id < 0 || id > 255) continue;
                int rt = dp_apply_json(ctx, pal, (uint8_t)id, kv, /*dirty*/false, /*check_only*/false);
                if (rt == OPRT_OK) {
                    if (nchanged < sizeof(changed)) changed[nchanged++] = (uint8_t)id;
                } else {
                    log_warn("dp: downlink dp %d rejected (%d)", id, rt);
                }
            }
            dp_cb = ctx->dp_cb;
            dp_user = ctx->dp_cb_user;
            /* Deep-copy the changed values WHILE HOLDING the lock, so the callbacks
             * below (fired after unlock, per the API contract) never dereference
             * registry storage that a concurrent set could free. */
            if (dp_cb && nchanged > 0) {
                snap = (iot_dp_value_t *)pal->malloc(sizeof(iot_dp_value_t) * nchanged);
                if (snap) {
                    for (size_t i = 0; i < nchanged; i++) {
                        iot_dp_entry_t *e = dp_find(ctx, changed[i]);
                        if (e && e->has_value) {
                            dp_value_snapshot(pal, e, &snap[i]);
                        } else {
                            memset(&snap[i], 0, sizeof(snap[i]));
                        }
                    }
                }
            }
            pal->mutex_unlock(ctx->mutex);

            if (dp_cb && snap) {
                for (size_t i = 0; i < nchanged; i++) {
                    dp_cb(changed[i], &snap[i], dp_user);
                    dp_value_snapshot_free(pal, &snap[i]);
                }
                pal->free(snap);
            } else if (dp_cb && nchanged > 0) {
                log_warn("dp: callback snapshot alloc failed; skipped %zu downlink callbacks", nchanged);
            }
            if (nchanged > 0) dp_fire_save(client);
        }
        consumed = true;
    }

    cJSON_Delete(root);
    return consumed;
}

/* ============================================================================
 * Internal: first-activation defaults
 * ============================================================================ */

/* Seed every value-less DP with a schema-derived default. Used only on first
 * activation (on-boarding) so the device has a complete initial DP state to
 * report. Marks each default dirty (the cloud has never seen these values). */
void iot_dp_init_defaults(iot_client_t *client)
{
    struct iot_dp_context *ctx = client ? client->dp : NULL;
    if (!ctx) return;
    const pal_t *pal = client->pal;

    pal->mutex_lock(ctx->mutex);
    for (size_t i = 0; i < ctx->entry_count; i++) {
        iot_dp_entry_t *e = &ctx->entries[i];
        if (e->has_value) continue;

        iot_dp_value_t v;
        memset(&v, 0, sizeof(v));
        v.type = e->type;
        switch (e->type) {
        case IOT_DP_TYPE_BOOL:
            v.value.boolean = false;
            break;
        case IOT_DP_TYPE_VALUE: {
            int32_t d = 0;                       /* 0, clamped into [min,max] */
            if (d < e->vmin) d = e->vmin;
            if (d > e->vmax) d = e->vmax;
            v.value.integer = d;
            break;
        }
        case IOT_DP_TYPE_ENUM:
            v.value.enum_index = 0;              /* first range entry */
            break;
        case IOT_DP_TYPE_STRING:
            v.value.string = "";
            break;
        case IOT_DP_TYPE_RAW:
            v.value.raw.data = NULL;
            v.value.raw.len = 0;
            break;
        }
        if (dp_store(pal, e, &v) == OPRT_OK) e->dirty = true;
    }
    pal->mutex_unlock(ctx->mutex);
}

/* ============================================================================
 * Internal: teardown
 * ============================================================================ */

void iot_dp_deinit(iot_client_t *client)
{
    if (!client || !client->dp) return;
    struct iot_dp_context *ctx = client->dp;
    const pal_t *pal = client->pal;

    pal->mutex_lock(ctx->mutex);
    iot_dp_entry_t *entries = ctx->entries;
    size_t count = ctx->entry_count;
    ctx->entries = NULL;
    ctx->entry_count = 0;
    pal->mutex_unlock(ctx->mutex);

    dp_entries_free(pal, entries, count);
    pal->mutex_destroy(ctx->mutex);
    client->dp = NULL;
}
