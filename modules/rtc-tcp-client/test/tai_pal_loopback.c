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
void tai_loopback_reset(void)
{
    lb_init_once();
    lb_fifo_clear(&g_rx);
    lb_fifo_clear(&g_tx);
    pthread_mutex_lock(&g_state_mtx);
    g_eof = 0;
    g_now_ms = 1700000000000ULL;
    g_rand_state = 1;
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

static int lb_tcp_send(void *tcp, const uint8_t *buf, size_t len,
                       uint32_t timeout_ms)
{
    (void)tcp; (void)timeout_ms;
    lb_fifo_push(&g_tx, buf, len);
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

    /* Cap effective timeout so the worker thread can check running flag. */
    if (timeout_ms > 50) timeout_ms = 50;

    /* Poll in ~1ms slices until timeout. */
    uint32_t waited = 0;
    while (waited < timeout_ms) {
        lb_sleep_ms(1);
        waited++;

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
