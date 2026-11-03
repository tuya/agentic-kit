/* Public LAN API regressions with the real protocol and deterministic PAL I/O. */
#include "tuya_lan.h"
#include "rng.h"
#include "cJSON.h"
#include "mbedtls/gcm.h"
#include "mbedtls/md.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CLIENTS 2u
#define WIRE_OVERHEAD 50u
#define MAX_SEND (TUYA_LAN_MAX_FRAME_SIZE - WIRE_OVERHEAD - 4u)
#define CHECK(e) do { if (!(e)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #e); return -1; } } while (0)

struct socket_handle { unsigned index; };
static struct {
    struct socket_handle handles[CLIENTS + 2];
    unsigned pending, accepted, live;
    bool blocked;
    int send_failure;
    uint8_t input[CLIENTS][TUYA_LAN_MAX_FRAME_SIZE];
    uint8_t output[CLIENTS][3u * TUYA_LAN_MAX_FRAME_SIZE];
    size_t input_len[CLIENTS], input_pos[CLIENTS], output_len[CLIENTS];
} io;
static unsigned allocations, random_counter;
static bool random_failure;
static uint64_t now;
static const uint8_t local_key[17] = "0123456789abcdef";

static void *tracked_malloc(size_t n)
{
    void *p = malloc(n);
    if (p) allocations++;
    return p;
}
static void tracked_free(void *p) { if (p) allocations--; free(p); }
static uint64_t clock_ms(void) { return now; }
static void unused_sleep(uint32_t ms) { (void)ms; }
static void unused_handle(void *p) { (void)p; }
static void *unused_mutex(void) { return NULL; }
static int unused_thread(void **p, void *(*fn)(void *), void *arg)
{ (void)p; (void)fn; (void)arg; return -1; }
static int unused_join(void *p) { (void)p; return -1; }
static void *unused_connect(const char *h, uint16_t port, uint32_t timeout)
{ (void)h; (void)port; (void)timeout; return NULL; }
static int unused_poll(void *p, int events, uint32_t timeout)
{ (void)p; (void)events; (void)timeout; return PAL_ERR_NET; }

int rng_bytes(const pal_t *pal, uint8_t *out, size_t len)
{
    (void)pal;
    if (random_failure) return -1;
    for (size_t i = 0; i < len; ++i) out[i] = (uint8_t)++random_counter;
    return 0;
}

