/*
 * DP management layer tests.
 *
 * Most cases are network-free: the test target inherits the library's src/ on
 * its include path, so it drives the internal hooks (iot_dp_rebuild /
 * iot_dp_dispatch_downlink / iot_dp_deinit) plus the public iot_dp_* API
 * directly on a hand-built iot_client_t — no MQTT needed.
 *
 * Two cases use mocks (reusing the existing harness pattern):
 *   - report round-trip via message_mock.py (publish -> echo -> dispatch);
 *   - schema upgrade via atop_mock.py (tuya.device.schema.newest.get).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include "iot_client.h"
#include "iot_client_message.h"
#include "iot_config_defaults.h"
#include "iot_dp.h"
#include "iot_dp_internal.h"
#include "atop.h"

/* ---- mock endpoints (match atop_test.c / iot_client_message_test.c) ---- */
#define MSG_MOCK_PORT   11885
#define MSG_MOCK_URL    "mqtts://127.0.0.1:11885"
#define ATOP_MOCK_HOST  "127.0.0.1"
#define ATOP_MOCK_PORT  8443
#define ATOP_MOCK_URL   "https://127.0.0.1:8443"

/* device identity that matches test/config/atop.conf (sec_key / device_id) */
#define TEST_DEVID      "ci_device_test_001"
#define TEST_SECRET_KEY "1234567890abcdef"
#define TEST_LOCAL_KEY  "0123456789abcdef"

static const char *TEST_SCHEMA =
    "["
    "{\"id\":1,\"type\":\"bool\",\"mode\":\"rw\"},"
    "{\"id\":2,\"type\":\"value\",\"mode\":\"rw\",\"property\":{\"min\":0,\"max\":1000}},"
    "{\"id\":3,\"type\":\"enum\",\"mode\":\"rw\",\"property\":{\"range\":[\"white\",\"warm\",\"cold\"]}},"
    "{\"id\":4,\"type\":\"string\",\"mode\":\"rw\",\"property\":{\"maxlen\":16}},"
    "{\"id\":5,\"type\":\"raw\",\"mode\":\"wr\",\"property\":{\"maxlen\":64}}"
    "]";

static pid_t msg_mock_pid = -1;
static pid_t atop_mock_pid = -1;
static int tests_run = 0;
static int tests_passed = 0;
static char *g_cacert = NULL;

#define RUN_TEST(fn)                                       \
    do {                                                   \
        tests_run++;                                       \
        printf("\n--- [%d] %s ---\n", tests_run, #fn);     \
        if ((fn)() == 0) { tests_passed++; printf("  PASS\n"); } \
        else { printf("  FAIL\n"); }                       \
    } while (0)

/* ---- DP callback capture ---- */
static int      dp_cb_count;
static uint8_t  dp_cb_ids[16];
static iot_dp_value_t dp_cb_vals[16];
static char     dp_cb_str[16][64];

static void reset_dp_cb(void)
{
    dp_cb_count = 0;
    memset(dp_cb_ids, 0, sizeof(dp_cb_ids));
    memset(dp_cb_vals, 0, sizeof(dp_cb_vals));
    memset(dp_cb_str, 0, sizeof(dp_cb_str));
}

static void dp_callback(uint8_t dp_id, const iot_dp_value_t *value, void *user_data)
{
    (void)user_data;
    if (dp_cb_count >= 16) return;
    int i = dp_cb_count++;
    dp_cb_ids[i] = dp_id;
    dp_cb_vals[i] = *value;
    if (value->type == IOT_DP_TYPE_STRING && value->value.string) {
        strncpy(dp_cb_str[i], value->value.string, sizeof(dp_cb_str[i]) - 1);
        dp_cb_vals[i].value.string = dp_cb_str[i];
    }
}

/* ---- save callback capture ---- */
static int  save_cb_count;
static char save_cb_last[512];

static void save_callback(const char *dp_state_json, void *user_data)
{
    (void)user_data;
    save_cb_count++;
    strncpy(save_cb_last, dp_state_json ? dp_state_json : "", sizeof(save_cb_last) - 1);
    save_cb_last[sizeof(save_cb_last) - 1] = '\0';
}

/* ---- schema-update callback capture ---- */
static int  schema_cb_count;
static char schema_cb_id[64];
static char schema_cb_schema[512];

static void schema_update_callback(const char *schema_id, const char *new_schema, void *user_data)
{
    (void)user_data;
    schema_cb_count++;
    strncpy(schema_cb_id, schema_id ? schema_id : "", sizeof(schema_cb_id) - 1);
    strncpy(schema_cb_schema, new_schema ? new_schema : "", sizeof(schema_cb_schema) - 1);
}

/* ---- message callback capture (report round-trip) ---- */
static volatile int msg_cb_count;
static char msg_cb_data[2048];
static size_t msg_cb_len;

static void msg_callback(const char *topic, size_t topic_len, const uint8_t *data, size_t data_len)
{
    (void)topic; (void)topic_len;
    msg_cb_count++;
    size_t n = data_len < sizeof(msg_cb_data) - 1 ? data_len : sizeof(msg_cb_data) - 1;
    memcpy(msg_cb_data, data, n);
    msg_cb_data[n] = '\0';
    msg_cb_len = n;
}

/* ---- helpers ---- */

static char *load_file(const pal_t *pal, const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = pal->malloc(len + 1);
    if (buf) {
        if (fread(buf, 1, len, f) != (size_t)len) { pal->free(buf); fclose(f); return NULL; }
        buf[len] = '\0';
    }
    fclose(f);
    return buf;
}

static int wait_for_port(int port)
{
    for (int i = 0; i < 100; i++) {
        int fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) return -1;
        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons((uint16_t)port);
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        int ret = connect(fd, (struct sockaddr *)&addr, sizeof(addr));
        close(fd);
        if (ret == 0) return 0;
        usleep(20000);
    }
    return -1;
}

