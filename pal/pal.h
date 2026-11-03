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
 *   pal_freertos.c -- lwIP sockets + FreeRTOS (ESP32)
 * The FreeRTOS UTC callback reads libc gettimeofday(); the POSIX adapter reads
 * CLOCK_REALTIME. The application/platform owns clock synchronization. If the
 * default UTC source does not suit the target, copy its table into a long-lived
 * application PAL and replace wal_clock_time before using the SDK.
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
#define PAL_ERR_INVALID (-2) /* invalid network arguments */

/* Numeric IPv4 endpoint used by the optional server/datagram operations.
 * A NULL bind IP means INADDR_ANY. Addresses returned by the port are NUL-terminated. */
typedef struct {
    char ip[16];
    uint16_t port;
} pal_addr_t;

/* -------------------------------------------------------------------------
 * pal_t -- function pointer table
 * -------------------------------------------------------------------------
 * The base callbacks are mandatory. pal_is_valid() checks them at initialization.
 * The optional server/datagram callbacks at the end are checked separately by
 * their consumers; their fields exist even when the implementation is disabled.
 *
 * ADDING, REMOVING OR REORDERING A MEMBER requires reviewing four separate
 * designated-initializer tables (and pal_is_valid() for base callbacks):
 *   pal/pal_posix.c, pal/pal_freertos.c,
 *   modules/rtc-tcp-client/test/tai_pal_loopback.c, and .../test/test_core.c
 * (test_core intentionally leaves networking/thread/sleep callbacks NULL because
 * it never passes that table through pal_is_valid()).
 * C99 zero-fills omitted designated members. The host clock regression matrix
 * compiles pal_freertos.c against test shims and checks its table, but the real
 * FreeRTOS/lwIP build is still the ESP-IDF component. Keep that table in step:
 * a missing base callback means pal_is_valid() rejects the port; optional
 * capabilities need their own completeness check. All consumers must rebuild
 * after a table layout change. Host shims do not verify device integration.
 */
