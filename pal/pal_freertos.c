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
 *   INCLUDE_vTaskDelay              1   (sleep_ms blocks without busy-waiting)
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
 * Build-time tunables (AGENTIC_KIT_PAL_FR_TASK_STACK_WORDS /
 * _PRIORITY / _NAME)
 *   Defaults & docs: pal_config_defaults.h (overrides are picked up via
 *   log.h; see that header's banner).
 *
 * UTC time requires libc gettimeofday(), maintained by the application/platform
 * through RTC/SNTP or another synchronization source. The SDK does not sync it.
 *
 * Usage:
 *   cfg.pal = tai_pal_freertos();
 *
 * If the libc UTC source does not suit the platform, override the callback:
 *   static pal_t pal;
 *   pal = *tai_pal_freertos();
 *   pal.wal_clock_time = app_utc_ms;
 *   cfg.pal = &pal;  // keep this table alive and immutable
 */

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <limits.h>
#include <errno.h>
#include <sys/time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "lwip/sockets.h"
#include "lwip/netdb.h"

#include "pal.h"
#include "log.h"

/* Worker task knob defaults (AGENTIC_KIT_PAL_FR_TASK_*) live in
 * pal_config_defaults.h, included below; it includes log.h first, so
 * integrator overrides still win. */
#include "pal_config_defaults.h"

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

    int gai_rc = getaddrinfo(host, port_str, &hints, &res);
    if (gai_rc != 0 || !res) {
        log_tag_error("pal", "getaddrinfo(%s:%u) failed: %d",
                 host, (unsigned)port, gai_rc);
        return NULL;
    }

    int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (fd < 0) {
        log_tag_error("pal", "socket() failed: errno=%d", errno);
        freeaddrinfo(res);
        return NULL;
    }

    /* Non-blocking connect so we can bound it with select() (O_NONBLOCK / F_*
     * come from lwip/sockets.h; lwip_fcntl is always available). */
    int fl = lwip_fcntl(fd, F_GETFL, 0);
    lwip_fcntl(fd, F_SETFL, fl | O_NONBLOCK);

    int rc = connect(fd, res->ai_addr, res->ai_addrlen);
    if (rc != 0 && errno != EINPROGRESS) {
        log_tag_error("pal", "connect(%s:%u) failed: errno=%d",
                 host, (unsigned)port, errno);
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
            log_tag_error("pal", "connect(%s:%u) %s (errno=%d)",
                     host, (unsigned)port,
                     sel == 0 ? "timed out" : "select failed", errno);
            close(fd);
            freeaddrinfo(res);
            return NULL;
        }
        int soerr = 0;
        socklen_t sl = sizeof(soerr);
        if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &soerr, &sl) != 0 || soerr != 0) {
            log_tag_error("pal", "connect(%s:%u) failed: errno=%d",
                     host, (unsigned)port, soerr);
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
    if (!h) {
        log_tag_error("pal", "tcp handle alloc failed");
        close(fd);
        return NULL;
    }
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
    log_tag_error("pal", "tcp_send failed: errno=%d", errno);
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
    log_tag_error("pal", "tcp_recv failed: errno=%d", errno);
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
/* Task context after scheduler start, never ISR. vTaskSetTimeOutState captures
 * ticks and their overflow count together, preserving multiple wraps without
 * polling or a tick hook. This relies on the TimeOut_t layout and critical
 * section in vTaskSetTimeOutState in FreeRTOS V10.4.3/V11.1.0, not a stable
 * extended-clock API. See ADR 0003 and tools/check_freertos_pal.py; recheck
 * vendor forks/upgrades. For 16/32-bit ticks, UBaseType_t must be at least 32 bits
 * and the kernel overflow count must remain representable. Native 64-bit ticks
 * use their own counter. A kernel restart establishes a new monotonic epoch.
 * Tickless-idle sleep counts only as the port compensates the kernel ticks.
 */
typedef char pal_supported_tick_width[
    (sizeof(TickType_t) * CHAR_BIT == 16 ||
     sizeof(TickType_t) * CHAR_BIT == 32 ||
     sizeof(TickType_t) * CHAR_BIT == 64) ? 1 : -1];
typedef char pal_supported_overflow_width[
    (sizeof(TickType_t) * CHAR_BIT == 64 ||
     sizeof(UBaseType_t) * CHAR_BIT >= 32) ? 1 : -1];
typedef char pal_valid_tick_rate[
    (configTICK_RATE_HZ > 0 &&
     (uint64_t)configTICK_RATE_HZ <= UINT32_MAX) ? 1 : -1];

static uint64_t pal_time_ms(void)
{
    TimeOut_t now;
    const unsigned tick_bits = (unsigned)(sizeof(TickType_t) * CHAR_BIT);
    vTaskSetTimeOutState(&now);
    uint64_t ticks = (uint64_t)now.xTimeOnEntering;
    /* Merge the kernel wrap count above the tick bits for 16/32-bit counters;
     * native 64-bit ticks keep their full counter. Literal shift counts stay
     * well-formed on every width: a variable count folds to 64 on the 64-bit
     * target and trips -Werror=shift-count-overflow (GCC) even in dead code. */
    if (tick_bits == 16U) {
        ticks |= (uint64_t)(UBaseType_t)now.xOverflowCount << 16;
    } else if (tick_bits == 32U) {
        ticks |= (uint64_t)(UBaseType_t)now.xOverflowCount << 32;
    }

    /* Convert by frequency, including fractional-ms ticks. Quotient/remainder
     * avoids overflowing ticks * 1000; saturate if milliseconds exceed uint64. */
    const uint64_t hz = (uint64_t)configTICK_RATE_HZ;
    const uint64_t seconds = ticks / hz;
    const uint64_t fraction = (ticks % hz) * 1000U / hz;
    if (seconds > UINT64_MAX / 1000U) return UINT64_MAX;
    const uint64_t whole = seconds * 1000U;
    if (fraction > UINT64_MAX - whole) return UINT64_MAX;
    return whole + fraction;
}

static uint64_t pal_wal_clock_time(void)
{
    /* Common FreeRTOS/lwIP ports provide libc UTC via gettimeofday(). A port
     * using another clock source can replace this callback in its PAL copy. */
    struct timeval tv;
    if (gettimeofday(&tv, NULL) != 0 || tv.tv_sec <= 0) return 0;
    return (uint64_t)tv.tv_sec * 1000ULL + (uint64_t)(tv.tv_usec / 1000);
}

static void pal_sleep_ms(uint32_t ms)
{
    if (ms == 0) return;

    uint64_t ticks = ((uint64_t)ms * configTICK_RATE_HZ + 999U) / 1000U;
    const uint64_t max_chunk = (uint64_t)portMAX_DELAY - 1U;
    while (ticks > 0) {
        uint64_t chunk = ticks > max_chunk ? max_chunk : ticks;
        vTaskDelay((TickType_t)(chunk + 1U));
        ticks -= chunk;
    }
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
                                 AGENTIC_KIT_PAL_FR_TASK_NAME,
                                 AGENTIC_KIT_PAL_FR_TASK_STACK_WORDS,
                                 t,
                                 AGENTIC_KIT_PAL_FR_TASK_PRIORITY,
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
};

const pal_t *tai_pal_freertos(void)
{
    return &g_freertos_pal;
}
