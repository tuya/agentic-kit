/*
 * tests/tai_pal_loopback.c -- Implementation of the loopback PAL.
 */

#include "tai_pal_loopback.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <time.h>

#include "log.h"
#include "../src/tai_internal.h"   /* frame/packet codec + key derivation for the handshake mock */

/* =========================================================================
 * Byte FIFO: single-producer, single-consumer, mutex-protected.
 * ========================================================================= */
#define LB_BUF_CAP  (512 * 1024)

typedef struct {
    pthread_mutex_t mtx;
    uint8_t        *data;
    size_t          head;     /* read offset */
    size_t          len;      /* bytes valid from head */
    size_t          cap;
} lb_fifo_t;

static void lb_fifo_init(lb_fifo_t *f, size_t cap)
{
    pthread_mutex_init(&f->mtx, NULL);
    f->data = (uint8_t *)malloc(cap);
    f->head = 0;
    f->len  = 0;
    f->cap  = cap;
}

static void lb_fifo_destroy(lb_fifo_t *f)
{
    free(f->data);
    f->data = NULL;
    pthread_mutex_destroy(&f->mtx);
}

static void lb_fifo_clear(lb_fifo_t *f)
{
    pthread_mutex_lock(&f->mtx);
    f->head = 0;
    f->len  = 0;
    pthread_mutex_unlock(&f->mtx);
}

static void lb_fifo_compact(lb_fifo_t *f)
{
    if (f->head == 0) return;
    if (f->len > 0) memmove(f->data, f->data + f->head, f->len);
    f->head = 0;
}

static void lb_fifo_push(lb_fifo_t *f, const uint8_t *b, size_t n)
{
    pthread_mutex_lock(&f->mtx);
    if (f->head + f->len + n > f->cap) lb_fifo_compact(f);
    if (f->len + n > f->cap) {
        fprintf(stderr, "[loopback] FIFO overflow (len=%zu + n=%zu > cap=%zu)\n",
                f->len, n, f->cap);
        pthread_mutex_unlock(&f->mtx);
        return;
    }
    memcpy(f->data + f->head + f->len, b, n);
    f->len += n;
    pthread_mutex_unlock(&f->mtx);
}

static size_t lb_fifo_pop(lb_fifo_t *f, uint8_t *out, size_t cap)
{
    pthread_mutex_lock(&f->mtx);
    size_t n = f->len < cap ? f->len : cap;
    if (n > 0) memcpy(out, f->data + f->head, n);
    f->head += n;
    f->len  -= n;
    if (f->len == 0) f->head = 0;
    pthread_mutex_unlock(&f->mtx);
    return n;
}

static size_t lb_fifo_len(lb_fifo_t *f)
{
    pthread_mutex_lock(&f->mtx);
    size_t n = f->len;
    pthread_mutex_unlock(&f->mtx);
    return n;
}

/* =========================================================================
 * Global state
 * ========================================================================= */
static lb_fifo_t g_rx;  /* server->client bytes (read by tcp_recv) */
static lb_fifo_t g_tx;  /* client->server bytes (written by tcp_send) */
static int       g_inited = 0;
static int       g_eof    = 0;
static uint64_t  g_now_ms = 1700000000000ULL;
static uint64_t  g_rand_state = 1;
static int       g_fail_after_calls = -1;    /* -1 = disabled; else fail once
                                              * this many tcp_send calls have
                                              * succeeded since arming */
static int       g_send_calls = 0;           /* tcp_send calls since arming */
static uint32_t  g_recv_cap_ms = 50;          /* effective recv-block cap; see
                                              * lb_tcp_recv. Raise it to mimic a
                                              * real PAL that honours the full
                                              * timeout (exercises the SDK-level
                                              * worker poll cap). */

static pthread_mutex_t g_state_mtx = PTHREAD_MUTEX_INITIALIZER;

static void lb_init_once(void)
{
    if (g_inited) return;
    lb_fifo_init(&g_rx, LB_BUF_CAP);
    lb_fifo_init(&g_tx, LB_BUF_CAP);
    g_inited = 1;
}

/* =========================================================================
 * Public helpers
 * ========================================================================= */
static void lb_hs_reset(void);   /* handshake-mock reset (defined below) */

