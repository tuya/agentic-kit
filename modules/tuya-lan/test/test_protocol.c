/* Independent encrypted peer: wire construction does not use SDK encoders. */
#include "tuya_lan_protocol.h"
#include "rng.h"
#include "cJSON.h"
#include "mbedtls/gcm.h"
#include "mbedtls/md.h"
#include "mbedtls/md5.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __func__, __LINE__, #x); return 1; } } while (0)
#define RUN(fn) do { ++total; if (fn()) ++failed; else ++passed; } while (0)
#define WIRE_MAX TUYA_LAN_MAX_FRAME_SIZE

static uint64_t now_ms;
static unsigned random_counter;
static int random_failure;
static int allocation_failure;
static unsigned allocations;
static const uint8_t local_key[17] = "0123456789abcdef";
static unsigned passed, failed, total;

/* The test replaces the shared DRBG to make entropy failures and binary keys
 * reproducible. Encryption/HMAC remain the real independently invoked mbedTLS. */
int rng_bytes(const pal_t *pal, uint8_t *data, size_t len)
{
    size_t i;
    (void)pal;
    if (random_failure) return -1;
    for (i = 0; i < len; ++i) data[i] = (uint8_t)random_counter++;
    return 0;
}
static void *test_malloc(size_t n)
{
    void *p;
    if (allocation_failure) return NULL;
    p = malloc(n);
    if (p) ++allocations;
    return p;
}
static void test_free(void *p) { if (p) { --allocations; free(p); } }
static uint64_t test_time(void) { return now_ms; }
static uint64_t test_wall(void) { return 1700000000123ULL; }
static const pal_t pal = {.malloc = test_malloc, .free = test_free,
                         .time_ms = test_time, .wal_clock_time = test_wall};
static uint32_t be32(const uint8_t *p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}
static void store32(uint8_t *p, uint32_t n)
{
    p[0] = (uint8_t)(n >> 24); p[1] = (uint8_t)(n >> 16);
    p[2] = (uint8_t)(n >> 8); p[3] = (uint8_t)n;
}

static size_t peer_encode(const uint8_t key[16], uint32_t sequence, uint32_t command,
                          const uint8_t *plaintext, size_t len, uint8_t *frame)
{
    mbedtls_gcm_context gcm;
    int status;
    size_t i;
    memset(frame, 0, 18);
    store32(frame, 0x6699);
    store32(frame + 6, sequence);
    store32(frame + 10, command);
    store32(frame + 14, (uint32_t)(len + 28));
    for (i = 0; i < 12; ++i) frame[18 + i] = (uint8_t)(i + sequence * 13u);
    mbedtls_gcm_init(&gcm);
    status = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, key, 128);
    if (!status) status = mbedtls_gcm_crypt_and_tag(&gcm, MBEDTLS_GCM_ENCRYPT,
                         len, frame + 18, 12, frame + 4, 14, plaintext,
                         frame + 30, 16, frame + 30 + len);
    mbedtls_gcm_free(&gcm);
    if (status) return 0;
    store32(frame + 46 + len, 0x9966);
    return len + 50;
}

static int peer_decode(const uint8_t key[16], const uint8_t *frame, size_t len,
                       uint8_t *plaintext, size_t *plain_len)
{
    mbedtls_gcm_context gcm;
    int status;
    if (len < 50 || be32(frame) != 0x6699 || be32(frame + len - 4) != 0x9966 ||
        be32(frame + 14) != len - 22) return -1;
    *plain_len = len - 50;
    mbedtls_gcm_init(&gcm);
    status = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, key, 128);
    if (!status) status = mbedtls_gcm_auth_decrypt(&gcm, *plain_len, frame + 18, 12,
                         frame + 4, 14, frame + len - 20, 16, frame + 30, plaintext);
    mbedtls_gcm_free(&gcm);
    return status;
}