/* Build a registry-backed client WITHOUT MQTT (for the network-free cases). */
static iot_client_t *make_client(const pal_t *pal, const char *schema)
{
    iot_client_t *client = (iot_client_t *)pal->malloc(sizeof(iot_client_t));
    if (!client) return NULL;
    memset(client, 0, sizeof(iot_client_t));
    client->pal = pal;
    strncpy(client->devid, TEST_DEVID, sizeof(client->devid) - 1);
    strncpy(client->secret_key, TEST_SECRET_KEY, sizeof(client->secret_key) - 1);
    strncpy(client->local_key, TEST_LOCAL_KEY, sizeof(client->local_key) - 1);
    if (schema) client->schema = pal_strdup(pal, schema);
    iot_dp_rebuild(client);
    return client;
}

static void destroy_client(iot_client_t *client)
{
    if (!client) return;
    const pal_t *pal = client->pal;
    iot_dp_deinit(client);
    if (client->schema) pal->free(client->schema);
    pal->free(client);
}

static int dispatch_json(iot_client_t *client, const char *json)
{
    return iot_dp_dispatch_downlink(client, "smart/device/in/x", 17,
                                    (const uint8_t *)json, strlen(json));
}

/* ============================================================================
 * Network-free tests
 * ============================================================================ */

/* [1] schema build: known DPs are writable/gettable; unknown id rejected. */
static int test_schema_rebuild_and_set(void)
{
    const pal_t *pal = get_default_pal();
    iot_client_t *c = make_client(pal, TEST_SCHEMA);
    int rc = -1;

    iot_dp_value_t v = { .type = IOT_DP_TYPE_BOOL, .value.boolean = true };
    if (iot_dp_set(c, 1, &v) != OPRT_OK)            { printf("  set(1) failed\n"); goto out; }
    iot_dp_value_t got;
    if (iot_dp_get(c, 1, &got) != OPRT_OK || !got.value.boolean) { printf("  get(1) mismatch\n"); goto out; }
    if (iot_dp_get(c, 2, &got) != OPRT_INVALID_RESULT) { printf("  get(2) should be no-value\n"); goto out; }
    if (iot_dp_set(c, 99, &v) != OPRT_DP_INVALID_ID) { printf("  set(99) should be INVALID_ID\n"); goto out; }
    rc = 0;
out:
    destroy_client(c);
    return rc;
}