void tai_loopback_reset(void)
{
    lb_init_once();
    lb_fifo_clear(&g_rx);
    lb_fifo_clear(&g_tx);
    lb_hs_reset();
    pthread_mutex_lock(&g_state_mtx);
    g_eof = 0;
    g_now_ms = 1700000000000ULL;
    g_rand_state = 1;
    g_fail_after_calls = -1;
    g_send_calls = 0;
    g_recv_cap_ms = 50;
    pthread_mutex_unlock(&g_state_mtx);
}

void tai_loopback_set_recv_cap_ms(uint32_t cap_ms)
{
    pthread_mutex_lock(&g_state_mtx);
    g_recv_cap_ms = cap_ms;
    pthread_mutex_unlock(&g_state_mtx);
}

/* Arm a send failure: the first `ok_calls` tcp_send calls after this succeed,
 * then the next one fails (returns -1). ok_calls<0 disarms. Used to exercise
 * §6.3: ok_calls=0 fails the first write (control frame / SG header), ok_calls=1
 * lets the SG header through and fails the payload write (mid-frame desync). */
void tai_loopback_fail_send_after(int ok_calls)
{
    pthread_mutex_lock(&g_state_mtx);
    g_fail_after_calls = ok_calls;
    g_send_calls = 0;
    pthread_mutex_unlock(&g_state_mtx);
}

void tai_loopback_seed_random(uint64_t seed)
{
    pthread_mutex_lock(&g_state_mtx);
    g_rand_state = seed ? seed : 1;
    pthread_mutex_unlock(&g_state_mtx);
}

uint64_t tai_loopback_now_ms(void)
{
    pthread_mutex_lock(&g_state_mtx);
    uint64_t t = g_now_ms;
    pthread_mutex_unlock(&g_state_mtx);
    return t;
}

void tai_loopback_advance_time(uint64_t delta)
{
    pthread_mutex_lock(&g_state_mtx);
    g_now_ms += delta;
    pthread_mutex_unlock(&g_state_mtx);
}

void tai_loopback_push_recv(const uint8_t *data, size_t len)
{
    lb_init_once();
    lb_fifo_push(&g_rx, data, len);
}

size_t tai_loopback_pop_sent(uint8_t *buf, size_t cap)
{
    lb_init_once();
    return lb_fifo_pop(&g_tx, buf, cap);
}

size_t tai_loopback_peek_sent_len(void)
{
    lb_init_once();
    return lb_fifo_len(&g_tx);
}

void tai_loopback_close_connection(void)
{
    pthread_mutex_lock(&g_state_mtx);
    g_eof = 1;
    pthread_mutex_unlock(&g_state_mtx);
}

/* =========================================================================
 * TCP (FIFO-backed)
 *
 * The loopback substitutes for raw TCP, not TLS.  Tests using this PAL must
 * set cfg.disable_tls = 1 so the SDK uses pal->tcp_* directly.
 * ========================================================================= */
static void *lb_tcp_connect(const char *host, uint16_t port)
{
    (void)host; (void)port;
    lb_init_once();
    /* Return a non-NULL sentinel handle. */
    return (void *)0x1;
}

/* =========================================================================
 * Handshake mock: complete the SDK's ClientHello -> SessionNew handshake so
 * confirmed-connect (tai_connect waiting for a SessionNew ack) succeeds. We
 * read the encrypt_random the client puts in its ClientHello security-suit,
 * derive the same sign_key from the test-supplied local_key, and push back a
 * signed SessionNew ack. Modes let a test force a timeout or a rejected session.
 * ========================================================================= */
static struct {
    int     mode;             /* TAI_LB_HS_* */
    int     done;             /* ack pushed (or skipped) for this connection */
    uint8_t acc[512];         /* accumulate client bytes until the ClientHello frame */
    size_t  acc_len;
    char    local_key[64];    /* shared secret, set by tai_loopback_set_local_key */
} g_hs;

void tai_loopback_set_local_key(const char *lk)
{
    size_t n = lk ? strlen(lk) : 0;
    if (n >= sizeof(g_hs.local_key)) n = sizeof(g_hs.local_key) - 1;
    if (n) memcpy(g_hs.local_key, lk, n);
    g_hs.local_key[n] = '\0';
}

void tai_loopback_set_handshake_mode(int mode) { g_hs.mode = mode; }