static int peer_hmac(const uint8_t random[16], uint8_t out[32])
{
    return mbedtls_md_hmac(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256),
                           local_key, 16, random, 16, out);
}
static int peer_session_key(const uint8_t a[16], const uint8_t b[16], uint8_t out[16])
{
    mbedtls_gcm_context gcm;
    uint8_t mixed[16], tag[16];
    size_t i;
    int status;
    for (i = 0; i < 16; ++i) mixed[i] = a[i] ^ b[i];
    mbedtls_gcm_init(&gcm);
    status = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, local_key, 128);
    if (!status) status = mbedtls_gcm_crypt_and_tag(&gcm, MBEDTLS_GCM_ENCRYPT,
                         16, a, 12, NULL, 0, mixed, out, 16, tag);
    mbedtls_gcm_free(&gcm);
    return status;
}

struct fixture {
    tuya_lan_config_t config;
    lan_protocol_t *protocol;
    uint8_t frame[WIRE_MAX], plain[WIRE_MAX], key[16], a[16], b[16];
    size_t frame_len;
    unsigned emissions, dp_calls, query_calls, command_calls;
    int emit_error, dp_result, query_mode;
    uint32_t command, id;
    char dps[128];
    uint8_t opaque[128];
    size_t opaque_len;
};
static int capture(void *user, const uint8_t *data, size_t len)
{
    struct fixture *f = user;
    if (f->emit_error) return f->emit_error;
    if (len > sizeof(f->frame)) return LAN_ERR_LIMIT;
    memcpy(f->frame, data, len); f->frame_len = len; ++f->emissions;
    return 0;
}
static int dp(const char *json, size_t len, void *user)
{
    struct fixture *f = user;
    ++f->dp_calls;
    if (len >= sizeof(f->dps)) return -1;
    memcpy(f->dps, json, len); f->dps[len] = 0;
    return f->dp_result;
}
static int query(char *json, size_t capacity, size_t *len, void *user)
{
    const char *state = "{\"dps\":{\"1\":true,\"2\":42}}";
    struct fixture *f = user;
    ++f->query_calls;
    if (f->query_mode == 1) { *len = capacity + 1; return 0; }
    if (f->query_mode == 2) state = "{\"dps\":false}";
    *len = strlen(state);
    if (*len > capacity) return -1;
    memcpy(json, state, *len);
    return 0;
}
static int command(uint32_t id, uint32_t cmd, const uint8_t *data, size_t len, void *user)
{
    struct fixture *f = user;
    if (len > sizeof(f->opaque)) return -1;
    ++f->command_calls; f->command = cmd; f->id = id;
    memcpy(f->opaque, data, len); f->opaque_len = len;
    return 0;
}
static int init(struct fixture *f)
{
    memset(f, 0, sizeof(*f));
    random_counter = 0; random_failure = allocation_failure = 0; now_ms = 0;
    f->config.pal = &pal;
    f->config.device_id = "device123"; f->config.uuid = "uuid123";
    f->config.product_key = "pk123"; f->config.local_key = (const char *)local_key;
    f->config.ipv4 = "192.168.1.9";
    f->config.handshake_timeout_ms = 10000; f->config.heartbeat_timeout_ms = 30000;
    f->config.on_dp = dp; f->config.on_query = query; f->config.on_command = command;
    f->config.user = f;
    memset(f->a, 0x37, sizeof(f->a));
    return lan_protocol_create(&f->config, 123, capture, f, &f->protocol);
}
static void cleanup(struct fixture *f)
{
    allocation_failure = random_failure = 0;
    lan_protocol_destroy(f->protocol); f->protocol = NULL;
}
static int exchange(struct fixture *f, uint32_t sequence, uint32_t cmd,
                    const uint8_t *data, size_t len)
{
    uint8_t encoded[WIRE_MAX];
    size_t n = peer_encode(f->key, sequence, cmd, data, len, encoded);
    return n ? lan_protocol_feed(f->protocol, encoded, n) : -100;
}
static int authenticate(struct fixture *f, int fragmented)
{
    uint8_t encoded[128], expected[32];
    size_t n, plain_len, i;
    int status;
    n = peer_encode(local_key, 1, 3, f->a, 16, encoded);
    if (fragmented) {
        for (i = 0; i < n; ++i) if (lan_protocol_feed(f->protocol, encoded + i, 1)) return -1;
    } else if (lan_protocol_feed(f->protocol, encoded, n)) return -2;
    if (f->emissions != 1 || be32(f->frame + 10) != 4) return -3;
    if (peer_decode(local_key, f->frame, f->frame_len, f->plain, &plain_len) ||
        plain_len != 52 || be32(f->plain) != 0) return -4;
    memcpy(f->b, f->plain + 4, 16);
    if (peer_hmac(f->a, expected) || memcmp(expected, f->plain + 20, 32)) return -5;
    if (peer_hmac(f->b, expected) || peer_session_key(f->a, f->b, f->key)) return -6;
    n = peer_encode(local_key, 2, 5, expected, 32, encoded);
    status = lan_protocol_feed(f->protocol, encoded, n);
    return status ? status : (lan_protocol_authenticated(f->protocol) ? 0 : -7);
}
static int ack(struct fixture *f, uint32_t cmd, uint32_t code)
{
    size_t n;
    CHECK(be32(f->frame + 10) == cmd);
    CHECK(peer_decode(f->key, f->frame, f->frame_len, f->plain, &n) == 0);
    CHECK(n >= 4 && be32(f->plain) == code);
    return 0;
}
static size_t inner(uint8_t *data, const char *json)
{
    memset(data, 0, 15); memcpy(data, "3.5", 3);
    store32(data + 11, 2); memcpy(data + 15, json, strlen(json));
    return 15 + strlen(json);
}

