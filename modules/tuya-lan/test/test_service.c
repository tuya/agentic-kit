/* Deterministic service tests: the protocol double emits its input verbatim;
 * the independent protocol suite verifies encryption and wire compatibility. */
#include "tuya_lan_protocol.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct fake_socket { unsigned index; };
struct lan_protocol {
    tuya_lan_config_t config;
    lan_protocol_emit_t emit;
    void *user;
    bool authenticated;
    uint64_t created;
};

static unsigned failures, cases, allocations, alloc_calls, fail_alloc_at;
static uint64_t now;
static struct {
    struct fake_socket handles[10];
    unsigned pending, next_accept, closed, live, accepts, reads, sends;
    bool fail_udp, blocked;
    size_t send_limit;
    unsigned send_again_every;
    uint8_t input[8][4096], output[8][32768];
    size_t input_len[8], input_pos[8], output_len[8];
    bool eof[8];
    bool udp_pending;
    pal_addr_t udp_source, last_destination;
    char udp_data[32];
    unsigned udp_sends, nonzero_timeouts, wrong_close;
} io;

#define CHECK(e) do { if (!(e)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #e); failures++; return; } } while (0)
#define RUN(fn) do { cases++; fn(); } while (0)
static void *test_malloc(size_t len)
{
    alloc_calls++;
    if (fail_alloc_at && alloc_calls == fail_alloc_at) return NULL;
    void *p = malloc(len);
    if (p) allocations++;
    return p;
}
static void test_free(void *p) { if (p) allocations--; free(p); }
static uint64_t test_now(void) { return now; }
static void *unused_connect(const char *h, uint16_t p, uint32_t t)
{ (void)h; (void)p; (void)t; return NULL; }
static int unused_poll(void *h, int e, uint32_t t)
{ (void)h; (void)e; (void)t; return PAL_ERR_NET; }
static void unused_close(void *h) { (void)h; }
static void *unused_mutex(void) { return NULL; }
static int unused_thread(void **h, void *(*fn)(void *), void *arg)
{ (void)h; (void)fn; (void)arg; return -1; }
static int unused_join(void *h) { (void)h; return -1; }
static void unused_sleep(uint32_t ms) { (void)ms; }


int lan_protocol_create(const tuya_lan_config_t *c, uint32_t id,
                        lan_protocol_emit_t emit, void *user, lan_protocol_t **out)
{
    (void)id;
    *out = c->pal->malloc(sizeof(**out));
    if (!*out) return LAN_ERR_MEMORY;
    **out = (lan_protocol_t){ .config = *c, .emit = emit, .user = user, .created = now };
    return LAN_OK;
}
void lan_protocol_destroy(lan_protocol_t *p) { p->config.pal->free(p); }
int lan_protocol_feed(lan_protocol_t *p, const uint8_t *data, size_t len)
{
    p->authenticated = true;
    p->created = now;
    return p->emit(p->user, data, len);
}
int lan_protocol_tick(lan_protocol_t *p)
{
    uint32_t timeout = p->authenticated ? p->config.heartbeat_timeout_ms : p->config.handshake_timeout_ms;
    return now - p->created >= timeout ? LAN_ERR_TIMEOUT : LAN_OK;
}
bool lan_protocol_authenticated(const lan_protocol_t *p) { return p->authenticated; }
int lan_protocol_report(lan_protocol_t *p, const char *data, size_t len)
{
    return p->authenticated ? p->emit(p->user, (const uint8_t *)data, len) : LAN_ERR_AUTH;
}
int lan_protocol_send(lan_protocol_t *p, uint32_t command, uint32_t code,
                      const uint8_t *data, size_t len)
{
    (void)command; (void)code;
    return p->authenticated ? p->emit(p->user, data, len) : LAN_ERR_AUTH;
}
int lan_protocol_validate_state(const tuya_lan_config_t *c, const char *json, size_t len)
{
    (void)c;
    return json && len && *json == '{' ? LAN_OK : LAN_ERR_PARAM;
}
int lan_discovery_build(const tuya_lan_config_t *c, uint8_t *out, size_t capacity, size_t *len)
{
    (void)c;
    if (capacity < 8) return LAN_ERR_LIMIT;
    memcpy(out, "announce", 8); *len = 8; return LAN_OK;
}
int lan_discovery_parse(const tuya_lan_config_t *c, const uint8_t *data, size_t len,
                        char *ip, size_t capacity)
{
    (void)c;
    if (len >= capacity) return LAN_ERR_PROTOCOL;
    memcpy(ip, data, len); ip[len] = 0; return LAN_OK;
}