/* [2] downlink protocol-4 -> dp_callback + cache updated. */
static int test_downlink_dp_set(void)
{
    const pal_t *pal = get_default_pal();
    iot_client_t *c = make_client(pal, TEST_SCHEMA);
    int rc = -1;
    reset_dp_cb();
    iot_dp_set_callback(c, dp_callback, NULL);

    if (!dispatch_json(c, "{\"protocol\":5,\"t\":1,\"data\":{\"dps\":{\"1\":false,\"2\":300,\"3\":\"warm\"}}}")) {
        printf("  dispatch did not consume downlink (protocol 5)\n"); goto out;
    }
    if (dp_cb_count != 3) { printf("  expected 3 callbacks, got %d\n", dp_cb_count); goto out; }

    iot_dp_value_t got;
    if (iot_dp_get(c, 1, &got) != OPRT_OK || got.value.boolean != false) { printf("  dp1 wrong\n"); goto out; }
    if (iot_dp_get(c, 2, &got) != OPRT_OK || got.value.integer != 300)   { printf("  dp2 wrong\n"); goto out; }
    if (iot_dp_get(c, 3, &got) != OPRT_OK || got.value.enum_index != 1)  { printf("  dp3 wrong (%d)\n", got.value.enum_index); goto out; }
    rc = 0;
out:
    destroy_client(c);
    return rc;
}

/* [3] non-DP JSON is not consumed (passthrough). */
static int test_non_dp_passthrough(void)
{
    const pal_t *pal = get_default_pal();
    iot_client_t *c = make_client(pal, TEST_SCHEMA);
    int rc = -1;
    if (dispatch_json(c, "{\"type\":\"test\",\"payload\":\"hi\"}")) { printf("  no-protocol consumed\n"); goto out; }
    if (dispatch_json(c, "{\"protocol\":99,\"data\":{}}"))         { printf("  unknown protocol consumed\n"); goto out; }
    /* protocol 4 is the uplink report format; if echoed back it must NOT be consumed downlink */
    if (dispatch_json(c, "{\"protocol\":4,\"data\":{\"dps\":{\"1\":true}}}")) { printf("  uplink report consumed downlink\n"); goto out; }
    if (dispatch_json(c, "not json at all"))                        { printf("  non-json consumed\n"); goto out; }
    rc = 0;
out:
    destroy_client(c);
    return rc;
}

/* [4] loose mode (no schema) never consumes. */
static int test_loose_passthrough(void)
{
    const pal_t *pal = get_default_pal();
    iot_client_t *c = make_client(pal, NULL);   /* loose */
    int rc = -1;
    if (dispatch_json(c, "{\"protocol\":4,\"data\":{\"dps\":{\"1\":true}}}")) {
        printf("  loose mode consumed a DP envelope\n"); goto out;
    }
    rc = 0;
out:
    destroy_client(c);
    return rc;
}

/* [5] validation error codes. */
static int test_validation_errors(void)
{
    const pal_t *pal = get_default_pal();
    iot_client_t *c = make_client(pal, TEST_SCHEMA);
    int rc = -1;

    iot_dp_value_t wrong_type = { .type = IOT_DP_TYPE_STRING, .value.string = "x" };
    if (iot_dp_set(c, 1, &wrong_type) != OPRT_DP_TYPE_MISMATCH) { printf("  expected TYPE_MISMATCH\n"); goto out; }

    iot_dp_value_t oor = { .type = IOT_DP_TYPE_VALUE, .value.integer = 5000 };
    if (iot_dp_set(c, 2, &oor) != OPRT_DP_VALUE_OUT_OF_RANGE) { printf("  expected OUT_OF_RANGE\n"); goto out; }

    iot_dp_value_t toolong = { .type = IOT_DP_TYPE_STRING, .value.string = "this string is way too long" };
    if (iot_dp_set(c, 4, &toolong) != OPRT_DP_VALUE_OUT_OF_RANGE) { printf("  expected OUT_OF_RANGE (strlen)\n"); goto out; }

    iot_dp_value_t b = { .type = IOT_DP_TYPE_BOOL, .value.boolean = true };
    if (iot_dp_set(c, 99, &b) != OPRT_DP_INVALID_ID) { printf("  expected INVALID_ID\n"); goto out; }
    rc = 0;
out:
    destroy_client(c);
    return rc;
}

/* [6] rebuild is idempotent (call twice; state still usable). */
static int test_rebuild_idempotent(void)
{
    const pal_t *pal = get_default_pal();
    iot_client_t *c = make_client(pal, TEST_SCHEMA);
    int rc = -1;
    iot_dp_rebuild(c);
    iot_dp_rebuild(c);
    iot_dp_value_t v = { .type = IOT_DP_TYPE_VALUE, .value.integer = 42 };
    if (iot_dp_set(c, 2, &v) != OPRT_OK) { printf("  set after double rebuild failed\n"); goto out; }
    iot_dp_value_t got;
    if (iot_dp_get(c, 2, &got) != OPRT_OK || got.value.integer != 42) { printf("  get mismatch\n"); goto out; }
    rc = 0;
out:
    destroy_client(c);
    return rc;
}

