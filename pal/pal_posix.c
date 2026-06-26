/*
 * pal_posix.c -- POSIX sockets + pthreads PAL implementation.
 *
 * Targets: Linux, macOS (any POSIX-like system).
 * Provides only raw TCP socket operations -- TLS and crypto are handled
 * internally by the SDK via mbedTLS.
 *
 * Usage:
 *   cfg.pal = tai_pal_posix();
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <time.h>
#include <pthread.h>
#include <errno.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <fcntl.h>

#include "pal.h"

/* Suppress SIGPIPE on send() when peer has closed the connection.
 * Linux uses MSG_NOSIGNAL per-call; macOS/BSD uses SO_NOSIGPIPE socket option. */
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

/* -------------------------------------------------------------------------
 * TCP socket handle
 * ------------------------------------------------------------------------- */
typedef struct {
    int fd;
    uint32_t last_rcvtimeo_ms;
    uint32_t last_sndtimeo_ms;
} posix_tcp_t;

static void *pal_tcp_connect(const char *host, uint16_t port)
{
    char port_str[8];
    snprintf(port_str, sizeof(port_str), "%u", (unsigned)port);

    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(host, port_str, &hints, &res) != 0 || !res)
        return NULL;

    int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (fd < 0) { freeaddrinfo(res); return NULL; }

    if (connect(fd, res->ai_addr, res->ai_addrlen) != 0) {
        close(fd);
        freeaddrinfo(res);
        return NULL;
    }
    freeaddrinfo(res);

    /* Disable Nagle for low-latency */
    int flag = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));

#ifdef SO_NOSIGPIPE
    /* macOS/BSD: suppress SIGPIPE at socket level */
    setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &flag, sizeof(flag));
#endif

    posix_tcp_t *h = (posix_tcp_t *)malloc(sizeof(posix_tcp_t));
    if (!h) { close(fd); return NULL; }
    h->fd = fd;
    h->last_rcvtimeo_ms = UINT32_MAX;
    h->last_sndtimeo_ms = UINT32_MAX;
    return h;
}

static int pal_tcp_send(void *handle, const uint8_t *buf, size_t len,
                         uint32_t timeout_ms)
{
    posix_tcp_t *h = (posix_tcp_t *)handle;
    int flags = MSG_NOSIGNAL;

    if (timeout_ms == 0) {
        /* Non-blocking try-once: skip SO_SNDTIMEO, use MSG_DONTWAIT. */
        flags |= MSG_DONTWAIT;
    } else if (timeout_ms != h->last_sndtimeo_ms) {
        struct timeval tv;
        tv.tv_sec  = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;
        setsockopt(h->fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
        h->last_sndtimeo_ms = timeout_ms;
    }

    ssize_t n;
    do {
        n = send(h->fd, buf, len, flags);
    } while (n < 0 && errno == EINTR);

    if (n > 0) return (int)n;
    if (n == 0) return 0;
    if (errno == EAGAIN || errno == EWOULDBLOCK)
        return PAL_ERR_AGAIN;
    return PAL_ERR_NET;
}

static int pal_tcp_recv(void *handle, uint8_t *buf, size_t buf_len,
                         uint32_t timeout_ms)
{
    posix_tcp_t *h = (posix_tcp_t *)handle;
    int flags = 0;

    if (timeout_ms == 0) {
        /* Non-blocking peek: skip SO_RCVTIMEO, use MSG_DONTWAIT. */
        flags |= MSG_DONTWAIT;
    } else if (timeout_ms != h->last_rcvtimeo_ms) {
        struct timeval tv;
        tv.tv_sec  = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;
        setsockopt(h->fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        h->last_rcvtimeo_ms = timeout_ms;
    }

    ssize_t n;
    do {
        n = recv(h->fd, buf, buf_len, flags);
    } while (n < 0 && errno == EINTR);

    if (n > 0) return (int)n;
    if (n == 0) return 0; /* EOF */
    if (errno == EAGAIN || errno == EWOULDBLOCK)
        return PAL_ERR_AGAIN;
    return PAL_ERR_NET;
}

static int pal_tcp_poll(void *handle, int events, uint32_t timeout_ms)
{
    posix_tcp_t *h = (posix_tcp_t *)handle;
    fd_set rfds, wfds;
    FD_ZERO(&rfds);
    FD_ZERO(&wfds);
    if (events & 1) FD_SET(h->fd, &rfds);
    if (events & 2) FD_SET(h->fd, &wfds);

    struct timeval tv;
    tv.tv_sec  = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;

    int sel = select(h->fd + 1,
                     (events & 1) ? &rfds : NULL,
                     (events & 2) ? &wfds : NULL,
                     NULL, &tv);
    if (sel < 0) return -3;
    if (sel == 0) return 0;

    int result = 0;
    if ((events & 1) && FD_ISSET(h->fd, &rfds)) result |= 1;
    if ((events & 2) && FD_ISSET(h->fd, &wfds)) result |= 2;
    return result;
}

static void pal_tcp_close(void *handle)
{
    if (!handle) return;
    posix_tcp_t *h = (posix_tcp_t *)handle;
    close(h->fd);
    free(h);
}

/* -------------------------------------------------------------------------
 * Time
 * ------------------------------------------------------------------------- */
static uint64_t pal_time_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)(ts.tv_nsec / 1000000);
}

