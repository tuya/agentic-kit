#ifndef TUYA_AI_ROLE_H
#define TUYA_AI_ROLE_H

#include "tuya_biz_client.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TUYA_AI_ROLE_ID_LEN   64
#define TUYA_AI_ROLE_NAME_LEN 128
#define TUYA_AI_ROLE_DESC_LEN 512
#define TUYA_AI_ROLE_URL_LEN  1024
#define TUYA_AI_ROLE_LANG_LEN 32

typedef enum {
    TUYA_AI_ROLE_CUSTOM = 0,
    TUYA_AI_ROLE_TEMPLATE = 1,
    TUYA_AI_ROLE_SINGLE = 2
} tuya_ai_role_type_t;

typedef struct {
    char role_id[TUYA_AI_ROLE_ID_LEN];
    char template_id[TUYA_AI_ROLE_ID_LEN];
    tuya_ai_role_type_t type;
    char name[TUYA_AI_ROLE_NAME_LEN];
    char description[TUYA_AI_ROLE_DESC_LEN];
    char image_url[TUYA_AI_ROLE_URL_LEN];
    char lang_code[TUYA_AI_ROLE_LANG_LEN];
    char lang_name[TUYA_AI_ROLE_NAME_LEN];
    char timbre_id[TUYA_AI_ROLE_ID_LEN];
    char timbre_name[TUYA_AI_ROLE_NAME_LEN];
    double speed;
} tuya_ai_role_t;

typedef struct {
    tuya_ai_role_t *items;
    size_t count;
    int total_count; /* -1 if cloud omitted pagination metadata */
} tuya_ai_role_list_t;

typedef struct {
    char code[TUYA_AI_ROLE_LANG_LEN];
    char name[TUYA_AI_ROLE_NAME_LEN];
    bool has_default;
} tuya_ai_language_t;

typedef struct {
    tuya_ai_language_t *items;
    size_t count;
} tuya_ai_language_list_t;

typedef struct {
    char id[TUYA_AI_ROLE_ID_LEN];
    char name[TUYA_AI_ROLE_NAME_LEN];
    /* Full language codes, comma-separated. An overlong value is rejected. */
    char support_langs[512];
} tuya_ai_timbre_t;

typedef struct {
    tuya_ai_timbre_t *items;
    size_t count;
    int total_count;
} tuya_ai_timbre_list_t;

/** NULL means unchanged; set_speed=false preserves speed. Strings are
 * borrowed only until this synchronous call returns. No prompt editing. */
typedef struct {
    const char *name;
    const char *lang_code;
    const char *timbre_id;
    bool set_speed;
    double speed; /* finite and positive; cloud decides supported range */
} tuya_ai_role_edit_t;

/** All operations return OPRT_* and clear their optional rejection on entry.
 * OPRT_ATOP_BUSINESS_ERROR preserves the cloud's code/msg in rejection.
 * Query outputs are empty on failure. Release lists with the matching free
 * function and SAME live context before replacing/reusing the output struct.
 * No cJSON object or SDK-private header crosses this public boundary.
 * Strings that exceed the public capacities are rejected, never truncated. */
int tuya_ai_role_get_bound(tuya_biz_client_t *, tuya_ai_role_t *, iot_atop_rejection_t *);
int tuya_ai_role_list_templates(tuya_biz_client_t *, tuya_ai_role_list_t *, iot_atop_rejection_t *);
int tuya_ai_role_list_custom(tuya_biz_client_t *, int page_no, int page_size,
                             tuya_ai_role_list_t *, iot_atop_rejection_t *);
int tuya_ai_role_bind(tuya_biz_client_t *, tuya_ai_role_type_t, const char *role_id,
                      iot_atop_rejection_t *);
/** Explicit deletion is custom-only. Binding/fallback policy stays in the app. */
int tuya_ai_role_delete_custom(tuya_biz_client_t *, const char *role_id,
                               iot_atop_rejection_t *);
/** Template edit creates a custom role with needBind=true; custom edit updates
 * it in place without rebinding. SINGLE and empty edits are rejected.
 * out_id must have TUYA_AI_ROLE_ID_LEN bytes; zeroed on failure. No auto-retry:
 * a timeout may mean creation succeeded. Refresh before submitting again. */
int tuya_ai_role_apply_edit(tuya_biz_client_t *, const tuya_ai_role_t *,
                            const tuya_ai_role_edit_t *,
                            char out_id[TUYA_AI_ROLE_ID_LEN], iot_atop_rejection_t *);
int tuya_ai_role_list_languages(tuya_biz_client_t *, tuya_ai_language_list_t *,
                                iot_atop_rejection_t *);
/** Full lang code is sent unchanged (e.g. zh_ha). NULL means unfiltered.
 * Pagination is explicit; no hidden first-page-only limit or automatic retry. */
int tuya_ai_role_list_timbres(tuya_biz_client_t *, const char *lang,
                             int page_no, int page_size, tuya_ai_timbre_list_t *,
                             iot_atop_rejection_t *);

void tuya_ai_role_list_free(tuya_biz_client_t *, tuya_ai_role_list_t *);
void tuya_ai_language_list_free(tuya_biz_client_t *, tuya_ai_language_list_t *);
void tuya_ai_timbre_list_free(tuya_biz_client_t *, tuya_ai_timbre_list_t *);

#ifdef __cplusplus
}
#endif
#endif