static void lb_hs_reset(void)
{
    g_hs.done    = 0;
    g_hs.acc_len = 0;
    g_hs.mode    = TAI_LB_HS_ACK_OK;
    /* local_key persists across reset; set once per test via set_local_key. */
}

/* Fed every client->server send; on the first complete ClientHello frame it
 * derives keys and pushes a signed SessionNew ack (unless mode == NO_ACK). */
static void lb_hs_feed(const uint8_t *buf, size_t len)
{
    if (g_hs.done || g_hs.mode == TAI_LB_HS_NO_ACK) return;
    if (g_hs.acc_len + len > sizeof(g_hs.acc)) { g_hs.done = 1; return; }
    memcpy(g_hs.acc + g_hs.acc_len, buf, len);
    g_hs.acc_len += len;

    size_t need = tai_frame_total_size(g_hs.acc, g_hs.acc_len);
    if (need == 0 || g_hs.acc_len < need) return;   /* wait for the full frame */

    /* ClientHello is unsigned (sig_len = 0). */
    uint8_t frag; uint16_t fseq; const uint8_t *pl; size_t pl_len;
    if (tai_frame_decode(g_hs.acc, need, 0, &frag, &fseq, &pl, &pl_len) != TAI_OK) {
        g_hs.done = 1; return;
    }
    uint8_t pkt_type; tai_attr_t attrs[TAI_MAX_ATTRS]; int na = 0;
    const uint8_t *payload; size_t payload_len;
    if (tai_packet_decode(TAI_VER_21, pl, pl_len, &pkt_type,
                          attrs, TAI_MAX_ATTRS, &na, &payload, &payload_len) != TAI_OK
        || pkt_type != TAI_PKT_CLIENT_HELLO) {
        g_hs.done = 1; return;
    }
    const tai_attr_t *suit = tai_attr_find(attrs, na, TAI_ATTR_SECURITY_SUIT);
    if (!suit || suit->len < 33) { g_hs.done = 1; return; }

    uint8_t       sign_level = suit->value[0];
    const uint8_t *enc_rand  = suit->value + 1;            /* 32 bytes */
    uint8_t sig_len = (sign_level == TAI_SIGN_HMAC_SHA256) ? 32
                    : (sign_level == TAI_SIGN_HMAC_SHA1)   ? 20 : 0;

    uint8_t enc_key[32], sign_key[32];
    if (tai_crypto_derive_keys(TAI_VER_21,
                               (const uint8_t *)g_hs.local_key, strlen(g_hs.local_key),
                               enc_rand, 32, enc_key, sign_key,
                               tai_pal_loopback()) != TAI_OK) {
        g_hs.done = 1; return;
    }

    if (g_hs.mode == TAI_LB_HS_SESSION_CLOSE) {
        /* Server closes the session DURING the handshake (before any SessionNew
         * ack): push a signed SESSION_CLOSE frame instead of the ack. */
        uint8_t app[64];
        int alen = tai_packet_encode(TAI_VER_21, TAI_PKT_SESSION_CLOSE,
                                     NULL, 0, (const uint8_t *)"", 0, app, sizeof(app));
        if (alen > 0) {
            uint8_t frame[128];
            int flen = tai_frame_encode(TAI_FRAG_NONE, 1, app, (size_t)alen,
                                        sign_key, sig_len, tai_pal_loopback(),
                                        frame, sizeof(frame));
            if (flen > 0) lb_fifo_push(&g_rx, frame, (size_t)flen);
        }
        g_hs.done = 1;
        return;
    }

    if (g_hs.mode == TAI_LB_HS_AUTH_OK) {
        /* Production-server style: confirm the connect with a signed
         * AuthenticateResponse (pkt 3) carrying connection-status-code = 200,
         * and NO separate SessionNew ack. */
        uint8_t code_buf[2] = { 0x00, 0xC8 };   /* 200, big-endian u16 */
        tai_attr_t a = { .type  = TAI_ATTR_CONNECTION_STATUS_CODE,
                         .len   = 2,
                         .value = code_buf };
        uint8_t app[64];
        int alen = tai_packet_encode(TAI_VER_21, TAI_PKT_AUTHENTICATE_RESPONSE,
                                     &a, 1, (const uint8_t *)"", 0, app, sizeof(app));
        if (alen > 0) {
            uint8_t frame[128];
            int flen = tai_frame_encode(TAI_FRAG_NONE, 1, app, (size_t)alen,
                                        sign_key, sig_len, tai_pal_loopback(),
                                        frame, sizeof(frame));
            if (flen > 0) lb_fifo_push(&g_rx, frame, (size_t)flen);
        }
        g_hs.done = 1;
        return;
    }

    /* SessionNew ack: status OK by default (no attr 44), or a non-zero reject. */
    tai_attr_t ack_attrs[1]; int ack_na = 0;
    uint8_t status_buf[2] = { 0, 1 };   /* code 1 = rejected */
    if (g_hs.mode == TAI_LB_HS_REJECT) {
        ack_attrs[0].type  = TAI_ATTR_SESSION_STATUS_CODE;
        ack_attrs[0].len   = 2;
        ack_attrs[0].value = status_buf;
        ack_na = 1;
    }
    uint8_t app[64];
    int alen = tai_packet_encode(TAI_VER_21, TAI_PKT_SESSION_NEW,
                                 ack_na ? ack_attrs : NULL, ack_na,
                                 (const uint8_t *)"", 0, app, sizeof(app));
    if (alen > 0) {
        uint8_t frame[128];
        int flen = tai_frame_encode(TAI_FRAG_NONE, 1, app, (size_t)alen,
                                    sign_key, sig_len, tai_pal_loopback(),
                                    frame, sizeof(frame));
        if (flen > 0) lb_fifo_push(&g_rx, frame, (size_t)flen);
    }
    g_hs.done = 1;
}

