#include "pal.h"

#include <arpa/inet.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <time.h>

#ifdef PAL_TEST_FREERTOS
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
extern const pal_t *tai_pal_freertos(void);
/* Network tests use the host monotonic clock, not the fake kernel clock. */
void vTaskSetTimeOutState(TimeOut_t *snapshot) { memset(snapshot, 0, sizeof(*snapshot)); }
#define default_pal tai_pal_freertos
#else
extern const pal_t *tai_pal_posix(void);
#define default_pal tai_pal_posix
#endif

static int failures, cases, live_handles, fail_alloc;
#define CHECK(e) do { if (!(e)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #e); failures++; return; } } while (0)
#define RUN(fn) do { cases++; fn(); } while (0)
static void *tracked_malloc(size_t n)
{
    if (fail_alloc) return NULL;
    void *p = malloc(n);
    if (p) live_handles++;
    return p;
}
static void tracked_free(void *p) { if (p) live_handles--; free(p); }
static const pal_t memory = { .malloc = tracked_malloc, .free = tracked_free };

static int wait_udp(void *h, pal_addr_t *from, uint8_t *buf, size_t len)
{
    for (int i = 0; i < 200; i++) {
        int rc = default_pal()->udp_recvfrom(h, from, buf, len);
        if (rc != PAL_ERR_AGAIN) return rc;
        poll(NULL, 0, 1);
    }
    return PAL_ERR_AGAIN;
}
static int wait_accept(void *h, void **out, pal_addr_t *peer)
{
    for (int i = 0; i < 200; i++) {
        int rc = default_pal()->tcp_accept(h, out, peer);
        if (rc != PAL_ERR_AGAIN) return rc;
        poll(NULL, 0, 1);
    }
    return PAL_ERR_AGAIN;
}
static int wait_tcp(void *h, uint8_t *buf, size_t len)
{
    for (int i = 0; i < 200; i++) {
        int rc = default_pal()->tcp_recv(h, buf, len, 0);
        if (rc != PAL_ERR_AGAIN) return rc;
        poll(NULL, 0, 1);
    }
    return PAL_ERR_AGAIN;
}

static void test_invalid(void)
{
    const pal_t *n = default_pal();
    void *h = (void *)1;
    CHECK(pal_is_valid(n) && pal_has_server_network(n));
    CHECK(!pal_has_server_network(NULL));
    CHECK(n->udp_bind(&memory, "bad", 0, false, &h) == PAL_ERR_INVALID);
    CHECK(h == NULL);
    CHECK(n->tcp_listen(NULL, "127.0.0.1", 0, 3, &h) == PAL_ERR_INVALID);
    CHECK(n->tcp_send(NULL, (const uint8_t *)"x", 1, 0) == PAL_ERR_INVALID);
    h = (void *)1;
    CHECK(n->tcp_accept(NULL, &h, NULL) == PAL_ERR_INVALID && h == NULL);
    CHECK(n->tcp_listen(&memory, "127.0.0.1", 0, 0, &h) == PAL_ERR_INVALID && h == NULL);
    n->tcp_close(NULL); n->udp_close(NULL);
}

static void test_udp(void)
{
    const pal_t *n = default_pal();
    void *a = NULL, *b = NULL;
    pal_addr_t peer = { "127.0.0.1", 0 }, from;
    uint8_t buf[8] = {0};
    CHECK(n->udp_bind(&memory, "127.0.0.1", 0, true, &a) == 0);
    CHECK(n->udp_bind(&memory, "127.0.0.1", 0, false, &b) == 0);
    CHECK(n->socket_local_port(b, &peer.port) == 0 && peer.port != 0);
    CHECK(n->udp_recvfrom(b, &from, buf, sizeof(buf)) == PAL_ERR_AGAIN);
    CHECK(n->udp_sendto(a, &peer, (const uint8_t *)"hello", 5) == 5);
    CHECK(wait_udp(b, &from, buf, sizeof(buf)) == 5);
    CHECK(!memcmp(buf, "hello", 5) && !strcmp(from.ip, "127.0.0.1"));
    uint16_t a_port;
    CHECK(n->socket_local_port(a, &a_port) == 0 && from.port == a_port);
    CHECK(n->udp_sendto(b, &from, (const uint8_t *)"", 0) == 0);
    CHECK(wait_udp(a, &peer, buf, sizeof(buf)) == 0);
    n->udp_close(a); n->udp_close(b);
}

static void test_udp_oversize(void)
{
    const pal_t *n = default_pal();
    void *a = NULL, *b = NULL;
    pal_addr_t peer = { "127.0.0.1", 0 }, from;
    uint8_t guarded[5] = {0, 0, 0, 0, 0xa5};
    CHECK(n->udp_bind(&memory, "127.0.0.1", 0, false, &a) == 0);
    CHECK(n->udp_bind(&memory, "127.0.0.1", 0, false, &b) == 0);
    CHECK(n->socket_local_port(b, &peer.port) == 0);
    CHECK(n->udp_sendto(a, &peer, (const uint8_t *)"oversize", 8) == 8);
    CHECK(wait_udp(b, &from, guarded, 4) == PAL_ERR_NET);
    CHECK(guarded[4] == 0xa5);
    CHECK(n->udp_recvfrom(b, &from, guarded, 4) == PAL_ERR_AGAIN);
    n->udp_close(a); n->udp_close(b);
}

