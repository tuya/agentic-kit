/*
 * pal_freertos.c -- lwIP sockets + FreeRTOS PAL implementation.
 *
 * Targets: any FreeRTOS + lwIP system (ESP-IDF, NuttX, NXP MCUXpresso,
 * STM32CubeMX with FreeRTOS+lwIP middleware, etc.).
 *
 * Provides only raw TCP socket operations -- TLS and crypto are handled
 * internally by the SDK via mbedTLS.
 *
 * --------------------------------------------------------------------------
 * Required FreeRTOSConfig.h flags
 * --------------------------------------------------------------------------
 *   configUSE_RECURSIVE_MUTEXES     1   (xSemaphoreCreateRecursiveMutex)
 *   configSUPPORT_DYNAMIC_ALLOCATION 1  (pvPortMalloc, dynamic semaphores/tasks)
 *   INCLUDE_vTaskSuspend            1   (used by thread-join shim)
 *   INCLUDE_vTaskDelete             1   (joiner deletes the worker task)
 *
 * Also recommended: a FreeRTOS heap implementation that supports vPortFree
 * (heap_3.c / heap_4.c / heap_5.c -- NOT heap_1.c).
 *
 * --------------------------------------------------------------------------
 * Required lwipopts.h flags
 * --------------------------------------------------------------------------
 *   LWIP_COMPAT_SOCKETS  1  (expose unprefixed socket()/connect()/...)
 *   LWIP_DNS             1  (for getaddrinfo)
 *   LWIP_DNS_API_DECLARE_H_ERRNO 0   (we don't use h_errno)
 *   LWIP_SOCKET          1
 *   LWIP_NETDB           1  (or LWIP_DNS_API_DEFINITIONS_LWIP=1)
 *   LWIP_SO_RCVTIMEO     1  (for SO_RCVTIMEO)
 *   LWIP_SO_SNDTIMEO     1  (for SO_SNDTIMEO)
 *   LWIP_TCP             1  (for TCP_NODELAY)
 *
 * Optional but recommended:
 *   LWIP_PROVIDE_ERRNO   1  (per-task errno; otherwise a libc errno is fine)
 *
 * --------------------------------------------------------------------------
 * Build-time tunables
 * --------------------------------------------------------------------------
 *   PAL_FR_TASK_STACK_WORDS  worker task stack in StackType_t units
 *                            (default ~6 KB; bump if TLS handshake stack
 *                            overflows)
 *   PAL_FR_TASK_PRIORITY     worker task priority
 *   PAL_FR_TASK_NAME         task name string (debug only)
 *
 * Usage:
 *   cfg.pal = tai_pal_freertos();
 */

#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <errno.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "lwip/sockets.h"
#include "lwip/netdb.h"

#include "pal.h"

/* Worker task tunables — override via -D at build time if needed. */
#ifndef PAL_FR_TASK_STACK_WORDS
#define PAL_FR_TASK_STACK_WORDS  (6 * 1024 / sizeof(StackType_t))
#endif
#ifndef PAL_FR_TASK_PRIORITY
#define PAL_FR_TASK_PRIORITY     (tskIDLE_PRIORITY + 5)
#endif
#ifndef PAL_FR_TASK_NAME
#define PAL_FR_TASK_NAME         "tai_worker"
#endif

/* lwIP doesn't have signals; MSG_NOSIGNAL is a no-op. */
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
} fr_tcp_t;

static void *pal_tcp_connect(const char *host, uint16_t port, uint32_t timeout_ms)
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

    /* Non-blocking connect so we can bound it with select() (O_NONBLOCK / F_*
     * come from lwip/sockets.h; lwip_fcntl is always available). */
    int fl = lwip_fcntl(fd, F_GETFL, 0);
    lwip_fcntl(fd, F_SETFL, fl | O_NONBLOCK);

    int rc = connect(fd, res->ai_addr, res->ai_addrlen);
    if (rc != 0 && errno != EINPROGRESS) {
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
            close(fd);
            freeaddrinfo(res);
            return NULL;
        }
        int soerr = 0;
        socklen_t sl = sizeof(soerr);
        if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &soerr, &sl) != 0 || soerr != 0) {
            close(fd);
            freeaddrinfo(res);
            return NULL;
        }
    }
    /* Restore blocking mode: tcp_send/tcp_recv rely on it (SO_SNDTIMEO/SO_RCVTIMEO). */
    lwip_fcntl(fd, F_SETFL, fl);
    freeaddrinfo(res);

    /* Disable Nagle for low-latency */
    int flag = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));

    fr_tcp_t *h = (fr_tcp_t *)pvPortMalloc(sizeof(fr_tcp_t));
    if (!h) { close(fd); return NULL; }
    h->fd = fd;
    h->last_rcvtimeo_ms = UINT32_MAX;
    h->last_sndtimeo_ms = UINT32_MAX;
    return h;
}

static int pal_tcp_send(void *handle, const uint8_t *buf, size_t len,
                         uint32_t timeout_ms)
{
    fr_tcp_t *h = (fr_tcp_t *)handle;
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

    int n;
    do {
        n = send(h->fd, buf, len, flags);
    } while (n < 0 && errno == EINTR);

    if (n > 0) return n;
    if (n == 0) return 0;
    if (errno == EAGAIN || errno == EWOULDBLOCK)
        return PAL_ERR_AGAIN;
    return PAL_ERR_NET;
}