static int listen_socket(const pal_t *pal, const char *ip, uint16_t port,
                         unsigned backlog, void **out)
{
    (void)pal; (void)ip; (void)port; (void)backlog;
    *out = &io.handles[0]; io.live++; return 0;
}
static int accept_socket(void *listener, void **out, pal_addr_t *peer)
{
    (void)listener; (void)peer;
    *out = NULL;
    if (!io.pending) return PAL_ERR_AGAIN;
    io.pending--;
    *out = &io.handles[2 + io.accepted++]; io.live++; return 0;
}
static int recv_socket(void *handle, uint8_t *out, size_t cap, uint32_t timeout)
{
    struct socket_handle *h = handle;
    unsigned i = h->index - 2u;
    if (timeout) return PAL_ERR_NET;
    size_t len = io.input_len[i] - io.input_pos[i];
    if (!len) return PAL_ERR_AGAIN;
    if (len > cap) len = cap;
    memcpy(out, io.input[i] + io.input_pos[i], len);
    io.input_pos[i] += len;
    return (int)len;
}
static int send_socket(void *handle, const uint8_t *data, size_t len, uint32_t timeout)
{
    struct socket_handle *h = handle;
    unsigned i = h->index - 2u;
    if (timeout || (int)i == io.send_failure) return PAL_ERR_NET;
    if (io.blocked) return PAL_ERR_AGAIN;
    if (len > sizeof(io.output[i]) - io.output_len[i]) return PAL_ERR_NET;
    memcpy(io.output[i] + io.output_len[i], data, len);
    io.output_len[i] += len;
    return (int)len;
}
static void close_socket(void *handle) { if (handle) io.live--; }
static int bind_udp(const pal_t *pal, const char *ip, uint16_t port,
                    bool broadcast, void **out)
{
    (void)pal; (void)ip; (void)port; (void)broadcast;
    *out = &io.handles[1]; io.live++; return 0;
}
static int send_udp(void *h, const pal_addr_t *peer, const uint8_t *data, size_t len)
{ (void)h; (void)peer; (void)data; return (int)len; }
static int recv_udp(void *h, pal_addr_t *peer, uint8_t *out, size_t cap)
{ (void)h; (void)peer; (void)out; (void)cap; return PAL_ERR_AGAIN; }
static int socket_port(void *handle, uint16_t *port)
{
    struct socket_handle *h = handle;
    *port = (uint16_t)(12000u + h->index); return 0;
}
static const pal_t pal = {
    .malloc = tracked_malloc, .free = tracked_free,
    .time_ms = clock_ms, .wal_clock_time = clock_ms, .sleep_ms = unused_sleep,
    .tcp_connect = unused_connect, .tcp_send = send_socket, .tcp_recv = recv_socket,
    .tcp_poll = unused_poll, .tcp_close = close_socket,
    .mutex_create = unused_mutex, .mutex_lock = unused_handle,
    .mutex_unlock = unused_handle, .mutex_destroy = unused_handle,
    .thread_create = unused_thread, .thread_join = unused_join,
    .tcp_listen = listen_socket, .tcp_accept = accept_socket,
    .udp_bind = bind_udp, .udp_sendto = send_udp, .udp_recvfrom = recv_udp,
    .socket_local_port = socket_port, .udp_close = close_socket
};