static int test_authenticated_control_query_report(void)
{
    struct fixture f;
    uint8_t data[512];
    size_t n, length;
    const char *state = "{\"dps\":{\"1\":false}}";
    cJSON *root;
    CHECK(init(&f) == 0);
    CHECK(!lan_protocol_authenticated(f.protocol));
    CHECK(authenticate(&f, 1) == 0);
    CHECK(exchange(&f, 3, 9, NULL, 0) == 0); CHECK(ack(&f, 9, 0) == 0);
    n = inner(data, "{\"protocol\":5,\"data\":{\"dps\":{\"1\":true,\"2\":42}}}");
    CHECK(exchange(&f, 4, 7, data, n) == 0); CHECK(ack(&f, 7, 0) == 0);
    CHECK(f.dp_calls == 1 && strcmp(f.dps, "{\"1\":true,\"2\":42}") == 0);
    f.dp_result = -9;
    CHECK(exchange(&f, 5, 13, data, n) == 0); CHECK(ack(&f, 13, 1) == 0);
    CHECK(exchange(&f, 6, 10, (const uint8_t *)"{}", 2) == 0);
    CHECK(ack(&f, 10, 0) == 0 && f.query_calls == 1);
    CHECK(peer_decode(f.key, f.frame, f.frame_len, f.plain, &length) == 0);
    f.plain[length] = 0;
    CHECK(strcmp((const char *)f.plain + 4, "{\"dps\":{\"1\":true,\"2\":42}}") == 0);
    n = inner(data, "{}");
    CHECK(exchange(&f, 7, 16, data, n) == 0); CHECK(ack(&f, 16, 0) == 0);
    CHECK(lan_protocol_report(f.protocol, state, strlen(state)) == 0);
    CHECK(ack(&f, 8, 0) == 0);
    CHECK(peer_decode(f.key, f.frame, f.frame_len, f.plain, &length) == 0);
    CHECK(length > 19 && memcmp(f.plain + 4, "3.5", 3) == 0 && be32(f.plain + 15) == 1);
    f.plain[length] = 0;
    root = cJSON_Parse((const char *)f.plain + 19);
    CHECK(root && cJSON_GetObjectItemCaseSensitive(root, "protocol")->valueint == 4);
    CHECK(cJSON_GetObjectItemCaseSensitive(root, "t")->valuedouble == 1700000000.0);
    CHECK(cJSON_IsObject(cJSON_GetObjectItemCaseSensitive(root, "data")));
    cJSON_Delete(root);
    CHECK(exchange(&f, 8, 0x20, (const uint8_t *)"\0\1\2\3", 4) == 0);
    CHECK(f.command_calls == 1 && f.command == 0x20 && f.id == 123 && f.opaque_len == 4);
    CHECK(memcmp(f.opaque, "\0\1\2\3", 4) == 0);
    CHECK(lan_protocol_send(f.protocol, 0x20, 0, (const uint8_t *)"reply", 5) == 0);
    CHECK(ack(&f, 0x20, 0) == 0);
    cleanup(&f); CHECK(allocations == 0); return 0;
}