static int net_listen(const pal_t *pal, const char *ip, uint16_t port,
                       unsigned backlog, void **out)
{
    (void)pal; (void)ip; (void)port; (void)backlog;
    *out = &io.handles[0]; io.live++; return 0;
}
static int net_accept(void *listener, void **out, pal_addr_t *peer)
{
    (void)listener; (void)peer;
    io.accepts++;
    *out = NULL;
    if (!io.pending) return PAL_ERR_AGAIN;
    io.pending--; *out = &io.handles[2 + io.next_accept++]; io.live++; return 0;
}
static int net_send(void *handle, const uint8_t *data, size_t len, uint32_t timeout_ms)
{
    struct fake_socket *h = handle;
    if (timeout_ms) io.nonzero_timeouts++;
    unsigned i = h->index - 2;
    io.sends++;
    if (io.blocked || (io.send_again_every && io.sends % io.send_again_every == 0)) return PAL_ERR_AGAIN;
    if (io.send_limit && len > io.send_limit) len = io.send_limit;
    if (len > sizeof(io.output[i]) - io.output_len[i]) return PAL_ERR_NET;
    memcpy(io.output[i] + io.output_len[i], data, len); io.output_len[i] += len;
    return (int)len;
}
static int net_recv(void *handle, uint8_t *data, size_t capacity, uint32_t timeout_ms)
{
    struct fake_socket *h = handle;
    if (timeout_ms) io.nonzero_timeouts++;
    unsigned i = h->index - 2;
    io.reads++;
    if (io.input_pos[i] == io.input_len[i]) return io.eof[i] ? 0 : PAL_ERR_AGAIN;
    size_t len = io.input_len[i] - io.input_pos[i];
    if (len > capacity) len = capacity;
    memcpy(data, io.input[i] + io.input_pos[i], len); io.input_pos[i] += len;
    return (int)len;
}
static int net_udp_bind(const pal_t *pal, const char *ip, uint16_t port,
                        bool broadcast, void **out)
{
    (void)pal; (void)ip; (void)port; (void)broadcast;
    *out = NULL;
    if (io.fail_udp) return PAL_ERR_NET;
    *out = &io.handles[1]; io.live++; return 0;
}
static int net_sendto(void *h, const pal_addr_t *peer,
                       const uint8_t *data, size_t len)
{
    (void)h; (void)data;
    io.last_destination = *peer; io.udp_sends++; return (int)len;
}
static int net_recvfrom(void *h, pal_addr_t *peer,
                         uint8_t *data, size_t capacity)
{
    (void)h;
    if (!io.udp_pending) return PAL_ERR_AGAIN;
    io.udp_pending = false;
    size_t len = strlen(io.udp_data);
    if (len > capacity) return PAL_ERR_NET;
    *peer = io.udp_source; memcpy(data, io.udp_data, len); return (int)len;
}
static int net_port(void *handle, uint16_t *port)
{
    struct fake_socket *h = handle;
    *port = (uint16_t)(12340 + h->index); return 0;
}
static void net_tcp_close(void *handle)
{
    struct fake_socket *h = handle;
    if (h) { if (h->index == 1) io.wrong_close++; io.closed++; io.live--; }
}
static void net_udp_close(void *handle)
{
    struct fake_socket *h = handle;
    if (h) { if (h->index != 1) io.wrong_close++; io.closed++; io.live--; }
}
static const pal_t test_pal = {
    .tcp_connect = unused_connect, .tcp_send = net_send, .tcp_recv = net_recv,
    .tcp_poll = unused_poll, .tcp_close = net_tcp_close,
    .malloc = test_malloc, .free = test_free,
    .time_ms = test_now, .wal_clock_time = test_now,
    .mutex_create = unused_mutex, .mutex_lock = unused_close,
    .mutex_unlock = unused_close, .mutex_destroy = unused_close,
    .thread_create = unused_thread, .thread_join = unused_join,
    .sleep_ms = unused_sleep,
    .tcp_listen = net_listen, .tcp_accept = net_accept,
    .udp_bind = net_udp_bind, .udp_sendto = net_sendto,
    .udp_recvfrom = net_recvfrom, .socket_local_port = net_port,
    .udp_close = net_udp_close
};