/* [7] dump/restore round-trip; restored values are not dirty. */
static int test_dump_restore_roundtrip(void)
{
    const pal_t *pal = get_default_pal();
    iot_client_t *c1 = make_client(pal, TEST_SCHEMA);
    int rc = -1;
    char *dump = NULL;

    iot_dp_value_t b  = { .type = IOT_DP_TYPE_BOOL,   .value.boolean = true };
    iot_dp_value_t i  = { .type = IOT_DP_TYPE_VALUE,  .value.integer = 500 };
    iot_dp_value_t en = { .type = IOT_DP_TYPE_ENUM,   .value.enum_index = 2 };   /* cold */
    iot_dp_value_t s  = { .type = IOT_DP_TYPE_STRING, .value.string = "hello" };
    iot_dp_set(c1, 1, &b); iot_dp_set(c1, 2, &i); iot_dp_set(c1, 3, &en); iot_dp_set(c1, 4, &s);

    if (iot_dp_dump_json(c1, &dump) != OPRT_OK || !dump) { printf("  dump failed\n"); goto out; }
    printf("  dump: %s\n", dump);

    iot_client_t *c2 = make_client(pal, TEST_SCHEMA);
    if (iot_dp_restore_json(c2, dump) != OPRT_OK) { printf("  restore failed\n"); destroy_client(c2); goto out; }

    iot_dp_value_t g;
    int ok = 1;
    if (iot_dp_get(c2, 1, &g) != OPRT_OK || g.value.boolean != true) ok = 0;
    if (iot_dp_get(c2, 2, &g) != OPRT_OK || g.value.integer != 500)  ok = 0;
    if (iot_dp_get(c2, 3, &g) != OPRT_OK || g.value.enum_index != 2) ok = 0;
    if (iot_dp_get(c2, 4, &g) != OPRT_OK || strcmp(g.value.string, "hello") != 0) ok = 0;
    if (!ok) { printf("  restored values mismatch\n"); destroy_client(c2); goto out; }

    /* restore must not mark dirty -> report_all_dirty is a no-op OK (no MQTT needed) */
    if (iot_dp_report_all_dirty(c2) != OPRT_OK) { printf("  report_all_dirty after restore not OK\n"); destroy_client(c2); goto out; }

    destroy_client(c2);
    rc = 0;
out:
    if (dump) pal->free(dump);
    destroy_client(c1);
    return rc;
}

/* [8] save callback fires on set/downlink, not on restore. */
static int test_save_callback(void)
{
    const pal_t *pal = get_default_pal();
    iot_client_t *c = make_client(pal, TEST_SCHEMA);
    int rc = -1;
    save_cb_count = 0;
    save_cb_last[0] = '\0';
    iot_dp_set_save_callback(c, save_callback, NULL);

    iot_dp_value_t b = { .type = IOT_DP_TYPE_BOOL, .value.boolean = true };
    iot_dp_set(c, 1, &b);
    if (save_cb_count != 1) { printf("  save_cb not fired on set (%d)\n", save_cb_count); goto out; }
    if (!strstr(save_cb_last, "\"1\":true")) { printf("  snapshot missing dp1: %s\n", save_cb_last); goto out; }

    /* downlink (protocol 5) also fires save once for the batch */
    dispatch_json(c, "{\"protocol\":5,\"data\":{\"dps\":{\"2\":7,\"3\":\"warm\"}}}");
    if (save_cb_count != 2) { printf("  save_cb not fired once on downlink (%d)\n", save_cb_count); goto out; }

    /* restore must NOT fire save */
    int before = save_cb_count;
    iot_dp_restore_json(c, "{\"dps\":{\"1\":false}}");
    if (save_cb_count != before) { printf("  save_cb fired on restore\n"); goto out; }
    rc = 0;
out:
    destroy_client(c);
    return rc;
}