static int test_coalesced_frames(void)
{
    struct fixture f;
    uint8_t frames[256];
    size_t n, next;
    CHECK(init(&f) == 0 && authenticate(&f, 0) == 0);
    n = peer_encode(f.key, 3, 9, NULL, 0, frames);
    next = peer_encode(f.key, 4, 10, (const uint8_t *)"{}", 2, frames + n);
    CHECK(lan_protocol_feed(f.protocol, frames, n + next) == 0);
    CHECK(f.emissions == 3 && f.query_calls == 1 && ack(&f, 10, 0) == 0);
    cleanup(&f); CHECK(allocations == 0); return 0;
}

static int test_replay_and_bad_tag(void)
{
    struct fixture f;
    uint8_t frame[256];
    size_t n;
    CHECK(init(&f) == 0 && authenticate(&f, 0) == 0);
    n = peer_encode(f.key, 3, 9, NULL, 0, frame);
    CHECK(lan_protocol_feed(f.protocol, frame, n) == 0);
    CHECK(lan_protocol_feed(f.protocol, frame, n) == LAN_ERR_AUTH);
    CHECK(!lan_protocol_authenticated(f.protocol));
    CHECK(exchange(&f, 4, 9, NULL, 0) == LAN_ERR_AUTH);
    cleanup(&f); CHECK(allocations == 0);
    CHECK(init(&f) == 0 && authenticate(&f, 0) == 0);
    n = peer_encode(f.key, 3, 9, NULL, 0, frame);
    frame[n - 5] ^= 1;
    CHECK(lan_protocol_feed(f.protocol, frame, n) == LAN_ERR_AUTH);
    CHECK(f.emissions == 1 && !lan_protocol_authenticated(f.protocol));
    cleanup(&f); CHECK(allocations == 0); return 0;
}

static int test_unauthenticated_and_out_of_order_handshake(void)
{
    struct fixture f;
    uint8_t frame[256], mac[32] = {0};
    size_t n;
    CHECK(init(&f) == 0);
    CHECK(lan_protocol_report(f.protocol, "{\"dps\":{}}", 10) == LAN_ERR_STATE);
    n = peer_encode(local_key, 1, 7, (const uint8_t *)"{}", 2, frame);
    CHECK(lan_protocol_feed(f.protocol, frame, n) == LAN_ERR_AUTH);
    CHECK(f.dp_calls == 0 && f.emissions == 0);
    cleanup(&f); CHECK(allocations == 0);
    CHECK(init(&f) == 0);
    n = peer_encode(local_key, 1, 5, mac, 32, frame);
    CHECK(lan_protocol_feed(f.protocol, frame, n) == LAN_ERR_AUTH);
    cleanup(&f); CHECK(allocations == 0);
    CHECK(init(&f) == 0);
    n = peer_encode(local_key, 1, 3, f.a, 16, frame);
    CHECK(lan_protocol_feed(f.protocol, frame, n) == 0);
    n = peer_encode(local_key, 2, 3, f.a, 16, frame);
    CHECK(lan_protocol_feed(f.protocol, frame, n) == LAN_ERR_AUTH);
    cleanup(&f); CHECK(allocations == 0);
    CHECK(init(&f) == 0);
    n = peer_encode(local_key, 1, 3, f.a, 16, frame);
    CHECK(lan_protocol_feed(f.protocol, frame, n) == 0);
    n = peer_encode(local_key, 2, 5, mac, 32, frame);
    CHECK(lan_protocol_feed(f.protocol, frame, n) == LAN_ERR_AUTH);
    cleanup(&f); CHECK(allocations == 0); return 0;
}

