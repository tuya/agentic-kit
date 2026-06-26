/*
 * demo_reconnect.h — shared app-side reconnect helper for the rtc-tcp-client
 * POSIX demos (header-only).
 *
 * The tuya_ai receive callbacks (on_audio/on_text/on_event/on_disconnect) run on
 * the SDK worker thread and MUST NOT call tai_disconnect()/tai_connect(): those
 * join the worker thread and would self-deadlock. So on_disconnect() only
 * records the cause via demo_reconnect_signal(); the OWNING thread (the demo's
 * main loop) drives the actual tai_disconnect()+tai_connect().
 *
 * Because the SDK is fail-fast (a protocol/transport error tears the connection
 * down immediately), a naive "reconnect on drop" loop would hammer the server.
 * The correct posture — which every long-lived app should copy — is exponential
 * backoff + a ceiling + a circuit breaker:
 *
 *     demo_reconnect_t rc = {0};                  // 0 => sensible defaults
 *     ...                                          // on_disconnect: demo_reconnect_signal(&rc, msg->reason, msg->close_code)
 *     for (;;) {
 *         if (tai_connect(ctx) != TAI_OK) {        // (re)connect failed
 *             if (demo_reconnect_tripped(&rc)) break;          // give up
 *             usleep(demo_reconnect_delay_ms(&rc) * 1000);
 *             rc.attempt++;
 *             rc.need_reconnect = 0;
 *             continue;
 *         }
 *         demo_reconnect_ok(&rc);                  // connected: reset counters
 *         ... do work; wait until done || rc.need_reconnect ...
 *         if (!rc.need_reconnect) break;           // finished cleanly
 *         tai_disconnect(ctx);                     // owning thread: safe (joins worker)
 *         if (demo_reconnect_tripped(&rc)) break;  // circuit breaker
 *         usleep(demo_reconnect_delay_ms(&rc) * 1000);
 *         rc.attempt++;
 *         rc.need_reconnect = 0;                   // retry the op on the new connection
 *     }
 */
#ifndef DEMO_RECONNECT_H
#define DEMO_RECONNECT_H

#include <stdint.h>
#include "tuya_ai.h"

typedef struct {
    volatile int need_reconnect; /* set by on_disconnect (worker thread)        */
    uint8_t      reason;         /* TAI_DISCONNECT_* that triggered it          */
    uint16_t     close_code;     /* server close code, if any                   */
    int          attempt;        /* consecutive failed (re)connect attempts     */
    int          max_attempts;   /* circuit breaker (<= 0 => default 6)         */
    uint32_t     base_ms;        /* first backoff step (0 => default 500 ms)    */
    uint32_t     cap_ms;         /* backoff ceiling    (0 => default 30000 ms)  */
} demo_reconnect_t;

/* Call from on_disconnect(): record the cause and request a reconnect. Never
 * call tai_disconnect()/tai_connect() from the callback itself. */
static inline void demo_reconnect_signal(demo_reconnect_t *r,
                                         uint8_t reason, uint16_t close_code)
{
    r->reason     = reason;
    r->close_code = close_code;
    r->need_reconnect = 1;
}

/* Backoff for the current attempt: base * 2^attempt, clamped to cap_ms. */
static inline uint32_t demo_reconnect_delay_ms(const demo_reconnect_t *r)
{
    uint32_t base = r->base_ms ? r->base_ms : 500u;
    uint32_t cap  = r->cap_ms  ? r->cap_ms  : 30000u;
    uint32_t d = base;
    for (int i = 0; i < r->attempt && d < cap; i++) {
        d <<= 1;
        if (d > cap) { d = cap; break; }
    }
    return d;
}

/* Circuit breaker: stop retrying after max_attempts consecutive failures. */
static inline int demo_reconnect_tripped(const demo_reconnect_t *r)
{
    int max = r->max_attempts > 0 ? r->max_attempts : 6;
    return r->attempt >= max;
}

/* Clear counters after a successful (re)connect. */
static inline void demo_reconnect_ok(demo_reconnect_t *r)
{
    r->attempt = 0;
    r->need_reconnect = 0;
}

/* SESSION_CLOSE (connection_alive=1) is not a transport fault — the link is
 * still up and only the session ended, so an app may rebuild the session
 * without backoff. Everything else (PROTOCOL / TRANSPORT / CONNECTION_CLOSE) is
 * a fault that warrants the backoff + circuit breaker. */
static inline int demo_reconnect_is_fault(const demo_reconnect_t *r)
{
    return r->reason != TAI_DISCONNECT_SESSION_CLOSE;
}

#endif /* DEMO_RECONNECT_H */