static tuya_lan_config_t config(void)
{
    memset(&io, 0, sizeof(io));
    for (unsigned i = 0; i < 10; i++) io.handles[i].index = i;
    now = 0; fail_alloc_at = 0; alloc_calls = 0;
    tuya_lan_config_t c;
    tuya_lan_config_init(&c);
    c.pal = &test_pal;
    c.device_id = "device"; c.uuid = "uuid"; c.product_key = "product";
    c.local_key = "0123456789abcdef"; c.ipv4 = "127.0.0.1";
    c.bind_ip = "127.0.0.1"; c.broadcast_ip = "127.0.0.1";
    c.tcp_port = c.udp_port = 0;
    c.discovery_interval_ms = 0;
    return c;
}

static void test_config_and_lifecycle(void)
{
    tuya_lan_config_t c = config();
    tuya_lan_t *lan = NULL;
    pal_t incomplete = test_pal;
    incomplete.udp_close = NULL;
    c.pal = &incomplete;
    CHECK(pal_is_valid(c.pal));
    CHECK(tuya_lan_create(&c, &lan) == LAN_ERR_PARAM && !lan);
    incomplete = test_pal; incomplete.tcp_accept = NULL;
    CHECK(tuya_lan_create(&c, &lan) == LAN_ERR_PARAM && !lan);
    c.pal = &test_pal;
    c.local_key = "short";
    CHECK(tuya_lan_create(&c, &lan) == LAN_ERR_PARAM && !lan);
    c.local_key = "0123456789abcdef"; c.ipv4 = "127.0.0.256";
    CHECK(tuya_lan_create(&c, &lan) == LAN_ERR_PARAM);
    c.ipv4 = "127.0.0.1";
    CHECK(tuya_lan_create(&c, &lan) == LAN_OK);
    CHECK(tuya_lan_process(lan) == LAN_ERR_STATE);
    CHECK(tuya_lan_start(lan) == LAN_OK);
    CHECK(tuya_lan_start(lan) == LAN_ERR_STATE);
    CHECK(tuya_lan_tcp_port(lan) == 12340 && tuya_lan_udp_port(lan) == 12341);
    CHECK(tuya_lan_stop(lan) == LAN_OK && io.live == 0);
    CHECK(tuya_lan_tcp_port(lan) == 0 && tuya_lan_udp_port(lan) == 0);
    CHECK(tuya_lan_stop(lan) == LAN_OK);
    CHECK(tuya_lan_start(lan) == LAN_OK);
    tuya_lan_destroy(lan);
    CHECK(io.live == 0 && allocations == 0);
    CHECK(io.nonzero_timeouts == 0 && io.wrong_close == 0);
}

static void test_config_string_boundaries(void)
{
    tuya_lan_config_t c = config();
    tuya_lan_t *lan = NULL;
    char identity[65], oversized[66];
    memset(identity, 'a', sizeof(identity) - 1); identity[sizeof(identity) - 1] = '\0';
    memset(oversized, 'a', sizeof(oversized) - 1); oversized[sizeof(oversized) - 1] = '\0';
    c.device_id = c.uuid = c.product_key = identity;
    CHECK(tuya_lan_create(&c, &lan) == LAN_OK && lan);
    tuya_lan_destroy(lan);
    CHECK(allocations == 0);

    const char **fields[] = { &c.device_id, &c.uuid, &c.product_key };
    for (size_t i = 0; i < sizeof(fields) / sizeof(fields[0]); ++i) {
        *fields[i] = oversized;
        CHECK(tuya_lan_create(&c, &lan) == LAN_ERR_PARAM && !lan);
        CHECK(allocations == 0);
        *fields[i] = identity;
    }
    c.local_key = NULL;
    CHECK(tuya_lan_create(&c, &lan) == LAN_ERR_PARAM && !lan);
    c.local_key = "0123456789abcde";
    CHECK(tuya_lan_create(&c, &lan) == LAN_ERR_PARAM && !lan);
    c.local_key = "0123456789abcdefg";
    CHECK(tuya_lan_create(&c, &lan) == LAN_ERR_PARAM && !lan);
    c.local_key = "0123456789abcdef";
    CHECK(tuya_lan_create(&c, &lan) == LAN_OK && lan);
    tuya_lan_destroy(lan);
    CHECK(allocations == 0 && io.live == 0);
}