/* [9] first-activation defaults: every DP seeded from schema, dirty, dumpable. */
static int test_init_defaults(void)
{
    const pal_t *pal = get_default_pal();
    iot_client_t *c = make_client(pal, TEST_SCHEMA);
    int rc = -1;
    char *dump = NULL;

    iot_dp_init_defaults(c);
    if (iot_dp_dump_json(c, &dump) != OPRT_OK || !dump) { printf("  dump failed\n"); goto out; }
    printf("  defaults: %s\n", dump);
    /* 1 bool->false, 2 value(min0)->0, 3 enum->white, 4 string->"" */
    if (!strstr(dump, "\"1\":false") || !strstr(dump, "\"2\":0") ||
        !strstr(dump, "\"3\":\"white\"") || !strstr(dump, "\"4\":\"\"")) {
        printf("  defaults content mismatch\n"); goto out;
    }
    /* raw DP 5 is seeded in-cache but must NOT be persisted (raw is never persisted) */
    if (strstr(dump, "\"5\":")) { printf("  raw DP must not appear in persisted dump\n"); goto out; }
    iot_dp_value_t v;
    if (iot_dp_get(c, 5, &v) != OPRT_OK) { printf("  raw DP not seeded\n"); goto out; }
    rc = 0;
out:
    if (dump) pal->free(dump);
    destroy_client(c);
    return rc;
}

/* [9b] raw DPs are never persisted: a raw + a non-raw DP are set via downlink, the
 * dump keeps only the non-raw one. */
static int test_raw_not_persisted(void)
{
    const pal_t *pal = get_default_pal();
    iot_client_t *c = make_client(pal, TEST_SCHEMA);
    int rc = -1;
    char *dump = NULL;

    if (!dispatch_json(c, "{\"protocol\":5,\"data\":{\"dps\":{\"1\":true,\"5\":\"AQIDBA==\"}}}")) {
        printf("  downlink not consumed\n"); goto out;
    }
    iot_dp_value_t g;
    if (iot_dp_get(c, 5, &g) != OPRT_OK || g.value.raw.len != 4) { printf("  raw not cached\n"); goto out; }

    if (iot_dp_dump_json(c, &dump) != OPRT_OK || !dump) { printf("  dump failed\n"); goto out; }
    printf("  dump: %s\n", dump);
    if (!strstr(dump, "\"1\":true")) { printf("  non-raw DP missing from dump\n"); goto out; }
    if (strstr(dump, "\"5\":"))      { printf("  raw DP leaked into persisted dump\n"); goto out; }
    rc = 0;
out:
    if (dump) pal->free(dump);
    destroy_client(c);
    return rc;
}

/* [9c] real Tuya schema form: top-level type "obj", leaf type under property.type;
 * min/max/range also live under property. */
static int test_schema_obj_format(void)
{
    const pal_t *pal = get_default_pal();
    const char *obj_schema =
        "[{\"mode\":\"rw\",\"property\":{\"type\":\"bool\"},\"id\":1,\"type\":\"obj\"},"
        "{\"mode\":\"rw\",\"property\":{\"min\":0,\"max\":100,\"step\":1,\"type\":\"value\"},\"id\":3,\"type\":\"obj\"},"
        "{\"mode\":\"ro\",\"property\":{\"range\":[\"none\",\"charging\",\"charge_done\"],\"type\":\"enum\"},\"id\":4,\"type\":\"obj\"}]";
    iot_client_t *c = make_client(pal, obj_schema);
    int rc = -1;

    /* leaf type parsed from property.type -> bool DP is settable */
    iot_dp_value_t b = { .type = IOT_DP_TYPE_BOOL, .value.boolean = true };
    if (iot_dp_set(c, 1, &b) != OPRT_OK) { printf("  set bool(1) failed (obj not parsed?)\n"); goto out; }

    /* min/max parsed from property -> out-of-range rejected */
    iot_dp_value_t n = { .type = IOT_DP_TYPE_VALUE, .value.integer = 50 };
    if (iot_dp_set(c, 3, &n) != OPRT_OK) { printf("  set value(3) failed\n"); goto out; }
    iot_dp_value_t oor = { .type = IOT_DP_TYPE_VALUE, .value.integer = 200 };
    if (iot_dp_set(c, 3, &oor) != OPRT_DP_VALUE_OUT_OF_RANGE) { printf("  property min/max not parsed\n"); goto out; }

    /* enum range parsed from property.range; DP4 is ro so downlink can set it */
    if (!dispatch_json(c, "{\"protocol\":5,\"data\":{\"dps\":{\"4\":\"charging\"}}}")) { printf("  enum downlink not consumed\n"); goto out; }
    iot_dp_value_t g;
    if (iot_dp_get(c, 4, &g) != OPRT_OK || g.value.enum_index != 1) { printf("  enum range not parsed (%d)\n", g.value.enum_index); goto out; }
    rc = 0;
out:
    destroy_client(c);
    return rc;
}

