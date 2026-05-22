/*
 * pal.h -- Platform Abstraction Layer for the Tuya AI Foundation library.
 *
 * Implement this interface for your target platform and pass a pointer to a
 * pal_t struct when initialising a connection context.
 *
 * The SDK handles TLS and cryptography internally (via mbedTLS).  The PAL only
 * needs to provide raw TCP socket operations, timing, memory, threading, and
 * logging.
 *
 * Two ready-made implementations are provided in the pal/ directory:
 *   pal_posix.c   -- POSIX sockets + pthreads (Linux / macOS)
 *   pal_esp_idf.c -- lwIP sockets + FreeRTOS  (ESP32)
 */

#ifndef PAL_H
#define PAL_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------
 * Log levels — kept as legacy aliases of LOG_* (see log.h).
 * Logging itself now lives in the global log facade, not the PAL table.
 * ------------------------------------------------------------------------- */
#define TAI_LOG_ERROR  1
#define TAI_LOG_WARN   2
#define TAI_LOG_INFO   3
#define TAI_LOG_DEBUG  4

/* -------------------------------------------------------------------------
 * Return codes for tcp_send / tcp_recv.
 * ------------------------------------------------------------------------- */
#define PAL_ERR_NET    (-3)  /* fatal network error */
#define PAL_ERR_AGAIN  (-7)  /* timed out / would block, try again later */

/* -------------------------------------------------------------------------
 * pal_t -- function pointer table
 * -------------------------------------------------------------------------
 * Every callback below is mandatory.  pal_is_valid() rejects any pal_t
 * that leaves a field NULL, so SDK call sites may dereference these
 * pointers without re-checking.
 */
typedef struct pal {

    /* --- TCP socket ------------------------------------------------------
     * tcp_connect: open a TCP connection to host:port.
     *   Returns an opaque handle on success, NULL on failure.
     * tcp_send: attempt to write up to len bytes.
     *   Must block for at most timeout_ms milliseconds (0 = non-blocking try).
     *   Returns: >0 = bytes actually written (may be less than len),
     *            TAI_ERR_AGAIN (-7) = timed out / would block (try later),
     *            TAI_ERR_NET (-3) = fatal error.
     * tcp_recv: read up to buf_len bytes; return bytes read, 0 = EOF, <0 = err.
     *   Must block for at most timeout_ms milliseconds (0 = non-blocking peek).
     * tcp_close: close and free the TCP socket.
     */
    void *(*tcp_connect)(const char *host, uint16_t port);
    int   (*tcp_send)(void *handle, const uint8_t *buf, size_t len,
                      uint32_t timeout_ms);
    int   (*tcp_recv)(void *handle, uint8_t *buf, size_t buf_len,
                      uint32_t timeout_ms);
    void  (*tcp_close)(void *handle);

    /* --- TCP socket polling ----------------------------------------------
     * tcp_poll: check if socket is readable and/or writable without doing I/O.
     *   events bitmask: 1 = check readable, 2 = check writable.
     *   timeout_ms: max wait in milliseconds (0 = non-blocking check).
     *   Returns: bitmask of ready events (1=readable, 2=writable),
     *            0 on timeout, <0 on error.
     */
    int   (*tcp_poll)(void *handle, int events, uint32_t timeout_ms);

    /* --- Time ------------------------------------------------------------
     * Return milliseconds since some monotonic epoch (need not be wall-clock).
     */
    uint64_t (*time_ms)(void);

    /* --- Memory ----------------------------------------------------------
     * Standard malloc/free semantics.  malloc must return NULL on failure.
     */
    void *(*malloc)(size_t size);
    void  (*free)(void *ptr);

    /* --- Mutex -----------------------------------------------------------
     * mutex_create: allocate and initialise a recursive mutex; return handle.
     * mutex_lock / mutex_unlock: standard lock / unlock.
     * mutex_destroy: release resources.
     */
    void *(*mutex_create)(void);
    void  (*mutex_lock)(void *mutex);
    void  (*mutex_unlock)(void *mutex);
    void  (*mutex_destroy)(void *mutex);

    /* --- Thread ----------------------------------------------------------
     * thread_create: spawn a new thread running func(arg).
     *   Store an opaque handle in *handle.  Return 0 on success.
     * thread_join: block until the thread finishes; free handle resources.
     *   Return 0 on success.
     */
    int   (*thread_create)(void **handle, void *(*func)(void *), void *arg);
    int   (*thread_join)(void *handle);

} pal_t;

/* -------------------------------------------------------------------------
 * Validate that a pal_t exposes every callback the SDK relies on.
 * Both ai-tcp and iot-client call this once at init and reject any pal
 * that returns false, so downstream code may dereference these pointers
 * without further null checks.
 * ------------------------------------------------------------------------- */
static inline bool pal_is_valid(const pal_t *p)
{
    return p && p->tcp_connect && p->tcp_send && p->tcp_recv && p->tcp_close
             && p->tcp_poll && p->time_ms && p->malloc && p->free
             && p->mutex_create && p->mutex_lock && p->mutex_unlock
             && p->mutex_destroy && p->thread_create && p->thread_join;
}

#ifdef __cplusplus
}
#endif

#endif /* PAL_H */
