/*
 * AI control channel (MQTT protocol 9000) dispatch tests.
 *
 * Network-free: exercises iot_ai_ctrl_dispatch() and iot_ai_ctrl_set_callback()
 * directly on a hand-built iot_client_t — no MQTT connection needed.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "iot_client.h"
#include "iot_config_defaults.h"
#include "iot_ai_ctrl.h"

#include "cJSON.h"

/* ---- mock endpoints (required by test harness macros, not used here) ---- */
#define MSG_MOCK_PORT   11885
#define MSG_MOCK_URL    "mqtts://127.0.0.1:11885"
#define ATOP_MOCK_HOST  "127.0.0.1"
#define ATOP_MOCK_PORT  8443
#define ATOP_MOCK_URL   "https://127.0.0.1:8443"

/* device identity that matches test/config/atop.conf (sec_key / device_id) */
#define TEST_DEVID      "ci_device_test_001"
#define TEST_SECRET_KEY "1234567890abcdef"
#define TEST_LOCAL_KEY  "0123456789abcdef"

static int tests_run = 0;
static int tests_passed = 0;

#define RUN_TEST(fn)                                       \
    do {                                                   \
        tests_run++;                                       \
        printf("\n--- [%d] %s ---\n", tests_run, #fn);     \
        if ((fn)() == 0) { tests_passed++; printf("  PASS\n"); } \
        else { printf("  FAIL\n"); }                       \
    } while (0)

/* ---- callback capture ---- */
static int  ctrl_cb_count;
static char ctrl_cb_type[64];
static char ctrl_cb_data[512];

static void reset_ctrl_cb(void)
{
    ctrl_cb_count = 0;
    memset(ctrl_cb_type, 0, sizeof(ctrl_cb_type));
    memset(ctrl_cb_data, 0, sizeof(ctrl_cb_data));
}

static void ctrl_callback(const char *type, const char *json_data,
                           size_t data_len, void *user_data)
{
    (void)user_data;
    if (ctrl_cb_count >= 16) return;
    ctrl_cb_count++;
    if (type) {
        strncpy(ctrl_cb_type, type, sizeof(ctrl_cb_type) - 1);
        ctrl_cb_type[sizeof(ctrl_cb_type) - 1] = '\0';
    }
    if (json_data && data_len > 0) {
        size_t n = data_len < sizeof(ctrl_cb_data) - 1 ? data_len : sizeof(ctrl_cb_data) - 1;
        memcpy(ctrl_cb_data, json_data, n);
        ctrl_cb_data[n] = '\0';
    }
}

/* ---- helpers ---- */

static iot_client_t *make_client(const pal_t *pal)
{
    iot_client_t *client = (iot_client_t *)pal->malloc(sizeof(iot_client_t));
    if (!client) return NULL;
    memset(client, 0, sizeof(iot_client_t));
    client->pal = pal;
    strncpy(client->devid, TEST_DEVID, sizeof(client->devid) - 1);
    strncpy(client->secret_key, TEST_SECRET_KEY, sizeof(client->secret_key) - 1);
    strncpy(client->local_key, TEST_LOCAL_KEY, sizeof(client->local_key) - 1);
    return client;
}

static void destroy_client(iot_client_t *client)
{
    if (!client) return;
    client->pal->free(client);
}

/* ============================================================================
 * Tests
 * ============================================================================ */

/* [1] Protocol 9000 with asrInterrupt fires the callback with correct type/data. */
static int test_asr_interrupt_dispatch(void)
{
    const pal_t *pal = get_default_pal();
    iot_client_t *c = make_client(pal);
    int rc = -1;
    reset_ctrl_cb();
    iot_ai_ctrl_set_callback(c, ctrl_callback, NULL);

    const char *envelope =
        "{\"protocol\":9000,\"t\":1234567890,\"data\":{"
        "\"bizType\":\"EVENT\",\"bizId\":\"uuid-1234\","
        "\"data\":{"
        "\"type\":\"asrInterrupt\","
        "\"data\":{\"eventId\":\"evt-5678\",\"time\":\"2025-01-15T10:30:00Z\"}"
        "}}}";

    if (!iot_ai_ctrl_dispatch(c, (const uint8_t *)envelope, strlen(envelope))) {
        printf("  dispatch did not consume protocol-9000 envelope\n"); goto out;
    }
    if (ctrl_cb_count != 1) { printf("  callback fired %d times (expected 1)\n", ctrl_cb_count); goto out; }
    if (strcmp(ctrl_cb_type, "asrInterrupt") != 0) { printf("  type mismatch: %s\n", ctrl_cb_type); goto out; }
    if (!strstr(ctrl_cb_data, "\"eventId\":\"evt-5678\"")) { printf("  data missing eventId: %s\n", ctrl_cb_data); goto out; }
    if (!strstr(ctrl_cb_data, "\"time\":\"2025-01-15T10:30:00Z\"")) { printf("  data missing time\n"); goto out; }
    rc = 0;
out:
    destroy_client(c);
    return rc;
}

/* [2] Non-9000 protocol is NOT consumed (passthrough to DP/raw callback). */
static int test_non_9000_passthrough(void)
{
    const pal_t *pal = get_default_pal();
    iot_client_t *c = make_client(pal);
    int rc = -1;
    reset_ctrl_cb();
    iot_ai_ctrl_set_callback(c, ctrl_callback, NULL);

    if (iot_ai_ctrl_dispatch(c, (const uint8_t *)"{\"protocol\":5,\"data\":{\"dps\":{\"1\":true}}}", 42)) {
        printf("  protocol 5 was consumed by ai_ctrl\n"); goto out;
    }
    if (iot_ai_ctrl_dispatch(c, (const uint8_t *)"{\"type\":\"test\"}", 16)) {
        printf("  non-protocol JSON consumed\n"); goto out;
    }
    if (iot_ai_ctrl_dispatch(c, (const uint8_t *)"not json", 8)) {
        printf("  non-JSON consumed\n"); goto out;
    }
    if (ctrl_cb_count != 0) { printf("  callback should not have fired\n"); goto out; }
    rc = 0;
out:
    destroy_client(c);
    return rc;
}

/* [3] No callback registered -> dispatch returns false (not consumed). */
static int test_no_callback_passthrough(void)
{
    const pal_t *pal = get_default_pal();
    iot_client_t *c = make_client(pal);
    int rc = -1;
    /* Do NOT register a callback */

    const char *envelope =
        "{\"protocol\":9000,\"data\":{\"data\":{\"type\":\"asrInterrupt\",\"data\":{}}}}";
    if (iot_ai_ctrl_dispatch(c, (const uint8_t *)envelope, strlen(envelope))) {
        printf("  consumed without callback registered\n"); goto out;
    }
    rc = 0;
out:
    destroy_client(c);
    return rc;
}

/* [4] Protocol 9000 but missing type field -> not consumed (malformed). */
static int test_missing_type_not_consumed(void)
{
    const pal_t *pal = get_default_pal();
    iot_client_t *c = make_client(pal);
    int rc = -1;
    reset_ctrl_cb();
    iot_ai_ctrl_set_callback(c, ctrl_callback, NULL);

    const char *envelope =
        "{\"protocol\":9000,\"data\":{\"data\":{\"data\":{\"key\":\"val\"}}}}";
    if (iot_ai_ctrl_dispatch(c, (const uint8_t *)envelope, strlen(envelope))) {
        printf("  consumed envelope with no type field\n"); goto out;
    }
    if (ctrl_cb_count != 0) { printf("  callback fired on malformed envelope\n"); goto out; }
    rc = 0;
out:
    destroy_client(c);
    return rc;
}

/* [5] NULL client or NULL bytes -> false, no crash. */
static int test_null_safety(void)
{
    if (iot_ai_ctrl_dispatch(NULL, (const uint8_t *)"x", 1)) {
        printf("  NULL client consumed\n"); return -1;
    }
    const pal_t *pal = get_default_pal();
    iot_client_t *c = make_client(pal);
    iot_ai_ctrl_set_callback(c, ctrl_callback, NULL);
    if (iot_ai_ctrl_dispatch(c, NULL, 0)) {
        printf("  NULL bytes consumed\n"); destroy_client(c); return -1;
    }
    destroy_client(c);
    return 0;
}

/* [6] set_callback with NULL deregisters (dispatch returns false). */
static int test_deregister(void)
{
    const pal_t *pal = get_default_pal();
    iot_client_t *c = make_client(pal);
    int rc = -1;
    reset_ctrl_cb();
    iot_ai_ctrl_set_callback(c, ctrl_callback, NULL);
    iot_ai_ctrl_set_callback(c, NULL, NULL);  /* deregister */

    const char *envelope =
        "{\"protocol\":9000,\"data\":{\"data\":{\"type\":\"asrInterrupt\",\"data\":{}}}}";
    if (iot_ai_ctrl_dispatch(c, (const uint8_t *)envelope, strlen(envelope))) {
        printf("  consumed after deregister\n"); goto out;
    }
    if (ctrl_cb_count != 0) { printf("  callback fired after deregister\n"); goto out; }
    rc = 0;
out:
    destroy_client(c);
    return rc;
}

/* [7] set_callback rejects NULL client. */
static int test_set_callback_null_client(void)
{
    if (iot_ai_ctrl_set_callback(NULL, ctrl_callback, NULL) != OPRT_INVALID_PARAMETER) {
        printf("  expected OPRT_INVALID_PARAMETER\n"); return -1;
    }
    return 0;
}

/* [8] Protocol field as float (9000.0) is still consumed. */
static int test_protocol_float(void)
{
    const pal_t *pal = get_default_pal();
    iot_client_t *c = make_client(pal);
    int rc = -1;
    reset_ctrl_cb();
    iot_ai_ctrl_set_callback(c, ctrl_callback, NULL);

    const char *envelope =
        "{\"protocol\":9000.0,\"data\":{\"data\":{\"type\":\"asrInterrupt\",\"data\":{}}}}";
    if (!iot_ai_ctrl_dispatch(c, (const uint8_t *)envelope, strlen(envelope))) {
        printf("  float protocol not consumed\n"); goto out;
    }
    if (ctrl_cb_count != 1) { printf("  callback fired %d times (expected 1)\n", ctrl_cb_count); goto out; }
    if (strcmp(ctrl_cb_type, "asrInterrupt") != 0) { printf("  type mismatch: %s\n", ctrl_cb_type); goto out; }
    rc = 0;
out:
    destroy_client(c);
    return rc;
}

/* [9] Protocol field as string ("9000") is NOT consumed. */
static int test_protocol_string(void)
{
    const pal_t *pal = get_default_pal();
    iot_client_t *c = make_client(pal);
    int rc = -1;
    reset_ctrl_cb();
    iot_ai_ctrl_set_callback(c, ctrl_callback, NULL);

    const char *envelope =
        "{\"protocol\":\"9000\",\"data\":{\"data\":{\"type\":\"asrInterrupt\",\"data\":{}}}}";
    if (iot_ai_ctrl_dispatch(c, (const uint8_t *)envelope, strlen(envelope))) {
        printf("  string protocol consumed\n"); goto out;
    }
    if (ctrl_cb_count != 0) { printf("  callback should not have fired\n"); goto out; }
    rc = 0;
out:
    destroy_client(c);
    return rc;
}

int main(void)
{
    setbuf(stdout, NULL);
    setbuf(stderr, NULL);
    printf("========== IoT AI Control Channel Test Suite ==========\n");

    const pal_t *pal = get_default_pal();
    iot_init(pal);

    RUN_TEST(test_asr_interrupt_dispatch);
    RUN_TEST(test_non_9000_passthrough);
    RUN_TEST(test_no_callback_passthrough);
    RUN_TEST(test_missing_type_not_consumed);
    RUN_TEST(test_null_safety);
    RUN_TEST(test_deregister);
    RUN_TEST(test_set_callback_null_client);
    RUN_TEST(test_protocol_float);
    RUN_TEST(test_protocol_string);

    printf("\n========== Results: %d/%d passed ==========\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