static void test_start_failure_and_allocations(void)
{
    for (unsigned i = 1; i <= 4; i++) {
        tuya_lan_config_t c = config();
        tuya_lan_t *lan = NULL; fail_alloc_at = i;
        CHECK(tuya_lan_create(&c, &lan) == LAN_ERR_MEMORY && !lan);
        CHECK(allocations == 0);
    }
    tuya_lan_config_t c = config();
    tuya_lan_t *lan = NULL;
    CHECK(tuya_lan_create(&c, &lan) == LAN_OK);
    io.fail_udp = true;
    CHECK(tuya_lan_start(lan) == LAN_ERR_NET && io.live == 0);
    io.fail_udp = false;
    CHECK(tuya_lan_start(lan) == LAN_OK);
    io.pending = 1; fail_alloc_at = alloc_calls + 1;
    CHECK(tuya_lan_process(lan) == LAN_OK && tuya_lan_client_count(lan) == 0);
    CHECK(io.live == 2);
    io.pending = 1; fail_alloc_at = alloc_calls + 2;
    CHECK(tuya_lan_process(lan) == LAN_OK && tuya_lan_client_count(lan) == 0);
    CHECK(io.live == 2);
    tuya_lan_destroy(lan);
    CHECK(allocations == 0 && io.live == 0);
    CHECK(io.nonzero_timeouts == 0 && io.wrong_close == 0);
}

static void test_partial_send_and_would_block(void)
{
    tuya_lan_config_t c = config();
    tuya_lan_t *lan = NULL;
    CHECK(tuya_lan_create(&c, &lan) == LAN_OK && tuya_lan_start(lan) == LAN_OK);
    io.pending = 1; io.blocked = true;
    io.input_len[0] = 100;
    for (size_t i = 0; i < io.input_len[0]; i++) io.input[0][i] = (uint8_t)i;
    CHECK(tuya_lan_process(lan) == LAN_OK && tuya_lan_client_count(lan) == 1);
    CHECK(io.output_len[0] == 0);
    io.blocked = false; io.send_limit = 7; io.send_again_every = 2;
    for (unsigned i = 0; i < 40 && io.output_len[0] < 100; i++)
        CHECK(tuya_lan_process(lan) == LAN_OK);
    CHECK(io.output_len[0] == 100 && !memcmp(io.output[0], io.input[0], 100));
    CHECK(tuya_lan_report(lan, "{\"dps\":{}}", 10) == LAN_OK);
    for (unsigned i = 0; i < 4; i++) CHECK(tuya_lan_process(lan) == LAN_OK);
    CHECK(io.output_len[0] == 110 && !memcmp(io.output[0] + 100, "{\"dps\":{}}", 10));
    tuya_lan_destroy(lan);
    CHECK(allocations == 0 && io.live == 0);
    CHECK(io.nonzero_timeouts == 0 && io.wrong_close == 0);
}

static void test_capacity_isolation_and_timeout(void)
{
    tuya_lan_config_t c = config();
    tuya_lan_t *lan = NULL;
    CHECK(tuya_lan_create(&c, &lan) == LAN_OK && tuya_lan_start(lan) == LAN_OK);
    io.pending = 5;
    CHECK(tuya_lan_process(lan) == LAN_OK && tuya_lan_client_count(lan) == 3);
    CHECK(io.accepts == 3);
    CHECK(tuya_lan_process(lan) == LAN_OK && tuya_lan_client_count(lan) == 3);
    CHECK(io.closed == 2);
    io.eof[0] = true;
    CHECK(tuya_lan_process(lan) == LAN_OK && tuya_lan_client_count(lan) == 2);
    io.input[1][0] = 'x'; io.input_len[1] = 1;
    now = 9000;
    CHECK(tuya_lan_process(lan) == LAN_OK);
    now = 10000;
    CHECK(tuya_lan_process(lan) == LAN_OK && tuya_lan_client_count(lan) == 1);
    now = 39000;
    CHECK(tuya_lan_process(lan) == LAN_OK && tuya_lan_client_count(lan) == 0);
    tuya_lan_destroy(lan);
    CHECK(allocations == 0 && io.live == 0);
    CHECK(io.nonzero_timeouts == 0 && io.wrong_close == 0);
}