struct fixture {
    tuya_lan_t *lan;
    uint8_t keys[CLIENTS][16];
    uint32_t sequences[CLIENTS], ids[CLIENTS];
    unsigned command_calls, callback_calls;
    int nested_result;
    bool callback_socket_closed;
};
static uint32_t get32(const uint8_t *p)
{ return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static void put32(uint8_t *p, uint32_t n)
{ p[0] = (uint8_t)(n >> 24); p[1] = (uint8_t)(n >> 16); p[2] = (uint8_t)(n >> 8); p[3] = (uint8_t)n; }

static int receive_command(uint32_t id, uint32_t command, const uint8_t *data,
                            size_t len, void *user)
{
    struct fixture *f = user;
    if (len != 1 || data[0] >= CLIENTS) return -1;
    if (command == 0x21 || command == 0x22) {
        uint32_t target = command == 0x22 ? f->ids[0] : id;
        f->callback_calls++;
        f->nested_result = tuya_lan_send(f->lan, target, 0x20, 0, NULL, 1);
        f->callback_socket_closed = io.live == CLIENTS + 1u;
        /* Deliberately hide the nested error: its latched failure must win. */
        return LAN_OK;
    }
    if (command != 0x20) return -1;
    f->command_calls++;
    f->ids[data[0]] = id;
    return 0;
}
static int reject_report_from_callback(struct fixture *f)
{
    f->callback_calls++;
    f->nested_result = tuya_lan_report(f->lan, NULL, 1);
    f->callback_socket_closed = io.live == 2;
    return LAN_OK;
}
static int receive_dp(const char *json, size_t len, void *user)
{
    (void)json; (void)len;
    return reject_report_from_callback(user);
}
static int receive_query(char *json, size_t capacity, size_t *len, void *user)
{
    const char state[] = "{\"dps\":{}}";
    CHECK(capacity >= sizeof(state));
    memcpy(json, state, sizeof(state));
    *len = sizeof(state) - 1u;
    return reject_report_from_callback(user);
}
static int enqueue(struct fixture *f, unsigned peer, const uint8_t key[16],
                   uint32_t command, const uint8_t *data, size_t len)
{
    size_t offset = io.input_pos[peer] == io.input_len[peer] ? 0 : io.input_len[peer];
    uint8_t *frame = io.input[peer] + offset;
    mbedtls_gcm_context gcm;
    int result;
    CHECK(len + WIRE_OVERHEAD <= sizeof(io.input[peer]) - offset);
    memset(frame, 0, 18);
    put32(frame, 0x6699);
    put32(frame + 6, ++f->sequences[peer]);
    put32(frame + 10, command); put32(frame + 14, (uint32_t)(len + 28u));
    for (size_t i = 0; i < 12; ++i) frame[18 + i] = (uint8_t)(i + f->sequences[peer]);
    mbedtls_gcm_init(&gcm);
    result = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, key, 128);
    if (!result) result = mbedtls_gcm_crypt_and_tag(&gcm, MBEDTLS_GCM_ENCRYPT,
                          len, frame + 18, 12, frame + 4, 14, data,
                          frame + 30, 16, frame + 30 + len);
    mbedtls_gcm_free(&gcm);
    CHECK(result == 0);
    put32(frame + 46 + len, 0x9966);
    io.input_pos[peer] = 0; io.input_len[peer] = offset + len + WIRE_OVERHEAD;
    return 0;
}
static int feed(struct fixture *f, unsigned peer, const uint8_t key[16],
                uint32_t command, const uint8_t *data, size_t len)
{
    CHECK(enqueue(f, peer, key, command, data, len) == 0);
    CHECK(tuya_lan_process(f->lan) == LAN_OK);
    return 0;
}
static int decode(unsigned peer, const uint8_t key[16], uint32_t command,
                  uint8_t *plain, size_t capacity, size_t *len)
{
    const uint8_t *frame = io.output[peer];
    size_t frame_len = io.output_len[peer];
    mbedtls_gcm_context gcm;
    int result;
    CHECK(frame_len >= WIRE_OVERHEAD && get32(frame) == 0x6699);
    CHECK(get32(frame + 10) == command && get32(frame + 14) == frame_len - 22u);
    CHECK(get32(frame + frame_len - 4u) == 0x9966);
    *len = frame_len - WIRE_OVERHEAD;
    CHECK(*len <= capacity);
    mbedtls_gcm_init(&gcm);
    result = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, key, 128);
    if (!result) result = mbedtls_gcm_auth_decrypt(&gcm, *len, frame + 18, 12,
                         frame + 4, 14, frame + frame_len - 20, 16, frame + 30, plain);
    mbedtls_gcm_free(&gcm);
    CHECK(result == 0 && *len >= 4 && get32(plain) == 0);
    io.output_len[peer] = 0;
    return 0;
}
static int authenticate(struct fixture *f, unsigned peer)
{
    uint8_t a[16], plain[64], proof[32], tag[16], mixed[16], index = (uint8_t)peer;
    size_t len;
    mbedtls_gcm_context gcm;
    int result;
    memset(a, (int)(peer + 0x30u), sizeof(a));
    CHECK(feed(f, peer, local_key, 3, a, sizeof(a)) == 0);
    CHECK(decode(peer, local_key, 4, plain, sizeof(plain), &len) == 0 && len == 52);
    CHECK(mbedtls_md_hmac(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256),
                         local_key, 16, plain + 4, 16, proof) == 0);
    for (size_t i = 0; i < 16; ++i) mixed[i] = a[i] ^ plain[4 + i];
    mbedtls_gcm_init(&gcm);
    result = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, local_key, 128);
    if (!result) result = mbedtls_gcm_crypt_and_tag(&gcm, MBEDTLS_GCM_ENCRYPT,
                         16, a, 12, NULL, 0, mixed, f->keys[peer], 16, tag);
    mbedtls_gcm_free(&gcm);
    CHECK(result == 0 && feed(f, peer, local_key, 5, proof, sizeof(proof)) == 0);
    CHECK(feed(f, peer, f->keys[peer], 0x20, &index, 1) == 0 && f->ids[peer] != 0);
    return 0;
}
static int init(struct fixture *f)
{
    tuya_lan_config_t config;
    memset(f, 0, sizeof(*f)); memset(&io, 0, sizeof(io));
    for (unsigned i = 0; i < CLIENTS + 2u; ++i) io.handles[i].index = i;
    io.send_failure = -1; now = 1; random_counter = 0; random_failure = false;
    tuya_lan_config_init(&config);
    config.pal = &pal;
    config.device_id = "device"; config.uuid = "uuid"; config.product_key = "product";
    config.local_key = (const char *)local_key; config.ipv4 = "127.0.0.1";
    config.discovery_interval_ms = 0;
    config.on_command = receive_command; config.user = f;
    config.on_dp = receive_dp; config.on_query = receive_query;
    CHECK(tuya_lan_create(&config, &f->lan) == LAN_OK && tuya_lan_start(f->lan) == LAN_OK);
    io.pending = CLIENTS;
    CHECK(tuya_lan_process(f->lan) == LAN_OK);
    for (unsigned i = 0; i < CLIENTS; ++i) CHECK(authenticate(f, i) == 0);
    CHECK(tuya_lan_client_count(f->lan) == CLIENTS);
    f->command_calls = 0;
    return 0;
}