static int lb_tcp_send(void *tcp, const uint8_t *buf, size_t len,
                       uint32_t timeout_ms)
{
    (void)tcp; (void)timeout_ms;

    /* Injected send failure (§6.3 atomicity test): fail once the armed number
     * of calls has succeeded — simulating a connection drop mid-frame. */
    pthread_mutex_lock(&g_state_mtx);
    int fail = (g_fail_after_calls >= 0 && g_send_calls >= g_fail_after_calls);
    if (!fail) g_send_calls++;
    pthread_mutex_unlock(&g_state_mtx);
    if (fail) return -1;

    lb_fifo_push(&g_tx, buf, len);
    lb_hs_feed(buf, len);            /* drive the handshake mock */
    return (int)len; /* PAL contract: >0 = bytes written */
}

/* Sleep helper for timeout emulation. */
static void lb_sleep_ms(uint32_t ms)
{
    struct timespec ts;
    ts.tv_sec  = ms / 1000;
    ts.tv_nsec = (long)(ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}

static uint64_t lb_time_ms(void);   /* defined below; used for a real-time recv timeout */

static int lb_tcp_recv(void *tcp, uint8_t *buf, size_t buf_len,
                       uint32_t timeout_ms)
{
    (void)tcp;

    /* Check EOF first. */
    pthread_mutex_lock(&g_state_mtx);
    int eof = g_eof;
    pthread_mutex_unlock(&g_state_mtx);
    if (eof) return 0;

    /* Fast path: data already available. */
    size_t n = lb_fifo_pop(&g_rx, buf, buf_len);
    if (n > 0) return (int)n;

    if (timeout_ms == 0) return -7; /* TAI_ERR_AGAIN */

    /* Cap effective timeout so the worker thread can check running flag. The cap
     * defaults to 50 ms (keeps tests snappy); a test can raise it via
     * tai_loopback_set_recv_cap_ms() to mimic a real PAL that honours the full
     * timeout, so the SDK-level worker poll cap (not this one) is what bounds
     * shutdown latency. */
    pthread_mutex_lock(&g_state_mtx);
    uint32_t cap = g_recv_cap_ms;
    pthread_mutex_unlock(&g_state_mtx);
    if (timeout_ms > cap) timeout_ms = cap;

    /* Poll in ~1ms slices until the timeout elapses, bounded by REAL elapsed
     * time, not slice count. Under scheduling load a 1ms sleep can overrun, so
     * counting iterations (waited++ per slice) would massively overshoot the
     * requested timeout -- a 2s request blocked ~15s on a loaded CI box. */
    uint64_t deadline = lb_time_ms() + timeout_ms;
    while (lb_time_ms() < deadline) {
        lb_sleep_ms(1);

        pthread_mutex_lock(&g_state_mtx);
        eof = g_eof;
        pthread_mutex_unlock(&g_state_mtx);
        if (eof) return 0;

        n = lb_fifo_pop(&g_rx, buf, buf_len);
        if (n > 0) return (int)n;
    }
    return -7; /* TAI_ERR_AGAIN */
}

static void lb_tcp_close(void *tcp) { (void)tcp; }

static int lb_tcp_poll(void *tcp, int events, uint32_t timeout_ms)
{
    (void)tcp; (void)timeout_ms;
    int ready = 0;
    if (events & 1) {
        if (lb_fifo_len(&g_rx) > 0) ready |= 1;
        pthread_mutex_lock(&g_state_mtx);
        if (g_eof) ready |= 1;
        pthread_mutex_unlock(&g_state_mtx);
    }
    if (events & 2) ready |= 2; /* always writable */
    return ready;
}

/* =========================================================================
 * Time / memory
 * ========================================================================= */
static uint64_t lb_time_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)ts.tv_nsec / 1000000ULL;
}
static void    *lb_malloc(size_t s) { return malloc(s); }
static void     lb_free  (void *p)  { free(p); }