/* [9d] schema "mode" does not restrict device writes: the device may set/report
 * ro, wr, and rw DPs alike, and the cloud may set any DP via downlink. */
static int test_writability_modes(void)
{
    const pal_t *pal = get_default_pal();
    const char *sch =
        "[{\"id\":10,\"type\":\"value\",\"mode\":\"ro\",\"property\":{\"min\":0,\"max\":100}},"
        "{\"id\":11,\"type\":\"value\",\"mode\":\"wr\",\"property\":{\"min\":0,\"max\":100}},"
        "{\"id\":12,\"type\":\"value\",\"mode\":\"rw\",\"property\":{\"min\":0,\"max\":100}}]";
    iot_client_t *c = make_client(pal, sch);
    int rc = -1;
    iot_dp_value_t v = { .type = IOT_DP_TYPE_VALUE, .value.integer = 5 };

    if (iot_dp_set(c, 10, &v) != OPRT_OK) { printf("  ro must be device-writable\n"); goto out; }
    if (iot_dp_set(c, 11, &v) != OPRT_OK) { printf("  wr must be device-writable (mode is not enforced)\n"); goto out; }
    if (iot_dp_set(c, 12, &v) != OPRT_OK) { printf("  rw must be writable\n"); goto out; }

    /* the cloud may also set any DP via downlink */
    if (!dispatch_json(c, "{\"protocol\":5,\"data\":{\"dps\":{\"11\":42}}}")) { printf("  downlink not consumed\n"); goto out; }
    iot_dp_value_t g;
    if (iot_dp_get(c, 11, &g) != OPRT_OK || g.value.integer != 42) { printf("  downlink not applied\n"); goto out; }
    rc = 0;
out:
    destroy_client(c);
    return rc;
}

/* [9e] validate-only: a persisted dp_state is vetted against the schema without
 * mutating the cache; the first non-conforming entry fails the whole document. */
static int test_validate_json(void)
{
    const pal_t *pal = get_default_pal();
    iot_client_t *c = make_client(pal, TEST_SCHEMA);
    int rc = -1;

    if (iot_dp_validate_json(c, "{\"dps\":{\"1\":true,\"2\":500,\"3\":\"warm\"}}") != OPRT_OK)
        { printf("  valid state rejected\n"); goto out; }
    if (iot_dp_validate_json(c, "{\"dps\":{\"99\":1}}") != OPRT_DP_INVALID_ID)
        { printf("  unknown id not caught\n"); goto out; }
    if (iot_dp_validate_json(c, "{\"dps\":{\"1\":5}}") != OPRT_DP_TYPE_MISMATCH)
        { printf("  type mismatch not caught\n"); goto out; }
    if (iot_dp_validate_json(c, "{\"dps\":{\"2\":99999}}") != OPRT_DP_VALUE_OUT_OF_RANGE)
        { printf("  out-of-range not caught\n"); goto out; }

    /* validation must not mutate the cache: DP1 still has no value */
    iot_dp_value_t g;
    if (iot_dp_get(c, 1, &g) != OPRT_INVALID_RESULT) { printf("  validate mutated the cache\n"); goto out; }
    rc = 0;
out:
    destroy_client(c);
    return rc;
}

/* ============================================================================
 * Mock-backed tests
 * ============================================================================ */

