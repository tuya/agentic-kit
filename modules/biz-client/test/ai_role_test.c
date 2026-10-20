/* Typed service contract tests. The ATOP boundary is faked; SDK generic-call
 * encryption/TLS/envelope behavior remains covered by iot_atop_call_test. */
#include "tuya_ai_role.h"
#include "iot_atop.h"
#include "cJSON.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

static int calls, frees, live, fail_after = -1;
static int response_rc;
static const char *response_json;
static char api[128];
static cJSON *request_body;
static pal_t pal;
static iot_client_t iot;
static tuya_biz_client_t *biz;
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __func__, __LINE__, #x); return 1; } } while (0)

static void *allocate(size_t n)
{
    if (fail_after == 0) return NULL;
    if (fail_after > 0) --fail_after;
    void *p = malloc(n);
    if (p) ++live;
    return p;
}
static void release(void *p) { if (p) { --live; free(p); } }

int iot_atop_call(iot_client_t *client, const iot_atop_request_t *req,
                  iot_atop_response_t *out)
{
    (void)client;
    ++calls;
    memset(out, 0, sizeof(*out));
    snprintf(api, sizeof(api), "%s", req->api);
    if (strcmp(req->version, "1.0")) return OPRT_INVALID_PARAMETER;
    cJSON_Delete(request_body);
    request_body = cJSON_Parse(req->data);
    if (response_rc == OPRT_ATOP_BUSINESS_ERROR) {
        strcpy(out->error_code, "ROLE_NOT_ALLOWED");
        strcpy(out->error_msg, "role unavailable");
    }
    if (response_json) {
        out->result = allocate(strlen(response_json) + 1);
        if (!out->result) return OPRT_MALLOC_FAILED;
        strcpy(out->result, response_json);
    }
    return response_rc;
}
void iot_atop_response_free(iot_client_t *client, iot_atop_response_t *out)
{
    (void)client; ++frees; release(out->result); memset(out, 0, sizeof(*out));
}
static const char *str(const char *key)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(request_body, key);
    return cJSON_IsString(v) ? v->valuestring : "";
}
static int number(const char *key)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(request_body, key);
    return cJSON_IsNumber(v) ? v->valueint : -1;
}
static void response(const char *json) { response_rc = OPRT_OK; response_json = json; }

static int lifecycle(void)
{
    tuya_biz_client_t *out = (void *)1;
    CHECK(tuya_biz_client_create(NULL, NULL, &out) == OPRT_INVALID_PARAMETER);
    CHECK(out == NULL);
    CHECK(tuya_biz_client_create(&iot, "", &out) == OPRT_INVALID_PARAMETER);
    fail_after = 0;
    CHECK(tuya_biz_client_create(&iot, NULL, &out) == OPRT_MALLOC_FAILED);
    fail_after = -1;
    CHECK(tuya_biz_client_create(&iot, "en", &out) == OPRT_OK);
    response("[]"); tuya_ai_role_list_t list = {0};
    CHECK(tuya_ai_role_list_templates(out, &list, NULL) == OPRT_OK);
    CHECK(strcmp(str("requestLang"), "en") == 0);
    tuya_ai_role_list_free(out, &list);
    tuya_biz_client_destroy(out);
    tuya_biz_client_destroy(NULL);
    return 0;
}

static int bound_and_templates(void)
{
    response("{\"roleId\":\"r1\",\"bindRoleType\":1,\"roleName\":\"Buddy\",\"useLangCode\":\"zh_ha\",\"speed\":1.2}");
    tuya_ai_role_t role;
    CHECK(tuya_ai_role_get_bound(biz, &role, NULL) == OPRT_OK);
    CHECK(!strcmp(api, "thing.ai.agent.role.get-bind-role"));
    CHECK(!strcmp(role.role_id, "r1") && role.type == TUYA_AI_ROLE_TEMPLATE);
    CHECK(!strcmp(role.lang_code, "zh_ha") && role.speed == 1.2);
    CHECK(number("t") > 1700000000 && !strcmp(str("requestLang"), "zh"));
    response("[{\"roleId\":\"template-1\"}]");
    tuya_ai_role_list_t list = {0};
    CHECK(tuya_ai_role_list_templates(biz, &list, NULL) == OPRT_OK);
    CHECK(list.count == 1 && list.items[0].type == TUYA_AI_ROLE_TEMPLATE);
    CHECK(list.items[0].speed == 1.0);
    tuya_ai_role_list_free(biz, &list);
    CHECK(!list.items && list.count == 0);
    tuya_ai_role_list_free(biz, &list);
    return 0;
}