static int test_zero_leading_session_key(void)
{
    struct fixture f;
    unsigned trial;
    size_t i;
    CHECK(init(&f) == 0);
    for (i = 0; i < 16; ++i) f.b[i] = (uint8_t)(i + 2);
    for (trial = 0; trial < 65536; ++trial) {
        f.a[0] = (uint8_t)(trial >> 8); f.a[1] = (uint8_t)trial;
        CHECK(peer_session_key(f.a, f.b, f.key) == 0);
        if (!f.key[0]) break;
    }
    CHECK(trial < 65536);
    CHECK(authenticate(&f, 0) == 0 && f.key[0] == 0);
    CHECK(exchange(&f, 3, 9, NULL, 0) == 0 && ack(&f, 9, 0) == 0);
    CHECK(lan_protocol_report(f.protocol, "{\"dps\":{}}", 10) == 0);
    CHECK(ack(&f, 8, 0) == 0);
    cleanup(&f); CHECK(allocations == 0); return 0;
}

static int test_bad_lengths_headers_and_partial_timeout(void)
{
    struct fixture f;
    uint8_t frame[256];
    size_t n;
    CHECK(init(&f) == 0);
    n = peer_encode(local_key, 1, 3, f.a, 16, frame);
    CHECK(lan_protocol_feed(f.protocol, frame, 17) == 0);
    now_ms = 9999; CHECK(lan_protocol_tick(f.protocol) == 0);
    now_ms = 10000; CHECK(lan_protocol_tick(f.protocol) == LAN_ERR_TIMEOUT);
    cleanup(&f); CHECK(allocations == 0);
    CHECK(init(&f) == 0);
    store32(frame + 14, UINT32_MAX);
    CHECK(lan_protocol_feed(f.protocol, frame, 18) == LAN_ERR_PROTOCOL);
    cleanup(&f); CHECK(allocations == 0);
    CHECK(init(&f) == 0);
    n = peer_encode(local_key, 1, 3, f.a, 16, frame);
    frame[n - 1] ^= 1;
    CHECK(lan_protocol_feed(f.protocol, frame, n) == LAN_ERR_PROTOCOL);
    cleanup(&f); CHECK(allocations == 0);
    CHECK(init(&f) == 0);
    n = peer_encode(local_key, 1, 3, f.a, 16, frame);
    frame[4] = 1;
    CHECK(lan_protocol_feed(f.protocol, frame, n) == LAN_ERR_PROTOCOL);
    cleanup(&f); CHECK(allocations == 0);
    CHECK(init(&f) == 0 && authenticate(&f, 0) == 0);
    now_ms = 29999; CHECK(lan_protocol_tick(f.protocol) == 0);
    CHECK(exchange(&f, 3, 9, NULL, 0) == 0);
    now_ms += 30000; CHECK(lan_protocol_tick(f.protocol) == LAN_ERR_TIMEOUT);
    cleanup(&f); CHECK(allocations == 0); return 0;
}

static int test_strict_dp_json_and_query_bounds(void)
{
    struct fixture f;
    uint8_t payload[512];
    size_t n;
    const char *bad[] = {
        "{\"data\":{\"dps\":true}}", "{\"data\":{\"dps\":{}}}junk",
        "{\"data\":{\"dps\":{}} ,\"data\":{\"dps\":{\"1\":true}}}",
        "{\"data\":{\"dps\":{},\"dps\":{\"1\":true}}}",
        "{\"data\":{\"dps\":{\"1\":\"unterminated}}}"
    };
    unsigned i;
    CHECK(init(&f) == 0 && authenticate(&f, 0) == 0);
    for (i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        n = inner(payload, bad[i]);
        CHECK(exchange(&f, 3 + i, 7, payload, n) == 0);
        CHECK(ack(&f, 7, 1) == 0 && f.dp_calls == 0);
    }
    n = inner(payload, "{\"data\":{\"dps\":{}}}");
    payload[n++] = 0;
    payload[n++] = 'x';
    CHECK(exchange(&f, 20, 7, payload, n) == 0 && ack(&f, 7, 1) == 0);
    f.query_mode = 1;
    CHECK(exchange(&f, 21, 10, (const uint8_t *)"{}", 2) == 0 && ack(&f, 10, 1) == 0);
    f.query_mode = 2;
    CHECK(exchange(&f, 22, 16, (const uint8_t *)"{}", 2) == 0 && ack(&f, 16, 1) == 0);
    CHECK(lan_protocol_report(f.protocol, "{\"dps\":false}", 13) == LAN_ERR_PARAM);
    CHECK(lan_protocol_report(f.protocol, "{\"dps\":{},\"dps\":{}}", 19) == LAN_ERR_PARAM);
    cleanup(&f); CHECK(allocations == 0); return 0;
}