static int pal_tcp_recv(void *handle, uint8_t *buf, size_t buf_len,
                         uint32_t timeout_ms)
{
    fr_tcp_t *h = (fr_tcp_t *)handle;
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

    int n;
    do {
        n = recv(h->fd, buf, buf_len, flags);
    } while (n < 0 && errno == EINTR);

    if (n > 0) return n;
    if (n == 0) return 0; /* EOF */
    if (errno == EAGAIN || errno == EWOULDBLOCK)
        return PAL_ERR_AGAIN;
    return PAL_ERR_NET;
}

static int pal_tcp_poll(void *handle, int events, uint32_t timeout_ms)
{
    fr_tcp_t *h = (fr_tcp_t *)handle;
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
    fr_tcp_t *h = (fr_tcp_t *)handle;
    close(h->fd);
    vPortFree(h);
}

/* -------------------------------------------------------------------------
 * Time
 * ------------------------------------------------------------------------- */
static uint64_t pal_time_ms(void)
{
    /* xTaskGetTickCount() is 32-bit on most ports; cast before multiply so
     * the result keeps growing past 49 days at 1 ms tick.  Caller only uses
     * the value for elapsed-time deltas, so wrap/precision past that is
     * irrelevant. */
    return (uint64_t)xTaskGetTickCount() * (uint64_t)portTICK_PERIOD_MS;
}

/* -------------------------------------------------------------------------
 * Memory
 * ------------------------------------------------------------------------- */
static void *pal_malloc(size_t sz) { return pvPortMalloc(sz); }
static void  pal_free(void *p)     { vPortFree(p); }

/* -------------------------------------------------------------------------
 * Mutex (FreeRTOS recursive semaphore)
 * ------------------------------------------------------------------------- */
static void *pal_mutex_create(void)
{
    /* Recursive mutex: the same thread can lock it multiple times. This is
     * REQUIRED, not an optimisation: iot-client's DP layer locks ctx->mutex in
     * iot_dp_schema_check_update() and, while holding it, calls
     * iot_dp_rebuild()/restore_json()/init_defaults() which each re-lock the
     * same mutex (see modules/iot-client/src/iot_dp.c). A plain mutex would
     * deadlock there. (tai-client and the TLS yield mutex do NOT rely on
     * recursion -- only the DP schema-update path does.) */
    return (void *)xSemaphoreCreateRecursiveMutex();
}
static void pal_mutex_lock(void *m)
{
    xSemaphoreTakeRecursive((SemaphoreHandle_t)m, portMAX_DELAY);
}
static void pal_mutex_unlock(void *m)
{
    xSemaphoreGiveRecursive((SemaphoreHandle_t)m);
}
static void pal_mutex_destroy(void *m)
{
    vSemaphoreDelete((SemaphoreHandle_t)m);
}

/* -------------------------------------------------------------------------
 * Thread (FreeRTOS task with semaphore-based join)
 *
 * FreeRTOS has no native join primitive.  We give the task a "done"
 * semaphore that it signals just before suspending itself; the joining
 * thread waits on the semaphore, then deletes the task.  Doing the delete
 * from the joiner (rather than vTaskDelete(NULL) in the shim) avoids the
 * race where the joiner returns before the kernel has actually torn down
 * the task.
 * ------------------------------------------------------------------------- */
typedef struct {
    TaskHandle_t       task;
    SemaphoreHandle_t  done;
    void              *(*func)(void *);
    void              *arg;
} fr_thread_t;

static void thread_shim(void *arg)
{
    fr_thread_t *t = (fr_thread_t *)arg;
    t->func(t->arg);
    xSemaphoreGive(t->done);
    vTaskSuspend(NULL);   /* wait for joiner to vTaskDelete us */
    for (;;) { }          /* unreachable */
}

static int pal_thread_create(void **handle, void *(*func)(void *), void *arg)
{
    fr_thread_t *t = (fr_thread_t *)pal_malloc(sizeof(fr_thread_t));
    if (!t) return -1;
    t->func = func;
    t->arg  = arg;
    t->done = xSemaphoreCreateBinary();
    if (!t->done) { pal_free(t); return -1; }

    BaseType_t rc = xTaskCreate(thread_shim,
                                 PAL_FR_TASK_NAME,
                                 PAL_FR_TASK_STACK_WORDS,
                                 t,
                                 PAL_FR_TASK_PRIORITY,
                                 &t->task);
    if (rc != pdPASS) {
        vSemaphoreDelete(t->done);
        pal_free(t);
        return -1;
    }
    *handle = t;
    return 0;
}

static int pal_thread_join(void *handle)
{
    if (!handle) return -1;
    fr_thread_t *t = (fr_thread_t *)handle;
    xSemaphoreTake(t->done, portMAX_DELAY);
    vTaskDelete(t->task);
    vSemaphoreDelete(t->done);
    pal_free(t);   /* must match pal_malloc in pal_thread_create (SPIRAM-capable) */
    return 0;
}

/* -------------------------------------------------------------------------
 * Public factory
 * ------------------------------------------------------------------------- */
static const pal_t g_freertos_pal = {
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

const pal_t *tai_pal_freertos(void)
{
    return &g_freertos_pal;
}