static int list_shapes_and_pagination(void)
{
    const char *keys[] = {"datas", "list", "records", "rows", "content", "data", "items"};
    for (size_t i = 0; i < sizeof(keys)/sizeof(keys[0]); ++i) {
        char json[160];
        snprintf(json, sizeof(json), "{\"%s\":[{\"roleId\":\"c1\"}],\"totalCount\":35}", keys[i]);
        response(json); tuya_ai_role_list_t list = {0};
        CHECK(tuya_ai_role_list_custom(biz, 2, 7, &list, NULL) == OPRT_OK);
        CHECK(!strcmp(api, "thing.ai.agent.role.custom-role.page"));
        CHECK(number("pageNo") == 2 && number("pageSize") == 7);
        CHECK(list.total_count == 35 && list.items[0].type == TUYA_AI_ROLE_CUSTOM);
        tuya_ai_role_list_free(biz, &list);
    }
    response("[]"); tuya_ai_role_list_t list = {0};
    CHECK(tuya_ai_role_list_custom(biz, 1, 1, &list, NULL) == OPRT_OK);
    CHECK(!list.items && !list.count && list.total_count == -1);
    int before = calls;
    CHECK(tuya_ai_role_list_custom(biz, 0, 20, &list, NULL) == OPRT_INVALID_PARAMETER);
    CHECK(tuya_ai_role_list_custom(biz, 1, 0, &list, NULL) == OPRT_INVALID_PARAMETER);
    CHECK(calls == before);
    return 0;
}

static int bind_and_delete(void)
{
    response("true");
    CHECK(tuya_ai_role_bind(biz, TUYA_AI_ROLE_TEMPLATE, "id\"quoted", NULL) == OPRT_OK);
    CHECK(!strcmp(api, "thing.ai.agent.role.bind-with-role"));
    CHECK(number("bindRoleType") == 1 && !strcmp(str("roleId"), "id\"quoted"));
    CHECK(tuya_ai_role_delete_custom(biz, "c1", NULL) == OPRT_OK);
    CHECK(!strcmp(api, "thing.ai.agent.role.custom-role.delete"));
    CHECK(!strcmp(str("roleId"), "c1"));
    return 0;
}

static int template_edit_creates_and_binds(void)
{
    tuya_ai_role_t cur = { .type = TUYA_AI_ROLE_TEMPLATE };
    strcpy(cur.role_id, "template-1");
    strcpy(cur.template_id, "not-the-role-id");
    tuya_ai_role_edit_t edit = { .name = "Hello \"friend\"", .lang_code = "zh_ha",
        .timbre_id = "voice-1", .set_speed = true, .speed = 1.2 };
    char id[TUYA_AI_ROLE_ID_LEN];
    response("\"new-custom\"");
    CHECK(tuya_ai_role_apply_edit(biz, &cur, &edit, id, NULL) == OPRT_OK);
    CHECK(!strcmp(api, "thing.ai.agent.role.custom-role.create-from-template"));
    CHECK(!strcmp(id, "new-custom") && !strcmp(str("roleId"), "template-1"));
    CHECK(!strcmp(str("roleName"), edit.name));
    CHECK(!strcmp(str("useLangCode"), "zh_ha") && !strcmp(str("speed"), "1.20"));
    CHECK(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(request_body, "needBind")));
    return 0;
}