/* [10] report round-trip via message_mock (publish -> echo -> dispatch -> message_cb). */
static int test_report_roundtrip(void)
{
    const pal_t *pal = get_default_pal();
    iot_client_t *client = (iot_client_t *)pal->malloc(sizeof(iot_client_t));
    if (!client) return -1;
    memset(client, 0, sizeof(iot_client_t));
    client->pal = pal;
    strncpy(client->devid, TEST_DEVID, sizeof(client->devid) - 1);
    strncpy(client->secret_key, TEST_SECRET_KEY, sizeof(client->secret_key) - 1);
    strncpy(client->local_key, TEST_LOCAL_KEY, sizeof(client->local_key) - 1);
    snprintf(client->mqtt_url, sizeof(client->mqtt_url), "%s", MSG_MOCK_URL);
    client->cacert = g_cacert;
    client->message_callback = msg_callback;
    client->schema = pal_strdup(pal, TEST_SCHEMA);
    iot_dp_rebuild(client);

    int rc = -1;
    if (iot_client_message_connect(client) != OPRT_OK) { printf("  connect failed\n"); goto out; }

    /* drain the mock's initial raw message */
    msg_cb_count = 0;
    for (int i = 0; i < 20 && msg_cb_count == 0; i++) iot_client_message_process(client, 50);
    msg_cb_count = 0;
    msg_cb_data[0] = '\0';

    iot_dp_value_t v = { .type = IOT_DP_TYPE_BOOL, .value.boolean = true };
    if (iot_dp_report(client, 1, &v) != OPRT_OK) { printf("  iot_dp_report failed\n"); goto out; }

    for (int i = 0; i < 40 && msg_cb_count == 0; i++) iot_client_message_process(client, 50);
    if (msg_cb_count == 0) { printf("  no echo received\n"); goto out; }
    printf("  echoed report: %s\n", msg_cb_data);
    if (!strstr(msg_cb_data, "\"protocol\":4") || !strstr(msg_cb_data, "\"1\":true")) {
        printf("  report payload mismatch\n"); goto out;
    }
    rc = 0;
out:
    iot_client_message_disconnect(client);
    iot_dp_deinit(client);
    if (client->schema) pal->free(client->schema);
    client->mqtt_url[0] = '\0';
    client->cacert = NULL;
    pal->free(client);
    return rc;
}

/* [11a] schema upgrade via atop_mock: new schema -> rebuild + callback. */
static int test_schema_check_update(void)
{
    const pal_t *pal = get_default_pal();
    iot_client_t *client = (iot_client_t *)pal->malloc(sizeof(iot_client_t));
    if (!client) return -1;
    memset(client, 0, sizeof(iot_client_t));
    client->pal = pal;
    strncpy(client->devid, TEST_DEVID, sizeof(client->devid) - 1);
    strncpy(client->secret_key, TEST_SECRET_KEY, sizeof(client->secret_key) - 1);
    strncpy(client->schema_id, "dp_test_schema", sizeof(client->schema_id) - 1);
    snprintf(client->https_url, sizeof(client->https_url), "%s", ATOP_MOCK_URL);
    client->cacert = g_cacert;
    /* Start with an OLD schema carrying live values, to prove the upgrade keeps them. */
    client->schema = pal_strdup(pal,
        "[{\"id\":1,\"type\":\"bool\",\"mode\":\"rw\"},"
        "{\"id\":2,\"type\":\"value\",\"mode\":\"rw\",\"property\":{\"min\":0,\"max\":1000}}]");
    iot_dp_rebuild(client);

    int rc = -1;
    schema_cb_count = 0;
    iot_dp_set_schema_update_callback(client, schema_update_callback, NULL);

    /* live values before the upgrade */
    iot_dp_value_t on = { .type = IOT_DP_TYPE_BOOL,  .value.boolean = true };
    iot_dp_value_t n  = { .type = IOT_DP_TYPE_VALUE, .value.integer = 42 };
    iot_dp_set(client, 1, &on);
    iot_dp_set(client, 2, &n);

    /* mock's "update" schema = DP 1,2,3,7 (adds enum 3 and string 7) */
    if (iot_dp_schema_check_update(client) != OPRT_OK) { printf("  schema_check_update failed\n"); goto out; }
    if (schema_cb_count != 1) { printf("  schema_update_callback not fired (%d)\n", schema_cb_count); goto out; }
    if (!client->schema || !strstr(client->schema, "\"id\"")) { printf("  client->schema not updated\n"); goto out; }

    iot_dp_value_t g;
    /* existing DP values preserved across the upgrade */
    if (iot_dp_get(client, 1, &g) != OPRT_OK || g.value.boolean != true) { printf("  DP1 not preserved\n"); goto out; }
    if (iot_dp_get(client, 2, &g) != OPRT_OK || g.value.integer != 42)   { printf("  DP2 not preserved\n"); goto out; }
    /* newly-added DPs were defaulted (have a value) */
    if (iot_dp_get(client, 3, &g) != OPRT_OK) { printf("  new DP3 not defaulted\n"); goto out; }
    if (iot_dp_get(client, 7, &g) != OPRT_OK) { printf("  new DP7 not defaulted\n"); goto out; }
    rc = 0;
out:
    iot_dp_deinit(client);
    if (client->schema) pal->free(client->schema);
    client->cacert = NULL;
    pal->free(client);
    return rc;
}