static int test_authenticated_header_tamper_and_rekey(void)
{
    struct fixture f;
    uint8_t frame[256];
    size_t n;
    CHECK(init(&f) == 0 && authenticate(&f, 0) == 0);
    n = peer_encode(f.key, 3, 9, NULL, 0, frame);
    frame[9] ^= 1;
    CHECK(lan_protocol_feed(f.protocol, frame, n) == LAN_ERR_AUTH);
    cleanup(&f); CHECK(allocations == 0);
    CHECK(init(&f) == 0 && authenticate(&f, 0) == 0);
    n = peer_encode(local_key, 3, 3, f.a, 16, frame);
    CHECK(lan_protocol_feed(f.protocol, frame, n) == LAN_ERR_AUTH);
    cleanup(&f); CHECK(allocations == 0); return 0;
}

static int test_control_lexical_validation_before_normalization(void)
{
    struct fixture f;
    uint8_t payload[512];
    size_t n;
    unsigned i;
    cJSON *root, *value;
    const char *bad[] = {
        "{\"data\":{\"dps\":{\"1\":\"abc\\u0000def\"}}}",
        "{\"data\":{\"dps\":{\"1\\u0000suffix\":true}}}",
        "{\"data\":{\"dps\":{\"1\":01}}}",
        "{\"data\":{\"dps\":{\"1\":-01}}}",
        "{\"data\":{\"dps\":{\"1\":+1}}}",
        "{\"data\":{\"dps\":{\"1\":1.}}}",
        "{\"data\":{\"dps\":{\"1\":.1}}}",
        "{\"data\":{\"dps\":{\"1\":1e}}}",
        "{\"data\":{\"dps\":{\"1\":1e+}}}",
        "{\"data\":{\"dps\":{\"1\":1e-}}}",
        "{\"data\":{\"dps\":{\"1\":1e999}}}",
        "{\"data\":{\"dps\":{\"1\":--1}}}",
        "{\"data\":{\"dps\":{\"1\":truefalse}}}",
        "{\"data\":{\"dps\":{\"1\":\vtrue}}}",
        "{\"data\":{\"dps\":{\"1\":true\f}}}",
        "{\"data\":{\"dps\":{\"1\":\"\xc0\x80\"}}}",
        "{\"data\":{\"dps\":{\"1\":\"\xe0\x9f\xbf\"}}}",
        "{\"data\":{\"dps\":{\"1\":\"\xf0\x8f\xbf\xbf\"}}}",
        "{\"data\":{\"dps\":{\"1\":\"\xed\xa0\x80\"}}}",
        "{\"data\":{\"dps\":{\"1\":\"\xf4\x90\x80\x80\"}}}",
        "{\"data\":{\"dps\":{\"1\":\"\xe1\x80x\"}}}",
        "{\"data\":{\"dps\":{\"1\":\"\\uD800\"}}}",
        "{\"data\":{\"dps\":{\"1\":\"\\uDC00\"}}}",
        "{\"data\":{\"dps\":{\"1\":\"\\uD800\\u0041\"}}}",
        "{\"data\":{\"dps\":{\"1\":\"\\u00x0\"}}}"
    };
    const char literal[] = "{\"data\":{\"dps\":{\"1\":\"abc\\\\u0000def\"}}}";
    const char valid[] = "{\"data\":{\"dps\":{\"1\":-0.125e+2,\"2\":\"\\uD83D\\uDE00\","
                         "\"3\":\"\xc2\xa2\xe4\xb8\xad\xf0\x9f\x98\x80\"}}}";
    CHECK(init(&f) == 0 && authenticate(&f, 0) == 0);
    for (i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        n = inner(payload, bad[i]);
        CHECK(exchange(&f, 3 + i, 7, payload, n) == 0);
        CHECK(ack(&f, 7, 1) == 0 && f.dp_calls == 0);
    }
    n = inner(payload, literal);
    CHECK(exchange(&f, 100, 7, payload, n) == 0 && ack(&f, 7, 0) == 0);
    CHECK(f.dp_calls == 1);
    root = cJSON_Parse(f.dps);
    value = cJSON_GetObjectItemCaseSensitive(root, "1");
    CHECK(cJSON_IsString(value) && strcmp(value->valuestring, "abc\\u0000def") == 0);
    cJSON_Delete(root);
    n = inner(payload, valid);
    CHECK(exchange(&f, 101, 13, payload, n) == 0 && ack(&f, 13, 0) == 0);
    CHECK(f.dp_calls == 2);
    root = cJSON_Parse(f.dps);
    value = cJSON_GetObjectItemCaseSensitive(root, "2");
    CHECK(cJSON_IsString(value) && strcmp(value->valuestring, "\xf0\x9f\x98\x80") == 0);
    value = cJSON_GetObjectItemCaseSensitive(root, "3");
    CHECK(cJSON_IsString(value) && strcmp(value->valuestring, "\xc2\xa2\xe4\xb8\xad\xf0\x9f\x98\x80") == 0);
    cJSON_Delete(root);
    cleanup(&f); CHECK(allocations == 0); return 0;
}