static int custom_edit_preserves_omitted_fields(void)
{
    tuya_ai_role_t cur = { .type = TUYA_AI_ROLE_CUSTOM };
    strcpy(cur.role_id, "custom-1");
    tuya_ai_role_edit_t edit = { .name = "New name" };
    char id[TUYA_AI_ROLE_ID_LEN]; response("true");
    CHECK(tuya_ai_role_apply_edit(biz, &cur, &edit, id, NULL) == OPRT_OK);
    CHECK(!strcmp(api, "thing.ai.agent.role.custom-role.update") && !strcmp(id, "custom-1"));
    CHECK(!cJSON_HasObjectItem(request_body, "needBind"));
    CHECK(!cJSON_HasObjectItem(request_body, "useLangCode"));
    CHECK(!cJSON_HasObjectItem(request_body, "speed"));
    response("false");
    CHECK(tuya_ai_role_apply_edit(biz, &cur, &edit, id, NULL) == OPRT_INVALID_RESULT);
    CHECK(id[0] == 0);
    return 0;
}

static int languages_and_timbres(void)
{
    response("[{\"langCode\":\"zh_ha\",\"langName\":\"Dialect\",\"hasDefault\":true}]");
    tuya_ai_language_list_t langs = {0};
    CHECK(tuya_ai_role_list_languages(biz, &langs, NULL) == OPRT_OK);
    CHECK(!strcmp(api, "thing.ai.agent.config.languages.list"));
    CHECK(langs.count == 1 && !strcmp(langs.items[0].code, "zh_ha") && langs.items[0].has_default);
    tuya_ai_language_list_free(biz, &langs);
    response("{\"datas\":[{\"voiceId\":\"v1\",\"voiceName\":\"Voice\",\"supportLangs\":[\"zh_ha\",\"en_US\"]}],\"totalCount\":77}");
    tuya_ai_timbre_list_t voices = {0};
    CHECK(tuya_ai_role_list_timbres(biz, "zh_ha", 2, 3, &voices, NULL) == OPRT_OK);
    CHECK(!strcmp(api, "thing.ai.timbre.market.page"));
    CHECK(!strcmp(str("lang"), "zh_ha") && number("pageNo") == 2 && number("pageSize") == 3);
    CHECK(voices.total_count == 77 && !strcmp(voices.items[0].support_langs, "zh_ha,en_US"));
    tuya_ai_timbre_list_free(biz, &voices);
    return 0;
}

static int malformed_results_and_rejections(void)
{
    const char *bad[] = {NULL, "null", "{}", "{", "[]junk", "false", "[{}]", "{\"datas\":{}}"};
    for (size_t i = 0; i < sizeof(bad)/sizeof(bad[0]); ++i) {
        response(bad[i]); tuya_ai_role_list_t list = {0};
        CHECK(tuya_ai_role_list_templates(biz, &list, NULL) == OPRT_INVALID_RESULT);
        CHECK(!list.items && !list.count);
    }
    response_rc = OPRT_ATOP_BUSINESS_ERROR; response_json = NULL;
    tuya_ai_role_t role; iot_atop_rejection_t rejection;
    CHECK(tuya_ai_role_get_bound(biz, &role, &rejection) == OPRT_ATOP_BUSINESS_ERROR);
    CHECK(!strcmp(rejection.code, "ROLE_NOT_ALLOWED") && role.role_id[0] == 0);
    response_rc = OPRT_COMMUNICATION_ERROR;
    CHECK(tuya_ai_role_get_bound(biz, &role, &rejection) == OPRT_COMMUNICATION_ERROR);
    CHECK(rejection.code[0] == 0);
    response("{}");
    CHECK(tuya_ai_role_get_bound(biz, &role, NULL) == OPRT_INVALID_RESULT);
    CHECK(calls == frees);
    return 0;
}