static int restart(struct fixture *f)
{
    tuya_lan_destroy(f->lan); f->lan = NULL;
    CHECK(allocations == 0 && io.live == 0);
    return init(f);
}
static int test_report_rejections_close_all_targets_immediately(struct fixture *f)
{
    char state[MAX_SEND + 1u];
    const char prefix[] = "{\"dps\":{\"1\":\"", suffix[] = "\"}}";
    memset(state, 'x', MAX_SEND);
    memcpy(state, prefix, sizeof(prefix) - 1u);
    memcpy(state + MAX_SEND - (sizeof(suffix) - 1u), suffix, sizeof(suffix) - 1u);
    state[MAX_SEND] = 0;
    for (unsigned rejection = 0; rejection < 3; ++rejection) {
        int rc = rejection == 0 ? tuya_lan_report(f->lan, state, MAX_SEND) :
                 rejection == 1 ? tuya_lan_report(f->lan, "invalid", 7) :
                                  tuya_lan_report(f->lan, NULL, 1);
        CHECK(rc == (rejection == 0 ? LAN_ERR_LIMIT : LAN_ERR_PARAM));
        CHECK(io.live == 2 && tuya_lan_client_count(f->lan) == 0);
        CHECK(tuya_lan_send(f->lan, f->ids[0], 0x20, 0, NULL, 0) == LAN_ERR_STATE);
        if (rejection < 2) CHECK(restart(f) == 0);
    }
    return 0;
}
static int test_send_rejections_close_only_target_immediately(struct fixture *f)
{
    uint8_t data[MAX_SEND + 1u], plain[64];
    size_t len;
    memset(data, 'x', sizeof(data));
    CHECK(tuya_lan_send(f->lan, UINT32_MAX, 0x20, 0, NULL, 0) == LAN_ERR_STATE);
    CHECK(tuya_lan_send(f->lan, 0, 0x20, 0, NULL, 0) == LAN_ERR_PARAM);
    CHECK(io.live == CLIENTS + 2u && tuya_lan_client_count(f->lan) == CLIENTS);
    for (unsigned rejection = 0; rejection < 3; ++rejection) {
        int rc = rejection == 0 ? tuya_lan_send(f->lan, f->ids[0], 0x20, 0, data, sizeof(data)) :
                 rejection == 1 ? tuya_lan_send(f->lan, f->ids[0], 0x20, 0, NULL, 1) :
                                  tuya_lan_send(f->lan, f->ids[0], 3, 0, NULL, 0);
        CHECK(rc == (rejection == 0 ? LAN_ERR_LIMIT : LAN_ERR_PARAM));
        CHECK(io.live == CLIENTS + 1u && tuya_lan_client_count(f->lan) == 1);
        CHECK(tuya_lan_send(f->lan, f->ids[1], 0x20, 0, (const uint8_t *)"ok", 2) == LAN_OK);
        CHECK(tuya_lan_process(f->lan) == LAN_OK);
        CHECK(decode(1, f->keys[1], 0x20, plain, sizeof(plain), &len) == 0);
        CHECK(len == 6 && !memcmp(plain + 4, "ok", 2));
        if (rejection < 2) CHECK(restart(f) == 0);
    }
    return 0;
}
static int test_callback_error_closes_before_return_and_stops_coalesced_frames(struct fixture *f)
{
    uint8_t index = 0;
    CHECK(enqueue(f, 0, f->keys[0], 0x21, &index, 1) == 0);
    CHECK(enqueue(f, 0, f->keys[0], 0x20, &index, 1) == 0);
    CHECK(tuya_lan_process(f->lan) == LAN_OK);
    CHECK(f->nested_result == LAN_ERR_PARAM && f->callback_socket_closed);
    CHECK(f->callback_calls == 1 && f->command_calls == 0);
    CHECK(io.live == CLIENTS + 1u && tuya_lan_client_count(f->lan) == 1);
    CHECK(io.output_len[0] == 0);
    return 0;
}
static int test_callback_failure_reaps_an_already_processed_target(struct fixture *f)
{
    unsigned before = allocations;
    uint8_t index = 1;
    CHECK(enqueue(f, 1, f->keys[1], 0x22, &index, 1) == 0);
    CHECK(enqueue(f, 1, f->keys[1], 0x20, &index, 1) == 0);
    CHECK(tuya_lan_process(f->lan) == LAN_OK);
    CHECK(f->nested_result == LAN_ERR_PARAM && f->callback_socket_closed);
    CHECK(f->callback_calls == 1 && f->command_calls == 1);
    CHECK(io.live == CLIENTS + 1u && tuya_lan_client_count(f->lan) == 1);
    CHECK(allocations + 2u == before);
    return 0;
}
static int test_dp_query_reentrant_report_failure(struct fixture *f)
{
    const char control_json[] = "{\"data\":{\"dps\":{\"1\":true}}}";
    uint8_t control[15u + sizeof(control_json) - 1u] = {'3', '.', '5'};
    uint8_t index = 1;
    memcpy(control + 15, control_json, sizeof(control_json) - 1u);
    for (unsigned query = 0; query < 2; ++query) {
        unsigned before = allocations;
        CHECK(enqueue(f, 1, f->keys[1], query ? TUYA_LAN_CMD_QUERY : TUYA_LAN_CMD_CONTROL,
                      query ? (const uint8_t *)"{}" : control,
                      query ? 2u : sizeof(control)) == 0);
        CHECK(enqueue(f, 1, f->keys[1], 0x20, &index, 1) == 0);
        CHECK(tuya_lan_process(f->lan) == LAN_OK);
        CHECK(f->nested_result == LAN_ERR_PARAM && f->callback_socket_closed);
        CHECK(f->callback_calls == 1 && f->command_calls == 0);
        CHECK(io.live == 2 && tuya_lan_client_count(f->lan) == 0);
        CHECK(io.output_len[0] == 0 && io.output_len[1] == 0);
        CHECK(allocations + 2u * CLIENTS == before);
        if (!query) CHECK(restart(f) == 0);
    }
    return 0;
}
static int test_queue_overflow_still_closes_target(struct fixture *f)
{
    uint8_t data[MAX_SEND];
    memset(data, 'x', sizeof(data)); io.blocked = true;
    CHECK(tuya_lan_send(f->lan, f->ids[0], 0x20, 0, data, sizeof(data)) == LAN_OK);
    CHECK(tuya_lan_send(f->lan, f->ids[0], 0x20, 0, data, sizeof(data)) == LAN_OK);
    CHECK(tuya_lan_send(f->lan, f->ids[0], 0x20, 0, data, 1) == LAN_ERR_LIMIT);
    CHECK(io.live == CLIENTS + 1u && tuya_lan_client_count(f->lan) == 1);
    io.blocked = false;
    CHECK(tuya_lan_process(f->lan) == LAN_OK && tuya_lan_client_count(f->lan) == 1);
    CHECK(tuya_lan_send(f->lan, f->ids[1], 0x20, 0, data, 1) == LAN_OK);
    CHECK(tuya_lan_process(f->lan) == LAN_OK);
    CHECK(io.output_len[1] > WIRE_OVERHEAD);
    return 0;
}
static int test_crypto_failure_still_closes_target(struct fixture *f)
{
    random_failure = true;
    CHECK(tuya_lan_send(f->lan, f->ids[0], 0x20, 0, NULL, 0) == LAN_ERR_CRYPTO);
    CHECK(io.live == CLIENTS + 1u && tuya_lan_client_count(f->lan) == 1);
    random_failure = false;
    CHECK(tuya_lan_process(f->lan) == LAN_OK && tuya_lan_client_count(f->lan) == 1);
    CHECK(tuya_lan_send(f->lan, f->ids[1], 0x20, 0, NULL, 0) == LAN_OK);
    CHECK(tuya_lan_process(f->lan) == LAN_OK && io.output_len[1] > WIRE_OVERHEAD);
    return 0;
}
static int test_network_failure_still_closes_target(struct fixture *f)
{
    CHECK(tuya_lan_send(f->lan, f->ids[0], 0x20, 0, NULL, 0) == LAN_OK);
    io.send_failure = 0;
    CHECK(tuya_lan_process(f->lan) == LAN_OK && tuya_lan_client_count(f->lan) == 1);
    CHECK(tuya_lan_send(f->lan, f->ids[1], 0x20, 0, NULL, 0) == LAN_OK);
    CHECK(tuya_lan_process(f->lan) == LAN_OK && io.output_len[1] > WIRE_OVERHEAD);
    return 0;
}

