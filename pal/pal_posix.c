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
#include <limits.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <fcntl.h>

#include "pal.h"
#include "log.h"

#ifndef AGENTIC_KIT_ENABLE_LAN
#define AGENTIC_KIT_ENABLE_LAN 0
#endif

/* Suppress SIGPIPE on send() when peer has closed the connection.
 * Linux uses MSG_NOSIGNAL per-call; macOS/BSD uses SO_NOSIGPIPE socket option. */
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

/* -------------------------------------------------------------------------
 * Shared handle for TCP connections, listeners and UDP sockets
 * ------------------------------------------------------------------------- */
typedef struct {
    int fd;
    uint32_t last_rcvtimeo_ms;
    uint32_t last_sndtimeo_ms;
    const pal_t *pal; /* NULL for legacy tcp_connect handles. */
} posix_socket_t;

static void *pal_tcp_connect(const char *host, uint16_t port, uint32_t timeout_ms)
{
    char port_str[8];
    snprintf(port_str, sizeof(port_str), "%u", (unsigned)port);

    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    int gai_rc = getaddrinfo(host, port_str, &hints, &res);
    if (gai_rc != 0 || !res) {
        log_tag_error("pal", "getaddrinfo(%s:%u) failed: %s",
                 host, (unsigned)port, gai_strerror(gai_rc));
        return NULL;
    }

    int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (fd < 0) {
        log_tag_error("pal", "socket() failed: %s", strerror(errno));
        freeaddrinfo(res);
        return NULL;
    }

    /* Non-blocking connect so we can bound it with select(). */
    int fl = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, fl | O_NONBLOCK);

    int rc = connect(fd, res->ai_addr, res->ai_addrlen);
    if (rc != 0 && errno != EINPROGRESS) {
        log_tag_error("pal", "connect(%s:%u) failed: %s",
                 host, (unsigned)port, strerror(errno));
        close(fd);
        freeaddrinfo(res);
        return NULL;
    }
    if (rc != 0) {
        /* EINPROGRESS: connect is under way. */
        if (timeout_ms == 0) {  /* non-blocking single attempt: don't wait */
            close(fd);
            freeaddrinfo(res);
            return NULL;
        }
        fd_set wfds;
        FD_ZERO(&wfds);
        FD_SET(fd, &wfds);
        struct timeval tv;
        tv.tv_sec  = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;
        int sel;
        do {
            sel = select(fd + 1, NULL, &wfds, NULL, &tv);
        } while (sel < 0 && errno == EINTR);
        if (sel <= 0) {  /* timeout (0) or error (<0) */
            log_tag_error("pal", "connect(%s:%u) %s", host, (unsigned)port,
                     sel == 0 ? "timed out" : strerror(errno));
            close(fd);
            freeaddrinfo(res);
            return NULL;
        }
        int soerr = 0;
        socklen_t sl = sizeof(soerr);
        if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &soerr, &sl) != 0 || soerr != 0) {
            log_tag_error("pal", "connect(%s:%u) failed: %s",
                     host, (unsigned)port, strerror(soerr));
            close(fd);
            freeaddrinfo(res);
            return NULL;
        }
    }
    /* Restore blocking mode: tcp_send/tcp_recv rely on it (SO_SNDTIMEO/SO_RCVTIMEO). */
    fcntl(fd, F_SETFL, fl);
    freeaddrinfo(res);

    /* Disable Nagle for low-latency */
    int flag = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));

#ifdef SO_NOSIGPIPE
    /* macOS/BSD: suppress SIGPIPE at socket level */
    setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &flag, sizeof(flag));
#endif

    posix_socket_t *h = (posix_socket_t *)malloc(sizeof(posix_socket_t));
    if (!h) {
        log_tag_error("pal", "tcp handle alloc failed");
        close(fd);
        return NULL;
    }
    h->fd = fd;
    h->last_rcvtimeo_ms = UINT32_MAX;
    h->last_sndtimeo_ms = UINT32_MAX;
    h->pal = NULL;
    return h;
}

static int pal_tcp_send(void *handle, const uint8_t *buf, size_t len,
                         uint32_t timeout_ms)
{
    posix_socket_t *h = (posix_socket_t *)handle;
    if (!h || !buf || !len || len > INT_MAX) return PAL_ERR_INVALID;
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
    } while (n < 0 && errno == EINTR && timeout_ms != 0);

    if (n > 0) return (int)n;
    if (n == 0) return 0;
    if (errno == EAGAIN || errno == EWOULDBLOCK ||
        (errno == EINTR && timeout_ms == 0))
        return PAL_ERR_AGAIN;
    log_tag_error("pal", "tcp_send failed: %s (errno=%d)", strerror(errno), errno);
    return PAL_ERR_NET;
}