/* -------------------------------------------------------------------------
 * Memory
 * ------------------------------------------------------------------------- */
static void *pal_malloc(size_t sz) { return malloc(sz); }
static void  pal_free(void *p)     { free(p); }

/* -------------------------------------------------------------------------
 * Mutex (pthreads)
 * ------------------------------------------------------------------------- */
static void *pal_mutex_create(void)
{
    /* Recursive is REQUIRED, not optional: iot-client's DP schema-update path
     * locks ctx->mutex and re-locks it via nested helpers (see iot_dp.c /
     * pal.h). A non-recursive mutex would deadlock there. */
    pthread_mutex_t *m = (pthread_mutex_t *)malloc(sizeof(pthread_mutex_t));
    if (!m) return NULL;
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(m, &attr);
    pthread_mutexattr_destroy(&attr);
    return m;
}
static void pal_mutex_lock(void *m)    { pthread_mutex_lock((pthread_mutex_t *)m); }
static void pal_mutex_unlock(void *m)  { pthread_mutex_unlock((pthread_mutex_t *)m); }
static void pal_mutex_destroy(void *m) { pthread_mutex_destroy((pthread_mutex_t *)m); free(m); }

/* -------------------------------------------------------------------------
 * Thread (pthreads)
 * ------------------------------------------------------------------------- */
static int pal_thread_create(void **handle, void *(*func)(void *), void *arg)
{
    pthread_t *t = (pthread_t *)malloc(sizeof(pthread_t));
    if (!t) return -1;
    if (pthread_create(t, NULL, func, arg) != 0) { free(t); return -1; }
    *handle = t;
    return 0;
}

static int pal_thread_join(void *handle)
{
    if (!handle) return -1;
    pthread_t *t = (pthread_t *)handle;
    int rc = pthread_join(*t, NULL);
    free(t);
    return rc == 0 ? 0 : -1;
}

/* -------------------------------------------------------------------------
 * Public factory
 * ------------------------------------------------------------------------- */
static const pal_t g_posix_pal = {
    .tcp_connect      = pal_tcp_connect,
    .tcp_send         = pal_tcp_send,
    .tcp_recv         = pal_tcp_recv,
    .tcp_close        = pal_tcp_close,
    .tcp_poll         = pal_tcp_poll,
    .time_ms          = pal_time_ms,
    .malloc           = pal_malloc,
    .free             = pal_free,
    .mutex_create     = pal_mutex_create,
    .mutex_lock       = pal_mutex_lock,
    .mutex_unlock     = pal_mutex_unlock,
    .mutex_destroy    = pal_mutex_destroy,
    .thread_create    = pal_thread_create,
    .thread_join      = pal_thread_join,
};

const pal_t *tai_pal_posix(void)
{
    return &g_posix_pal;
}