typedef struct pal {

    /* --- TCP socket ------------------------------------------------------
     * tcp_connect: open a TCP connection to host:port.
     *   Must block for at most timeout_ms milliseconds while establishing the
     *   connection (0 = non-blocking single attempt, matching tcp_send/tcp_recv:
     *   the connect is tried once and, if it cannot complete immediately, fails).
     *   timeout_ms > 0 bounds the connect so a stalled peer cannot hang the caller
     *   past the deadline.
     *   Returns an opaque handle on success, NULL on timeout or failure.
     * tcp_send: attempt to write up to len bytes.
     *   Must block for at most timeout_ms milliseconds (0 = non-blocking try).
     *   Returns: >0 = bytes actually written (may be less than len),
     *            TAI_ERR_AGAIN (-7) = timed out / would block (try later),
     *            TAI_ERR_NET (-3) = fatal error.
     * tcp_recv: read up to buf_len bytes; return bytes read, 0 = EOF, <0 = err.
     *   Must block for at most timeout_ms milliseconds (0 = non-blocking peek).
     * tcp_close: close and free the TCP socket.
     */
    void *(*tcp_connect)(const char *host, uint16_t port, uint32_t timeout_ms);
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
     * time_ms: milliseconds since a local monotonic epoch, for elapsed
     *   time, deadlines and keepalive. Never adjusted by wall-clock sync.
     * wal_clock_time: wall-clock milliseconds since 1970-01-01 00:00:00 UTC
     *   (Unix epoch, no timezone offset). Return 0 while UTC is unavailable.
     *   Synchronization may move this clock forwards or backwards. The app
     *   must synchronize it before sending cloud requests or media timestamps.
     *   The FreeRTOS default reads libc gettimeofday(). Replace it at startup
     *   if the platform needs another UTC source.
     * Both callbacks are mandatory and must be safe for concurrent tasks.
     */
    uint64_t (*time_ms)(void);
    uint64_t (*wal_clock_time)(void);

    /* --- Memory ----------------------------------------------------------
     * Standard malloc/free semantics.  malloc must return NULL on failure.
     */
    void *(*malloc)(size_t size);
    void  (*free)(void *ptr);

    /* --- Mutex -----------------------------------------------------------
     * mutex_create: allocate and initialise a RECURSIVE mutex (the same thread
     *   may lock it more than once); return handle. Recursion is REQUIRED, not
     *   optional: iot-client's DP schema-update path locks ctx->mutex and, while
     *   holding it, re-locks via nested helpers -- a plain mutex deadlocks there.
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

    /* --- Sleep -----------------------------------------------------------
     * Suspend the calling thread without busy-waiting for at least ms
     * milliseconds; 0 is a no-op. Scheduler delays and tick rounding may
     * extend the sleep. No socket or socket-readiness dependency.
     */
    void  (*sleep_ms)(uint32_t ms);

    /* --- Optional IPv4 server and datagram operations ---------------------
     * tcp_listen/udp_bind: bind a numeric IPv4 address (NULL = any address).
     *   Port 0 requests an ephemeral port. Use pal's allocator for the handle.
     *   Return 0 on success and set *out; errors leave *out NULL.
     * tcp_accept: one nonblocking accept attempt; peer is optional. Accepted
     *   connections use the SAME handle/operations as tcp_connect connections:
     *   tcp_send/recv/poll/close. timeout_ms=0 on those calls never waits.
     * udp_sendto/udp_recvfrom: one nonblocking datagram attempt. A receive of 0
     *   is a valid empty datagram. Oversized datagrams are consumed and fail,
     *   never returned truncated. udp_close releases a UDP handle.
     * socket_local_port: get a listening, accepted or UDP handle's bound port.
     * Errors use PAL_ERR_INVALID/NET/AGAIN; AGAIN also includes an interrupted
     *   nonblocking attempt. Never mix TCP and UDP handles or platform ports.
     * These callbacks may be NULL. LAN validates the complete group before use.
     * The built-in ports enable them with AGENTIC_KIT_ENABLE_LAN; conditional
     *   compilation must never change the pal_t member layout.
     */
    int (*tcp_listen)(const struct pal *pal, const char *ip, uint16_t port,
                      unsigned backlog, void **out);
    int (*tcp_accept)(void *listener, void **out, pal_addr_t *peer);
    int (*udp_bind)(const struct pal *pal, const char *ip, uint16_t port,
                    bool broadcast, void **out);
    int (*udp_sendto)(void *socket, const pal_addr_t *peer,
                      const uint8_t *data, size_t len);
    int (*udp_recvfrom)(void *socket, pal_addr_t *peer,
                        uint8_t *data, size_t capacity);
    int (*socket_local_port)(void *socket, uint16_t *port);
    void (*udp_close)(void *socket);

} pal_t;

/* -------------------------------------------------------------------------
 * Validate that a pal_t exposes every callback the SDK relies on.
 * Both ai-tcp and iot-client call this once at init and reject any pal
 * that returns false, so downstream code may dereference these pointers
 * without further null checks.
 *
 * This checks PRESENCE, never behaviour.  A port whose mutex_create()
 * returns a non-recursive mutex passes here, activates, reports DPs and
 * handles downlinks -- then hard-deadlocks the first time the cloud offers a
 * newer schema (see the mutex_create contract above).  No test covers it:
 * the only path that needs the recursion is iot-client's, and every pal in
 * the test suite is already recursive.
 * ------------------------------------------------------------------------- */
static inline bool pal_is_valid(const pal_t *p)
{
    return p && p->tcp_connect && p->tcp_send && p->tcp_recv && p->tcp_close
             && p->tcp_poll && p->time_ms && p->wal_clock_time
             && p->malloc && p->free
             && p->mutex_create && p->mutex_lock && p->mutex_unlock
             && p->mutex_destroy && p->thread_create && p->thread_join
             && p->sleep_ms;
}

/* Server/datagram capability is optional for IoT and RTC, required for LAN. */
static inline bool pal_has_server_network(const pal_t *p)
{
    return p && p->tcp_listen && p->tcp_accept && p->tcp_send && p->tcp_recv
             && p->tcp_close && p->tcp_poll && p->udp_bind && p->udp_sendto
             && p->udp_recvfrom && p->socket_local_port && p->udp_close;
}

#ifdef __cplusplus
}
#endif

#endif /* PAL_H */