static int pal_tcp_recv(void *handle, uint8_t *buf, size_t buf_len,
                         uint32_t timeout_ms)
{
    posix_socket_t *h = (posix_socket_t *)handle;
    if (!h || !buf || !buf_len || buf_len > INT_MAX) return PAL_ERR_INVALID;
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
    } while (n < 0 && errno == EINTR && timeout_ms != 0);

    if (n > 0) return (int)n;
    if (n == 0) return 0; /* EOF */
    if (errno == EAGAIN || errno == EWOULDBLOCK ||
        (errno == EINTR && timeout_ms == 0))
        return PAL_ERR_AGAIN;
    log_tag_error("pal", "tcp_recv failed: %s (errno=%d)", strerror(errno), errno);
    return PAL_ERR_NET;
}

static int pal_tcp_poll(void *handle, int events, uint32_t timeout_ms)
{
    posix_socket_t *h = (posix_socket_t *)handle;
    if (!h || !(events & 3) || (events & ~3)) return PAL_ERR_INVALID;
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
    if (sel < 0) return errno == EINTR && timeout_ms == 0
                         ? PAL_ERR_AGAIN : PAL_ERR_NET;
    if (sel == 0) return 0;

    int result = 0;
    if ((events & 1) && FD_ISSET(h->fd, &rfds)) result |= 1;
    if ((events & 2) && FD_ISSET(h->fd, &wfds)) result |= 2;
    return result;
}

static void pal_tcp_close(void *handle)
{
    if (!handle) return;
    posix_socket_t *h = (posix_socket_t *)handle;
    close(h->fd);
    if (h->pal) h->pal->free(h);
    else free(h);
}

#if AGENTIC_KIT_ENABLE_LAN
/* -------------------------------------------------------------------------
 * Optional TCP server and UDP operations, sharing the TCP handle and I/O.
 * ------------------------------------------------------------------------- */
static int pal_socket_error(void)
{
    return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR
           ? PAL_ERR_AGAIN : PAL_ERR_NET;
}

static int pal_socket_address(const char *ip, uint16_t port,
                              struct sockaddr_in *address)
{
    memset(address, 0, sizeof(*address));
    address->sin_family = AF_INET;
    address->sin_port = htons(port);
    if (!ip || !*ip) address->sin_addr.s_addr = htonl(INADDR_ANY);
    else if (inet_pton(AF_INET, ip, &address->sin_addr) != 1)
        return PAL_ERR_INVALID;
    return 0;
}

static int pal_socket_prepare(int fd, bool nonblocking)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0 || fcntl(fd, F_SETFL, nonblocking ? flags | O_NONBLOCK
                                                 : flags & ~O_NONBLOCK) < 0)
        return PAL_ERR_NET;
#ifdef SO_NOSIGPIPE
    int one = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one)) < 0)
        return PAL_ERR_NET;
#endif
    return 0;
}

static int pal_socket_handle(const pal_t *pal, int fd, void **out)
{
    posix_socket_t *h = pal->malloc(sizeof(*h));
    if (!h) {
        log_tag_error("pal", "socket handle alloc failed");
        close(fd);
        return PAL_ERR_NET;
    }
    h->fd = fd;
    h->last_rcvtimeo_ms = UINT32_MAX;
    h->last_sndtimeo_ms = UINT32_MAX;
    h->pal = pal;
    *out = h;
    return 0;
}

static int pal_socket_bind(const pal_t *pal, const char *ip, uint16_t port,
                           int type, bool broadcast, void **out)
{
    struct sockaddr_in address;
    if (!out) return PAL_ERR_INVALID;
    *out = NULL;
    if (!pal || !pal->malloc || !pal->free ||
        pal_socket_address(ip, port, &address)) return PAL_ERR_INVALID;
    int fd = socket(AF_INET, type, 0);
    if (fd < 0) return PAL_ERR_NET;
    int one = 1;
    if (pal_socket_prepare(fd, true) ||
        (type == SOCK_STREAM && setsockopt(fd, SOL_SOCKET, SO_REUSEADDR,
                                          &one, sizeof(one)) < 0) ||
        (broadcast && setsockopt(fd, SOL_SOCKET, SO_BROADCAST,
                                &one, sizeof(one)) < 0) ||
        bind(fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        close(fd);
        return PAL_ERR_NET;
    }
    return pal_socket_handle(pal, fd, out);
}

static int pal_tcp_listen(const pal_t *pal, const char *ip, uint16_t port,
                          unsigned backlog, void **out)
{
    if (!out) return PAL_ERR_INVALID;
    *out = NULL;
    if (!backlog || backlog > INT_MAX) return PAL_ERR_INVALID;
    int rc = pal_socket_bind(pal, ip, port, SOCK_STREAM, false, out);
    if (rc) return rc;
    posix_socket_t *h = *out;
    if (listen(h->fd, (int)backlog) < 0) {
        pal_tcp_close(h);
        *out = NULL;
        return PAL_ERR_NET;
    }
    return 0;
}

static int pal_tcp_accept(void *handle, void **out, pal_addr_t *peer)
{
    posix_socket_t *h = handle;
    struct sockaddr_in address;
    socklen_t len = sizeof(address);
    if (!out) return PAL_ERR_INVALID;
    *out = NULL;
    if (!h || !h->pal) return PAL_ERR_INVALID;
    int fd = accept(h->fd, (struct sockaddr *)&address, &len);
    if (fd < 0) return pal_socket_error();
    /* Accepted sockets retain bounded blocking I/O for timeout_ms > 0;
     * tcp_send/recv use MSG_DONTWAIT for the LAN loop's timeout_ms == 0. */
    if (pal_socket_prepare(fd, false)) { close(fd); return PAL_ERR_NET; }
    int one = 1;
    if (setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one)) < 0) {
        close(fd);
        return PAL_ERR_NET;
    }
    if (peer) {
        memset(peer, 0, sizeof(*peer));
        if (!inet_ntop(AF_INET, &address.sin_addr, peer->ip, sizeof(peer->ip))) {
            close(fd);
            return PAL_ERR_NET;
        }
        peer->port = ntohs(address.sin_port);
    }
    return pal_socket_handle(h->pal, fd, out);
}