static void test_queue_overflow_disconnects_only_target(void)
{
    tuya_lan_config_t c = config();
    tuya_lan_t *lan = NULL;
    CHECK(tuya_lan_create(&c, &lan) == LAN_OK && tuya_lan_start(lan) == LAN_OK);
    io.pending = 2; io.input[0][0] = 'x'; io.input_len[0] = 1;
    CHECK(tuya_lan_process(lan) == LAN_OK && tuya_lan_client_count(lan) == 2);
    io.blocked = true;
    char frame[TUYA_LAN_MAX_FRAME_SIZE];
    memset(frame, 'x', sizeof(frame)); frame[0] = '{';
    CHECK(tuya_lan_send(lan, 1, 0x20, 0, (uint8_t *)frame, sizeof(frame)) == LAN_OK);
    CHECK(tuya_lan_send(lan, 1, 0x20, 0, (uint8_t *)frame, sizeof(frame)) == LAN_OK);
    CHECK(tuya_lan_send(lan, 1, 0x20, 0, (uint8_t *)frame, 1) == LAN_ERR_LIMIT);
    CHECK(tuya_lan_process(lan) == LAN_OK && tuya_lan_client_count(lan) == 1);
    CHECK(tuya_lan_send(lan, 2, 0x20, 0, (uint8_t *)frame, 1) == LAN_ERR_AUTH);
    tuya_lan_destroy(lan);
    CHECK(allocations == 0 && io.live == 0);
    CHECK(io.nonzero_timeouts == 0 && io.wrong_close == 0);
}

static void test_discovery_source_and_interval(void)
{
    tuya_lan_config_t c = config();
    tuya_lan_t *lan = NULL; c.discovery_interval_ms = 5000;
    CHECK(tuya_lan_create(&c, &lan) == LAN_OK && tuya_lan_start(lan) == LAN_OK);
    CHECK(tuya_lan_process(lan) == LAN_OK && io.udp_sends == 1);
    CHECK(!strcmp(io.last_destination.ip, "127.0.0.1") && io.last_destination.port == 6667);
    now = 4999; CHECK(tuya_lan_process(lan) == LAN_OK && io.udp_sends == 1);
    now = 5000; CHECK(tuya_lan_process(lan) == LAN_OK && io.udp_sends == 2);
    io.udp_source = (pal_addr_t){ "127.0.0.2", 2468 };
    strcpy(io.udp_data, "127.0.0.1"); io.udp_pending = true;
    CHECK(tuya_lan_process(lan) == LAN_OK && io.udp_sends == 2);
    strcpy(io.udp_data, "127.0.0.2"); io.udp_pending = true;
    CHECK(tuya_lan_process(lan) == LAN_OK && io.udp_sends == 3);
    CHECK(!strcmp(io.last_destination.ip, "127.0.0.2") && io.last_destination.port == 2468);
    tuya_lan_destroy(lan);
    CHECK(allocations == 0 && io.live == 0);
    CHECK(io.nonzero_timeouts == 0 && io.wrong_close == 0);
}

int main(void)
{
    RUN(test_config_and_lifecycle); RUN(test_config_string_boundaries);
    RUN(test_start_failure_and_allocations);
    RUN(test_partial_send_and_would_block); RUN(test_capacity_isolation_and_timeout);
    RUN(test_queue_overflow_disconnects_only_target); RUN(test_discovery_source_and_interval);
    printf("tuya_lan_service: %u cases, %u failures\n", cases, failures);
    return failures ? 1 : 0;
}