static int test_json_depth_and_late_packet(void)
{
    struct fixture f;
    char deep[256];
    size_t n, i;
    const char escaped[] = "{\"dps\":{\"1\":\"braces {}[] and \\\"quote\\\"\"}}";
    CHECK(init(&f) == 0 && authenticate(&f, 0) == 0);
    CHECK(lan_protocol_validate_state(&f.config, escaped, strlen(escaped)) == 0);
    memcpy(deep, "{\"dps\":{\"1\":", 12); n = 12;
    for (i = 0; i < 40; ++i) deep[n++] = '[';
    deep[n++] = '0';
    for (i = 0; i < 40; ++i) deep[n++] = ']';
    deep[n++] = '}'; deep[n++] = '}'; deep[n] = 0;
    CHECK(lan_protocol_validate_state(&f.config, deep, n) == LAN_ERR_PARAM);
    now_ms = 30000;
    CHECK(exchange(&f, 3, 9, NULL, 0) == LAN_ERR_TIMEOUT);
    CHECK(f.emissions == 1 && !lan_protocol_authenticated(f.protocol));
    cleanup(&f); CHECK(allocations == 0); return 0;
}

static int test_entropy_allocation_and_emit_failure(void)
{
    struct fixture f;
    uint8_t frame[256];
    size_t n;
    CHECK(init(&f) == 0); cleanup(&f);
    random_failure = 1;
    CHECK(lan_protocol_create(&f.config, 1, capture, &f, &f.protocol) == LAN_ERR_CRYPTO);
    CHECK(f.protocol == NULL); random_failure = 0;
    CHECK(init(&f) == 0);
    n = peer_encode(local_key, 1, 3, f.a, 16, frame);
    random_failure = 1;
    CHECK(lan_protocol_feed(f.protocol, frame, n) == LAN_ERR_CRYPTO);
    CHECK(f.emissions == 0); cleanup(&f); CHECK(allocations == 0);
    CHECK(init(&f) == 0 && authenticate(&f, 0) == 0);
    allocation_failure = 1;
    CHECK(exchange(&f, 3, 9, NULL, 0) == LAN_ERR_MEMORY);
    cleanup(&f); CHECK(allocations == 0);
    CHECK(init(&f) == 0 && authenticate(&f, 0) == 0);
    random_failure = 1;
    CHECK(lan_protocol_send(f.protocol, 0x20, 0, NULL, 0) == LAN_ERR_CRYPTO);
    cleanup(&f); CHECK(allocations == 0);
    CHECK(init(&f) == 0 && authenticate(&f, 0) == 0);
    f.emit_error = LAN_ERR_LIMIT;
    CHECK(exchange(&f, 3, 9, NULL, 0) == LAN_ERR_LIMIT);
    CHECK(!lan_protocol_authenticated(f.protocol));
    cleanup(&f); CHECK(allocations == 0); return 0;
}