static int invalid_arguments_do_not_call_cloud(void)
{
    int before = calls; char id[TUYA_AI_ROLE_ID_LEN];
    tuya_ai_role_t cur = { .type = TUYA_AI_ROLE_TEMPLATE };
    strcpy(cur.role_id, "t1");
    tuya_ai_role_edit_t edit = {0};
    CHECK(tuya_ai_role_apply_edit(biz, &cur, &edit, id, NULL) == OPRT_INVALID_PARAMETER);
    edit.set_speed = true; edit.speed = NAN;
    CHECK(tuya_ai_role_apply_edit(biz, &cur, &edit, id, NULL) == OPRT_INVALID_PARAMETER);
    edit.speed = -1;
    CHECK(tuya_ai_role_apply_edit(biz, &cur, &edit, id, NULL) == OPRT_INVALID_PARAMETER);
    edit.speed = 1; cur.type = TUYA_AI_ROLE_SINGLE;
    CHECK(tuya_ai_role_apply_edit(biz, &cur, &edit, id, NULL) == OPRT_INVALID_PARAMETER);
    CHECK(tuya_ai_role_bind(biz, (tuya_ai_role_type_t)8, "x", NULL) == OPRT_INVALID_PARAMETER);
    CHECK(tuya_ai_role_delete_custom(biz, "", NULL) == OPRT_INVALID_PARAMETER);
    CHECK(tuya_ai_role_get_bound(biz, NULL, NULL) == OPRT_INVALID_PARAMETER);
    CHECK(calls == before);
    return 0;
}

static int failed_creation_and_overlong_fields(void)
{
    tuya_ai_role_t cur = { .type = TUYA_AI_ROLE_TEMPLATE }; strcpy(cur.role_id, "tpl");
    tuya_ai_role_edit_t edit = { .name = "Renamed" }; char id[TUYA_AI_ROLE_ID_LEN];
    const char *bad_ids[] = {"true", "{}", "\"\"", "null"};
    for (size_t i = 0; i < sizeof(bad_ids)/sizeof(bad_ids[0]); ++i) {
        response(bad_ids[i]);
        CHECK(tuya_ai_role_apply_edit(biz, &cur, &edit, id, NULL) == OPRT_INVALID_RESULT);
        CHECK(!id[0]);
    }
    char json[256]; memset(json, 'x', sizeof(json));
    snprintf(json, sizeof(json), "[{\"roleId\":\"%080d\"}]", 1);
    response(json); tuya_ai_role_list_t list = {0};
    CHECK(tuya_ai_role_list_templates(biz, &list, NULL) == OPRT_INVALID_RESULT);
    CHECK(!list.items && !list.count);
    return 0;
}

static int allocation_failures_release_everything(void)
{
    response("[{\"roleId\":\"t1\"}]");
    cJSON_Delete(request_body); request_body = NULL;
    int baseline = live; bool succeeded = false;
    for (int n = 0; n < 90; ++n) {
        fail_after = n;
        tuya_ai_role_list_t list = {0};
        int rc = tuya_ai_role_list_templates(biz, &list, NULL);
        fail_after = -1;
        CHECK(rc == OPRT_OK || rc == OPRT_MALLOC_FAILED || rc == OPRT_INVALID_RESULT);
        tuya_ai_role_list_free(biz, &list);
        cJSON_Delete(request_body); request_body = NULL;
        CHECK(live == baseline);
        if (rc == OPRT_OK) { succeeded = true; break; }
    }
    CHECK(succeeded);
    return 0;
}

int main(void)
{
    cJSON_Hooks hooks = {allocate, release}; cJSON_InitHooks(&hooks);
    pal.malloc = allocate; pal.free = release; iot.pal = &pal;
    if (tuya_biz_client_create(&iot, NULL, &biz) != OPRT_OK) return 1;
    int (*tests[])(void) = { lifecycle, bound_and_templates, list_shapes_and_pagination,
        bind_and_delete, template_edit_creates_and_binds, custom_edit_preserves_omitted_fields,
        languages_and_timbres, malformed_results_and_rejections, invalid_arguments_do_not_call_cloud,
        failed_creation_and_overlong_fields, allocation_failures_release_everything };
    int failed = 0;
    for (size_t i = 0; i < sizeof(tests)/sizeof(tests[0]); ++i) failed += tests[i]();
    cJSON_Delete(request_body); tuya_biz_client_destroy(biz);
    if (live) { fprintf(stderr, "unreleased allocations: %d\n", live); ++failed; }
    printf("biz-client: %zu cases, %d failed\n", sizeof(tests)/sizeof(tests[0]), failed);
    return failed ? 1 : 0;
}