/* [11b] no-update path: atop_schema_newest_get returns updated=false on []. */
static int test_schema_newest_no_update(void)
{
    const pal_t *pal = get_default_pal();
    schema_newest_request_t req = {
        .devid = TEST_DEVID,
        .key = TEST_SECRET_KEY,
        .schema_id = "dp_test_schema",
        .version = "NOUPDATE",
        .node_id = NULL,
        .host = ATOP_MOCK_HOST,
        .port = ATOP_MOCK_PORT,
        .cacert = g_cacert,
    };
    schema_newest_response_t resp = {0};
    int rc = -1;
    int rt = atop_schema_newest_get(pal, &req, &resp);
    if (rt != OPRT_OK) { printf("  atop_schema_newest_get failed: %d\n", rt); goto out; }
    if (resp.updated || resp.schema) { printf("  expected no update\n"); goto out; }
    rc = 0;
out:
    atop_schema_newest_response_free(pal, &resp);
    return rc;
}

/* ---- mock lifecycle ---- */

static int start_msg_mock(void)
{
    msg_mock_pid = fork();
    if (msg_mock_pid == 0) {
        execlp(PYTHON3_EXEC, PYTHON3_EXEC, MESSAGE_MOCK_PATH, NULL);
        perror("execlp message mock");
        _exit(1);
    }
    if (msg_mock_pid < 0) return -1;
    return wait_for_port(MSG_MOCK_PORT);
}

static int start_atop_mock(void)
{
    atop_mock_pid = fork();
    if (atop_mock_pid == 0) {
        setenv("ATOP_MOCK_USE_SSL", "1", 1);
        execlp(PYTHON3_EXEC, PYTHON3_EXEC, ATOP_MOCK_PATH, NULL);
        perror("execlp atop mock");
        _exit(1);
    }
    if (atop_mock_pid < 0) return -1;
    return wait_for_port(ATOP_MOCK_PORT);
}

static void stop_mocks(void)
{
    if (msg_mock_pid > 0) { kill(msg_mock_pid, SIGTERM); waitpid(msg_mock_pid, NULL, 0); msg_mock_pid = -1; }
    if (atop_mock_pid > 0) { kill(atop_mock_pid, SIGTERM); waitpid(atop_mock_pid, NULL, 0); atop_mock_pid = -1; }
}

int main(void)
{
    setbuf(stdout, NULL);
    setbuf(stderr, NULL);
    printf("========== IoT DP Test Suite ==========\n");

    const pal_t *pal = get_default_pal();
    iot_init(pal);

    g_cacert = load_file(pal, TEST_CONFIG_DIR "/root_cert.pem");
    if (!g_cacert) fprintf(stderr, "Warning: CA cert not loaded; mock tests may fail\n");

    if (start_msg_mock() != 0)  { fprintf(stderr, "Failed to start message mock\n"); return 1; }
    if (start_atop_mock() != 0) { fprintf(stderr, "Failed to start atop mock\n"); stop_mocks(); return 1; }

    /* network-free */
    RUN_TEST(test_schema_rebuild_and_set);
    RUN_TEST(test_downlink_dp_set);
    RUN_TEST(test_non_dp_passthrough);
    RUN_TEST(test_loose_passthrough);
    RUN_TEST(test_validation_errors);
    RUN_TEST(test_rebuild_idempotent);
    RUN_TEST(test_dump_restore_roundtrip);
    RUN_TEST(test_save_callback);
    RUN_TEST(test_init_defaults);
    RUN_TEST(test_raw_not_persisted);
    RUN_TEST(test_schema_obj_format);
    RUN_TEST(test_writability_modes);
    RUN_TEST(test_validate_json);

    /* mock-backed */
    RUN_TEST(test_report_roundtrip);
    RUN_TEST(test_schema_check_update);
    RUN_TEST(test_schema_newest_no_update);

    stop_mocks();
    pal->free(g_cacert);

    printf("\n========== Results: %d/%d passed ==========\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