int main(void)
{
    cJSON_Hooks hooks = {tracked_malloc, tracked_free};
    unsigned failed = 0, cases = 0;
    const struct { const char *name; int (*run)(struct fixture *); } tests[] = {
        {"report_rejections_close_all_targets_immediately", test_report_rejections_close_all_targets_immediately},
        {"send_rejections_close_only_target_immediately", test_send_rejections_close_only_target_immediately},
        {"callback_error_closes_before_return_and_stops_coalesced_frames", test_callback_error_closes_before_return_and_stops_coalesced_frames},
        {"callback_failure_reaps_an_already_processed_target", test_callback_failure_reaps_an_already_processed_target},
        {"dp_query_reentrant_report_failure", test_dp_query_reentrant_report_failure},
        {"queue_overflow_still_closes_target", test_queue_overflow_still_closes_target},
        {"crypto_failure_still_closes_target", test_crypto_failure_still_closes_target},
        {"network_failure_still_closes_target", test_network_failure_still_closes_target}
    };
    cJSON_InitHooks(&hooks);
    for (size_t i = 0; i < sizeof(tests) / sizeof(tests[0]); ++i) {
        struct fixture f;
        int result = init(&f);
        if (!result) result = tests[i].run(&f);
        random_failure = false;
        tuya_lan_destroy(f.lan);
        if (allocations || io.live) result = -1;
        cases++;
        if (result) { failed++; fprintf(stderr, "FAIL %s\n", tests[i].name); }
    }
    printf("tuya_lan_api: %u cases, %u failures\n", cases, failed);
    return failed ? 1 : 0;
}
