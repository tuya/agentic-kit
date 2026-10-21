#include "role_demo.h"
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void role_demo_usage(void)
{
    puts("role_management_demo COMMAND [arguments]\n"
         "  bound | templates | languages\n"
         "  custom PAGE SIZE\n"
         "  timbres LANG PAGE SIZE\n"
         "  bind template|custom ROLE_ID\n"
         "  edit template|custom ROLE_ID name=NAME lang=CODE timbre=ID speed=1.20\n"
         "    (supply only the fields to change)\n"
         "  delete custom ROLE_ID --confirm\n"
         "Credentials come from TUYA_DEVICE_ID, TUYA_SECRET_KEY, TUYA_LOCAL_KEY;\n"
         "TUYA_CA_FILE is required; TUYA_REGION defaults to AY. Secrets are not printed.");
}

static int positive_integer(const char *s)
{
    char *end; errno = 0;
    long n = strtol(s, &end, 10);
    return errno || end == s || *end || n < 1 || n > INT_MAX ? 0 : (int)n;
}
static int role_type(const char *s)
{
    return !strcmp(s, "template") ? TUYA_AI_ROLE_TEMPLATE :
           !strcmp(s, "custom") ? TUYA_AI_ROLE_CUSTOM : -1;
}
static int current(tuya_biz_client_t *biz, iot_atop_rejection_t *err)
{
    tuya_ai_role_t role;
    int rc = tuya_ai_role_get_bound(biz, &role, err);
    if (!rc) printf("bound: id=%s type=%d name=%s lang=%s timbre=%s speed=%.2f\n",
                    role.role_id, role.type, role.name, role.lang_code, role.timbre_id, role.speed);
    return rc;
}

int role_demo_execute(tuya_biz_client_t *biz, int argc, const char *const argv[])
{
    if (!biz || argc < 1) return OPRT_INVALID_PARAMETER;
    iot_atop_rejection_t err = {0};
    int rc = OPRT_INVALID_PARAMETER;
    bool changed = false;
    if (!strcmp(argv[0], "bound") && argc == 1) rc = current(biz, &err);
    else if ((!strcmp(argv[0], "templates") && argc == 1) ||
             (!strcmp(argv[0], "custom") && argc == 3)) {
        tuya_ai_role_list_t list = {0};
        rc = argc == 1 ? tuya_ai_role_list_templates(biz, &list, &err) :
             tuya_ai_role_list_custom(biz, positive_integer(argv[1]), positive_integer(argv[2]), &list, &err);
        if (!rc) {
            printf("roles: %zu, total=%d\n", list.count, list.total_count);
            for (size_t i = 0; i < list.count; ++i)
                printf("  %s type=%d %s\n", list.items[i].role_id, list.items[i].type, list.items[i].name);
        }
        tuya_ai_role_list_free(biz, &list);
    } else if (!strcmp(argv[0], "languages") && argc == 1) {
        tuya_ai_language_list_t list = {0};
        rc = tuya_ai_role_list_languages(biz, &list, &err);
        if (!rc) for (size_t i = 0; i < list.count; ++i)
            printf("%s %s\n", list.items[i].code, list.items[i].name);
        tuya_ai_language_list_free(biz, &list);
    } else if (!strcmp(argv[0], "timbres") && argc == 4) {
        tuya_ai_timbre_list_t list = {0};
        rc = tuya_ai_role_list_timbres(biz, argv[1], positive_integer(argv[2]),
                                      positive_integer(argv[3]), &list, &err);
        if (!rc) {
            printf("timbres: %zu, total=%d\n", list.count, list.total_count);
            for (size_t i = 0; i < list.count; ++i)
                printf("%s %s [%s]\n", list.items[i].id, list.items[i].name, list.items[i].support_langs);
        }
        tuya_ai_timbre_list_free(biz, &list);
    } else if (!strcmp(argv[0], "bind") && argc == 3 && role_type(argv[1]) >= 0) {
        rc = tuya_ai_role_bind(biz, (tuya_ai_role_type_t)role_type(argv[1]), argv[2], &err);
        changed = !rc;
    } else if (!strcmp(argv[0], "edit") && argc >= 4 && role_type(argv[1]) >= 0) {
        tuya_ai_role_t role = { .type = (tuya_ai_role_type_t)role_type(argv[1]) };
        tuya_ai_role_edit_t edit = {0};
        if (strlen(argv[2]) >= sizeof(role.role_id)) goto done;
        strcpy(role.role_id, argv[2]);
        for (int i = 3; i < argc; ++i) {
            if (!strncmp(argv[i], "name=", 5)) edit.name = argv[i] + 5;
            else if (!strncmp(argv[i], "lang=", 5)) edit.lang_code = argv[i] + 5;
            else if (!strncmp(argv[i], "timbre=", 7)) edit.timbre_id = argv[i] + 7;
            else if (!strncmp(argv[i], "speed=", 6)) {
                char *end; errno = 0;
                edit.speed = strtod(argv[i] + 6, &end);
                if (errno || end == argv[i] + 6 || *end) goto done;
                edit.set_speed = true;
            } else goto done;
        }
        char id[TUYA_AI_ROLE_ID_LEN];
        rc = tuya_ai_role_apply_edit(biz, &role, &edit, id, &err);
        if (!rc) printf("saved role: %s\n", id);
        changed = !rc;
    } else if (!strcmp(argv[0], "delete") && argc == 4 &&
               !strcmp(argv[1], "custom") && !strcmp(argv[3], "--confirm")) {
        tuya_ai_role_t bound;
        rc = tuya_ai_role_get_bound(biz, &bound, &err);
        if (!rc && !strcmp(bound.role_id, argv[2])) {
            fprintf(stderr, "Select another role before deleting the current role.\n");
            rc = OPRT_INVALID_PARAMETER;
        } else if (!rc) {
            rc = tuya_ai_role_delete_custom(biz, argv[2], &err);
            changed = !rc;
        }
    }
    if (changed) {
        puts("Cloud change accepted; refreshing bound role (not a voice-effect check).");
        rc = current(biz, &err);
        if (rc) fprintf(stderr, "Change accepted, refresh failed; query before retrying a write.\n");
    }
done:
    if (rc) fprintf(stderr, "role operation failed: rc=%d cloud=%s %s\n", rc, err.code, err.msg);
    return rc;
}