static int pal_udp_bind(const pal_t *pal, const char *ip, uint16_t port,
                        bool broadcast, void **out)
{
    return pal_socket_bind(pal, ip, port, SOCK_DGRAM, broadcast, out);
}

static int pal_udp_sendto(void *handle, const pal_addr_t *peer,
                          const uint8_t *data, size_t len)
{
    posix_socket_t *h = handle;
    struct sockaddr_in address;
    if (!h || !peer || !data || len > INT_MAX ||
        pal_socket_address(peer->ip, peer->port, &address)) return PAL_ERR_INVALID;
    int n = (int)sendto(h->fd, data, len, MSG_NOSIGNAL,
                        (struct sockaddr *)&address, sizeof(address));
    return n >= 0 ? n : pal_socket_error();
}

static int pal_udp_recvfrom(void *handle, pal_addr_t *peer,
                            uint8_t *data, size_t capacity)
{
    posix_socket_t *h = handle;
    if (!h || !peer || !data || !capacity || capacity >= INT_MAX)
        return PAL_ERR_INVALID;
    struct sockaddr_in address;
    /* recvmsg consumes an oversized datagram while reporting truncation. */
    struct iovec iov = { data, capacity };
    struct msghdr msg;
    memset(&msg, 0, sizeof(msg));
    msg.msg_name = &address;
    msg.msg_namelen = sizeof(address);
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    int n = (int)recvmsg(h->fd, &msg, 0);
    if (n < 0) return pal_socket_error();
    if ((msg.msg_flags & MSG_TRUNC) || (size_t)n > capacity) return PAL_ERR_NET;
    memset(peer, 0, sizeof(*peer));
    if (!inet_ntop(AF_INET, &address.sin_addr, peer->ip, sizeof(peer->ip)))
        return PAL_ERR_NET;
    peer->port = ntohs(address.sin_port);
    return n;
}

static int pal_socket_local_port(void *handle, uint16_t *port)
{
    posix_socket_t *h = handle;
    struct sockaddr_in address;
    socklen_t len = sizeof(address);
    if (!h || !port) return PAL_ERR_INVALID;
    if (getsockname(h->fd, (struct sockaddr *)&address, &len) < 0)
        return PAL_ERR_NET;
    *port = ntohs(address.sin_port);
    return 0;
}
#endif /* AGENTIC_KIT_ENABLE_LAN */

/* -------------------------------------------------------------------------
 * Time
 * ------------------------------------------------------------------------- */
static uint64_t pal_wal_clock_time(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts) != 0 || ts.tv_sec <= 0) return 0;
    return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)(ts.tv_nsec / 1000000);
}

static uint64_t pal_time_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)(ts.tv_nsec / 1000000);
}

static void pal_sleep_ms(uint32_t ms)
{
    if (ms == 0) return;
    struct timespec delay = { ms / 1000, (long)(ms % 1000) * 1000000L };
    while (nanosleep(&delay, &delay) < 0 && errno == EINTR) { }
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
    .wal_clock_time   = pal_wal_clock_time,
    .malloc           = pal_malloc,
    .free             = pal_free,
    .mutex_create     = pal_mutex_create,
    .mutex_lock       = pal_mutex_lock,
    .mutex_unlock     = pal_mutex_unlock,
    .mutex_destroy    = pal_mutex_destroy,
    .thread_create    = pal_thread_create,
    .thread_join      = pal_thread_join,
    .sleep_ms         = pal_sleep_ms,
#if AGENTIC_KIT_ENABLE_LAN
    .tcp_listen       = pal_tcp_listen,
    .tcp_accept       = pal_tcp_accept,
    .udp_bind         = pal_udp_bind,
    .udp_sendto       = pal_udp_sendto,
    .udp_recvfrom     = pal_udp_recvfrom,
    .socket_local_port = pal_socket_local_port,
    .udp_close        = pal_tcp_close,
#endif
};

const pal_t *tai_pal_posix(void)
{
    return &g_posix_pal;
}
