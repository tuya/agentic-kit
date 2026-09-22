/*
 * AI control channel (MQTT protocol 9000) dispatch tests.
 *
 * Network-free: exercises iot_ai_ctrl_dispatch() and
 * iot_ai_ctrl_set_callback() directly.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "iot_ai_ctrl.h"
#include "iot_client.h"
#include "iot_internal.h"
#include "cJSON.h"

static int tests_run;
static int tests_passed;

#define RUN_TEST(fn)                                                     \
    do {                                                                 \
        tests_run++;                                                     \
        printf("\n--- [%d] %s ---\n", tests_run, #fn);                  \
        if ((fn)() == 0) {                                               \
            tests_passed++;                                              \
            printf("  PASS\n");                                         \
        } else {                                                         \
            printf("  FAIL\n");                                         \
        }                                                                \
    } while (0)

static int callback_count;
static char callback_type[64];
static char callback_data[512];
static void *callback_user_data;

static void reset_callback(void)
{
    callback_count = 0;
    callback_type[0] = '\0';
    callback_data[0] = '\0';
    callback_user_data = NULL;
}

static void control_callback(const char *type, const char *json_data,
                             size_t data_len, void *user_data)
{
    callback_count++;
    callback_user_data = user_data;

    if (type) {
        snprintf(callback_type, sizeof(callback_type), "%s", type);
    }
    if (json_data && data_len > 0) {
        size_t n = data_len < sizeof(callback_data) - 1
                     ? data_len : sizeof(callback_data) - 1;
        memcpy(callback_data, json_data, n);
        callback_data[n] = '\0';
    }
}

static iot_client_t *make_client(const pal_t *pal)
{
    iot_client_t *client = (iot_client_t *)pal->malloc(sizeof(*client));
    if (!client) return NULL;
    memset(client, 0, sizeof(*client));
    client->pal = pal;
    return client;
}

static void destroy_client(iot_client_t *client)
{
    if (client) client->pal->free(client);
}

static int test_asr_interrupt_dispatch(void)
{
    const pal_t *pal = get_default_pal();
    iot_client_t *client = make_client(pal);
    static char user_data;
    const char *envelope =
        "{\"protocol\":9000,\"t\":1234567890,\"data\":{"
        "\"bizType\":\"EVENT\",\"bizId\":\"uuid-1234\",\"data\":{"
        "\"type\":\"asrInterrupt\",\"data\":{"
        "\"eventId\":\"evt-5678\",\"time\":\"2025-01-15T10:30:00Z\"}}}}";
    int result = -1;

    if (!client) return -1;
    reset_callback();
    iot_ai_ctrl_set_callback(client, control_callback, &user_data);

    if (!iot_ai_ctrl_dispatch(client, (const uint8_t *)envelope,
                              strlen(envelope))) {
        printf("  protocol-9000 envelope was not consumed\n");
    } else if (callback_count != 1) {
        printf("  callback count=%d\n", callback_count);
    } else if (strcmp(callback_type, "asrInterrupt") != 0) {
        printf("  callback type=%s\n", callback_type);
    } else if (!strstr(callback_data, "\"eventId\":\"evt-5678\"")) {
        printf("  callback data=%s\n", callback_data);
    } else if (callback_user_data != &user_data) {
        printf("  callback user data mismatch\n");
    } else {
        result = 0;
    }

    destroy_client(client);
    return result;
}

static int test_non_9000_passthrough(void)
{
    const pal_t *pal = get_default_pal();
    iot_client_t *client = make_client(pal);
    static const char protocol_five[] =
        "{\"protocol\":5,\"data\":{\"dps\":{\"1\":true}}}";
    static const char no_protocol[] = "{\"type\":\"test\"}";
    static const char garbage[] = "not json";
    int result = -1;

    if (!client) return -1;
    reset_callback();
    iot_ai_ctrl_set_callback(client, control_callback, NULL);

    if (iot_ai_ctrl_dispatch(client, (const uint8_t *)protocol_five,
                             sizeof(protocol_five) - 1) ||
        iot_ai_ctrl_dispatch(client, (const uint8_t *)no_protocol,
                             sizeof(no_protocol) - 1) ||
        iot_ai_ctrl_dispatch(client, (const uint8_t *)garbage,
                             sizeof(garbage) - 1)) {
        printf("  non-control input was consumed\n");
    } else if (callback_count != 0) {
        printf("  callback fired on passthrough\n");
    } else {
        result = 0;
    }

    destroy_client(client);
    return result;
}

static int test_no_callback_passthrough(void)
{
    const pal_t *pal = get_default_pal();
    iot_client_t *client = make_client(pal);
    const char *envelope =
        "{\"protocol\":9000,\"data\":{\"data\":{"
        "\"type\":\"asrInterrupt\",\"data\":{}}}}";
    int consumed;

    if (!client) return -1;
    consumed = iot_ai_ctrl_dispatch(client, (const uint8_t *)envelope,
                                    strlen(envelope));
    destroy_client(client);
    return consumed ? -1 : 0;
}

static int test_missing_type_not_consumed(void)
{
    const pal_t *pal = get_default_pal();
    iot_client_t *client = make_client(pal);
    const char *envelope =
        "{\"protocol\":9000,\"data\":{\"data\":{"
        "\"data\":{\"key\":\"value\"}}}}";
    int result = -1;

    if (!client) return -1;
    reset_callback();
    iot_ai_ctrl_set_callback(client, control_callback, NULL);
    if (!iot_ai_ctrl_dispatch(client, (const uint8_t *)envelope,
                              strlen(envelope)) && callback_count == 0) {
        result = 0;
    }
    destroy_client(client);
    return result;
}

static int test_exact_length_without_nul(void)
{
    const pal_t *pal = get_default_pal();
    iot_client_t *client = make_client(pal);
    static const char json[] =
        "{\"protocol\":9000,\"data\":{\"data\":{"
        "\"type\":\"asrInterrupt\",\"data\":null}}}";
    size_t len = sizeof(json) - 1;
    uint8_t *bytes;
    int result = -1;

    if (!client) return -1;
    bytes = (uint8_t *)pal->malloc(len);
    if (!bytes) {
        destroy_client(client);
        return -1;
    }
    memcpy(bytes, json, len);
    reset_callback();
    iot_ai_ctrl_set_callback(client, control_callback, NULL);

    if (iot_ai_ctrl_dispatch(client, bytes, len) && callback_count == 1 &&
        strcmp(callback_type, "asrInterrupt") == 0 &&
        strcmp(callback_data, "null") == 0) {
        result = 0;
    }

    pal->free(bytes);
    destroy_client(client);
    return result;
}

static int test_null_safety(void)
{
    const pal_t *pal = get_default_pal();
    iot_client_t *client = make_client(pal);
    int failed;

    if (!client) return -1;
    iot_ai_ctrl_set_callback(client, control_callback, NULL);
    failed = iot_ai_ctrl_dispatch(NULL, (const uint8_t *)"x", 1) ||
             iot_ai_ctrl_dispatch(client, NULL, 0);
    destroy_client(client);
    return failed ? -1 : 0;
}

static int test_deregister(void)
{
    const pal_t *pal = get_default_pal();
    iot_client_t *client = make_client(pal);
    const char *envelope =
        "{\"protocol\":9000,\"data\":{\"data\":{"
        "\"type\":\"asrInterrupt\",\"data\":{}}}}";
    int consumed;

    if (!client) return -1;
    reset_callback();
    iot_ai_ctrl_set_callback(client, control_callback, NULL);
    iot_ai_ctrl_set_callback(client, NULL, NULL);
    consumed = iot_ai_ctrl_dispatch(client, (const uint8_t *)envelope,
                                    strlen(envelope));
    destroy_client(client);
    return consumed || callback_count != 0 ? -1 : 0;
}

static int test_set_callback_null_client(void)
{
    return iot_ai_ctrl_set_callback(NULL, control_callback, NULL) ==
           OPRT_INVALID_PARAMETER ? 0 : -1;
}

static int test_protocol_must_be_exact_integer(void)
{
    const pal_t *pal = get_default_pal();
    iot_client_t *client = make_client(pal);
    const char *fractional =
        "{\"protocol\":9000.5,\"data\":{\"data\":{"
        "\"type\":\"asrInterrupt\",\"data\":{}}}}";
    const char *string_value =
        "{\"protocol\":\"9000\",\"data\":{\"data\":{"
        "\"type\":\"asrInterrupt\",\"data\":{}}}}";
    int failed;

    if (!client) return -1;
    reset_callback();
    iot_ai_ctrl_set_callback(client, control_callback, NULL);
    failed = iot_ai_ctrl_dispatch(client, (const uint8_t *)fractional,
                                  strlen(fractional)) ||
             iot_ai_ctrl_dispatch(client, (const uint8_t *)string_value,
                                  strlen(string_value)) ||
             callback_count != 0;
    destroy_client(client);
    return failed ? -1 : 0;
}

static int cjson_allocation_count;
static int cjson_fail_after;

static void *counting_malloc(size_t size)
{
    cjson_allocation_count++;
    if (cjson_fail_after >= 0 && cjson_allocation_count > cjson_fail_after)
        return NULL;
    return malloc(size);
}

static int test_payload_print_allocation_failure_is_unconsumed(void)
{
    const pal_t *pal = get_default_pal();
    iot_client_t *client = make_client(pal);
    const char *envelope =
        "{\"protocol\":9000,\"data\":{\"data\":{"
        "\"type\":\"asrInterrupt\",\"data\":{\"eventId\":\"evt-1\"}}}}";
    cJSON_Hooks counting_hooks = { counting_malloc, pal->free };
    cJSON_Hooks restore_hooks = { pal->malloc, pal->free };
    int result = -1;

    if (!client) return -1;
    reset_callback();
    iot_ai_ctrl_set_callback(client, control_callback, NULL);

    cJSON_InitHooks(&counting_hooks);
    cjson_allocation_count = 0;
    cjson_fail_after = -1;
    cJSON *root = cJSON_ParseWithLength(envelope, strlen(envelope));
    if (root) cJSON_Delete(root);
    if (cjson_allocation_count <= 0) goto out;

    /* Sweep one failing allocation index at a time. The successful run proves
     * every earlier run failed before the callback; no scoped payload was
     * rewritten to an empty string. */
    result = -1;
    for (cjson_fail_after = 0; cjson_fail_after < 32; cjson_fail_after++) {
        cjson_allocation_count = 0;
        int consumed = iot_ai_ctrl_dispatch(client, (const uint8_t *)envelope,
                                            strlen(envelope));
        if (consumed) {
            result = callback_count == 1 ? 0 : -1;
            break;
        }
        if (callback_count != 0) break;
    }

out:
    cJSON_InitHooks(&restore_hooks);
    destroy_client(client);
    return result;
}

int main(void)
{
    const pal_t *pal = get_default_pal();

    setbuf(stdout, NULL);
    setbuf(stderr, NULL);
    printf("========== IoT AI Control Channel Test Suite ==========\n");

    if (iot_init(pal) != OPRT_OK) return 1;

    RUN_TEST(test_asr_interrupt_dispatch);
    RUN_TEST(test_non_9000_passthrough);
    RUN_TEST(test_no_callback_passthrough);
    RUN_TEST(test_missing_type_not_consumed);
    RUN_TEST(test_exact_length_without_nul);
    RUN_TEST(test_null_safety);
    RUN_TEST(test_deregister);
    RUN_TEST(test_set_callback_null_client);
    RUN_TEST(test_protocol_must_be_exact_integer);
    RUN_TEST(test_payload_print_allocation_failure_is_unconsumed);

    printf("\n========== Results: %d/%d passed ==========\n",
           tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