static void test_tcp(void)
{
    const pal_t *n = default_pal();
    void *listener = NULL, *accepted = NULL;
    pal_addr_t peer;
    uint16_t port;
    uint8_t buf[8];
    CHECK(n->tcp_listen(&memory, "127.0.0.1", 0, 3, &listener) == 0);
    CHECK(n->socket_local_port(listener, &port) == 0 && port != 0);
    CHECK(n->tcp_accept(listener, &accepted, &peer) == PAL_ERR_AGAIN);
    void *client = n->tcp_connect("127.0.0.1", port, 1000);
    CHECK(client != NULL);
    CHECK(wait_accept(listener, &accepted, &peer) == 0);
    CHECK(!strcmp(peer.ip, "127.0.0.1") && peer.port != 0);
    uint16_t local_port;
    CHECK(n->socket_local_port(accepted, &local_port) == 0 && local_port == port);
    CHECK(n->socket_local_port(client, &local_port) == 0 && local_port == peer.port);
    CHECK(n->tcp_recv(accepted, buf, sizeof(buf), 0) == PAL_ERR_AGAIN);
    CHECK(n->tcp_send(client, (const uint8_t *)"abc", 3, 1000) == 3);
    CHECK(wait_tcp(accepted, buf, sizeof(buf)) == 3 && !memcmp(buf, "abc", 3));
    CHECK(n->tcp_send(accepted, (const uint8_t *)"def", 3, 0) == 3);
    CHECK(n->tcp_recv(client, buf, sizeof(buf), 1000) == 3 && !memcmp(buf, "def", 3));
    n->tcp_close(client);
    CHECK(wait_tcp(accepted, buf, sizeof(buf)) == 0);
    n->tcp_close(accepted); n->tcp_close(listener);
}

static uint64_t monotonic_ms(void)
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) abort();
    return (uint64_t)now.tv_sec * 1000 + (uint64_t)now.tv_nsec / 1000000;
}

/* A listening socket is nonblocking, but accepted and connected TCP must both
 * honor positive timeouts, then return to nonblocking behavior at timeout 0. */
static void test_tcp_timeouts(void)
{
    const pal_t *n = default_pal();
    void *listener = NULL, *accepted = NULL;
    uint16_t port;
    uint8_t byte;
    CHECK(n->tcp_listen(&memory, "127.0.0.1", 0, 3, &listener) == 0);
    CHECK(n->socket_local_port(listener, &port) == 0);
    void *client = n->tcp_connect("127.0.0.1", port, 1000);
    CHECK(client != NULL);
    CHECK(wait_accept(listener, &accepted, NULL) == 0);
    void *ends[] = { accepted, client };
    for (size_t i = 0; i < sizeof(ends) / sizeof(ends[0]); i++) {
        uint64_t start = monotonic_ms();
        CHECK(n->tcp_recv(ends[i], &byte, 1, 40) == PAL_ERR_AGAIN);
        CHECK(monotonic_ms() - start >= 10);
        start = monotonic_ms();
        CHECK(n->tcp_recv(ends[i], &byte, 1, 0) == PAL_ERR_AGAIN);
        CHECK(monotonic_ms() - start < 100);
        CHECK(n->tcp_poll(ends[i], 1, 0) == 0);
        CHECK(n->tcp_poll(ends[i], 2, 0) == 2);
    }
    n->tcp_close(client); n->tcp_close(accepted); n->tcp_close(listener);
}

static void test_accept_allocation_failure(void)
{
    const pal_t *n = default_pal();
    void *listener = NULL, *accepted = (void *)1;
    uint16_t port;
    CHECK(n->tcp_listen(&memory, "127.0.0.1", 0, 3, &listener) == 0);
    CHECK(n->socket_local_port(listener, &port) == 0);
    void *client = n->tcp_connect("127.0.0.1", port, 1000);
    CHECK(client != NULL);
    fail_alloc = 1;
    int rc = wait_accept(listener, &accepted, NULL);
    fail_alloc = 0;
    CHECK(rc == PAL_ERR_NET && accepted == NULL);
    CHECK(live_handles == 1);
    uint8_t byte;
    CHECK(wait_tcp(client, &byte, 1) == 0);
    n->tcp_close(client); n->tcp_close(listener);
}

static void test_open_allocation_failure(void)
{
    const pal_t *n = default_pal();
    void *h = NULL;
    fail_alloc = 1;
    int rc = n->udp_bind(&memory, "127.0.0.1", 0, false, &h);
    fail_alloc = 0;
    CHECK(rc == PAL_ERR_NET && h == NULL);
    CHECK(live_handles == 0);
}

int main(void)
{
    RUN(test_invalid); RUN(test_udp); RUN(test_udp_oversize);
    RUN(test_tcp); RUN(test_tcp_timeouts); RUN(test_accept_allocation_failure);
    RUN(test_open_allocation_failure);
    if (live_handles != 0) { fprintf(stderr, "leaked %d handles\n", live_handles); failures++; }
    printf("pal_net: %d cases, %d failures\n", cases, failures);
    return failures ? 1 : 0;
}