/* =========================================================================
 * Mutex / thread (pthreads)
 * ========================================================================= */
static void *lb_mutex_create(void)
{
    pthread_mutex_t *m = (pthread_mutex_t *)malloc(sizeof(*m));
    if (!m) return NULL;
    pthread_mutexattr_t a;
    pthread_mutexattr_init(&a);
    pthread_mutexattr_settype(&a, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(m, &a);
    pthread_mutexattr_destroy(&a);
    return m;
}
static void lb_mutex_lock   (void *m) { pthread_mutex_lock   ((pthread_mutex_t *)m); }
static void lb_mutex_unlock (void *m) { pthread_mutex_unlock ((pthread_mutex_t *)m); }
static void lb_mutex_destroy(void *m) { pthread_mutex_destroy((pthread_mutex_t *)m); free(m); }

static int lb_thread_create(void **handle, void *(*func)(void *), void *arg)
{
    pthread_t *t = (pthread_t *)malloc(sizeof(*t));
    if (!t) return -1;
    if (pthread_create(t, NULL, func, arg) != 0) { free(t); return -1; }
    *handle = t;
    return 0;
}

static int lb_thread_join(void *handle)
{
    pthread_t *t = (pthread_t *)handle;
    int rc = pthread_join(*t, NULL);
    free(t);
    return rc == 0 ? 0 : -1;
}

/* =========================================================================
 * Logging -- suppressed by default. Set TAI_LOOPBACK_VERBOSE=1 to enable.
 * ========================================================================= */
static void lb_log(log_level_t level, const char *fmt, va_list args)
{
    static int checked = 0, enabled = 0;
    if (!checked) {
        const char *v = getenv("TAI_LOOPBACK_VERBOSE");
        enabled = (v && v[0] && v[0] != '0');
        checked = 1;
    }
    if (!enabled) return;
    static const char *lvl[] = { "?", "E", "W", "I", "D" };
    const char *l = (level >= 1 && level <= 4) ? lvl[level] : "?";
    char msg[1024];
    vsnprintf(msg, sizeof msg, fmt ? fmt : "", args);
    fprintf(stderr, "[TAI/%s] %s\n", l, msg);
}

/* =========================================================================
 * Factory
 * ========================================================================= */
static const pal_t g_loopback_pal = {
    .tcp_connect      = lb_tcp_connect,
    .tcp_send         = lb_tcp_send,
    .tcp_recv         = lb_tcp_recv,
    .tcp_close        = lb_tcp_close,
    .tcp_poll         = lb_tcp_poll,
    .time_ms          = lb_time_ms,
    .malloc           = lb_malloc,
    .free             = lb_free,
    .mutex_create     = lb_mutex_create,
    .mutex_lock       = lb_mutex_lock,
    .mutex_unlock     = lb_mutex_unlock,
    .mutex_destroy    = lb_mutex_destroy,
    .thread_create    = lb_thread_create,
    .thread_join      = lb_thread_join,
};

const pal_t *tai_pal_loopback(void)
{
    lb_init_once();
    log_set_handler(lb_log);
    return &g_loopback_pal;
}