static int test_discovery_independent_peer(void)
{
    struct fixture f;
    uint8_t encoded[WIRE_MAX], plaintext[WIRE_MAX], key[16];
    const char request[] = "{\"ip\":\"192.168.1.2\",\"from\":\"app\"}";
    size_t len, plain_len;
    char ip[16];
    cJSON *root;
    CHECK(init(&f) == 0);
    CHECK(mbedtls_md5((const uint8_t *)"yGAdlopoPVldABfn", 16, key) == 0);
    CHECK(lan_discovery_build(&f.config, encoded, sizeof(encoded), &len) == 0);
    CHECK(be32(encoded + 10) == 0x23 && be32(encoded + 6) == 0);
    CHECK(peer_decode(key, encoded, len, plaintext, &plain_len) == 0);
    CHECK(plain_len > 4 && be32(plaintext) == 0); plaintext[plain_len] = 0;
    root = cJSON_Parse((const char *)plaintext + 4);
    CHECK(root && strcmp(cJSON_GetObjectItemCaseSensitive(root, "gwId")->valuestring, "device123") == 0);
    CHECK(strcmp(cJSON_GetObjectItemCaseSensitive(root, "version")->valuestring, "3.5") == 0);
    CHECK(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(root, "encrypt")));
    CHECK(cJSON_GetObjectItemCaseSensitive(root, "active")->valueint == 2);
    cJSON_Delete(root);
    len = peer_encode(key, 7, 0x25, (const uint8_t *)request, strlen(request), encoded);
    CHECK(lan_discovery_parse(&f.config, encoded, len, ip, sizeof(ip)) == 0);
    CHECK(strcmp(ip, "192.168.1.2") == 0);
    CHECK(lan_discovery_parse(&f.config, encoded, len - 1, ip, sizeof(ip)) == LAN_ERR_PROTOCOL);
    CHECK(lan_discovery_parse(&f.config, encoded, len, ip, 4) == LAN_ERR_LIMIT);
    encoded[len - 5] ^= 1;
    CHECK(lan_discovery_parse(&f.config, encoded, len, ip, sizeof(ip)) == LAN_ERR_AUTH);
    CHECK(lan_discovery_build(&f.config, encoded, 16, &len) == LAN_ERR_LIMIT && len == 0);
    cleanup(&f); CHECK(allocations == 0); return 0;
}

int main(void)
{
    cJSON_Hooks hooks = {test_malloc, test_free};
    cJSON_InitHooks(&hooks);
    RUN(test_authenticated_control_query_report);
    RUN(test_coalesced_frames);
    RUN(test_replay_and_bad_tag);
    RUN(test_unauthenticated_and_out_of_order_handshake);
    RUN(test_zero_leading_session_key);
    RUN(test_bad_lengths_headers_and_partial_timeout);
    RUN(test_strict_dp_json_and_query_bounds);
    RUN(test_authenticated_header_tamper_and_rekey);
    RUN(test_control_lexical_validation_before_normalization);
    RUN(test_json_depth_and_late_packet);
    RUN(test_entropy_allocation_and_emit_failure);
    RUN(test_discovery_independent_peer);
    printf("=== LAN protocol: %u passed, %u failed (%u total) ===\n", passed, failed, total);
    return failed ? 1 : 0;
}
