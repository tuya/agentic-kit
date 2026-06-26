/*
 * tests/tai_pal_loopback.h -- In-memory PAL for deterministic integration tests.
 *
 * Replaces the TLS layer with a pair of byte FIFOs so tests can drive the
 * library without any network. Crypto uses OpenSSL directly (same as the
 * openssl PAL), time is a controllable virtual clock, and randomness is
 * seeded for reproducibility.
 *
 *   test                                             library
 *   ----                                             -------
 *   tai_loopback_push_recv(server_bytes) --------->  tls_recv
 *   tai_loopback_pop_sent(&captured)     <---------  tls_send
 *
 * The worker thread (tai_connect) is still a real pthread, so tests must
 * give it a short window to process incoming bytes before asserting.
 */

#ifndef TAI_PAL_LOOPBACK_H
#define TAI_PAL_LOOPBACK_H

#include <stddef.h>
#include <stdint.h>

#include "pal.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Factory: returns the PAL function table. */
const pal_t *tai_pal_loopback(void);

/* Reset all state (FIFOs, clock, random seed). Call before each test. */
void tai_loopback_reset(void);

/* Seed random_bytes output so encrypt_random and generated IDs are
 * reproducible. Defaults to seed=1 after reset. */
void tai_loopback_seed_random(uint64_t seed);

/* Virtual clock. Defaults to 1700000000000 ms after reset. */
uint64_t tai_loopback_now_ms(void);
void     tai_loopback_advance_time(uint64_t delta_ms);

/* Enqueue bytes that the client will read via tls_recv. */
void tai_loopback_push_recv(const uint8_t *data, size_t len);

/* Drain bytes the client has written via tls_send.
 * Copies up to cap bytes into buf, returns bytes copied, and removes them
 * from the internal buffer. */
size_t tai_loopback_pop_sent(uint8_t *buf, size_t cap);

/* Total bytes currently buffered on the sent queue (not drained yet). */
size_t tai_loopback_peek_sent_len(void);

/* Make the next tls_recv return 0 (EOF), simulating server closing the
 * connection. Useful for testing disconnect callbacks. */
void tai_loopback_close_connection(void);

/* --- Handshake mock (for confirmed-connect, §3.5) ------------------------
 * The loopback completes the SDK handshake: on the client's ClientHello it
 * derives the sign_key from the local_key and pushes back a signed SessionNew
 * ack, so tai_connect's confirmed wait succeeds. Set the local_key once per
 * test (must match cfg.local_key) and optionally force a timeout / rejection. */
#define TAI_LB_HS_ACK_OK   0   /* push a SessionNew ack with status OK (default) */
#define TAI_LB_HS_NO_ACK   1   /* push nothing -> tai_connect times out          */
#define TAI_LB_HS_REJECT   2   /* push a SessionNew ack with a non-OK status      */
#define TAI_LB_HS_SESSION_CLOSE 3 /* push a signed SESSION_CLOSE instead of the ack:
                                   * server closes the session mid-handshake. The
                                   * SDK must NOT fire on_disconnect on the connect
                                   * thread; tai_connect just fails (times out).   */
#define TAI_LB_HS_AUTH_OK  4   /* confirm via an AuthenticateResponse (pkt 3) with
                                * connection-status-code 200, the way the production
                                * server does, instead of a SessionNew ack.        */

void tai_loopback_set_local_key(const char *local_key);
void tai_loopback_set_handshake_mode(int mode);

/* Effective cap on a single blocking tcp_recv (default 50 ms). Raise it to make
 * the loopback honour the full requested timeout like a real PAL, so a test can
 * verify the SDK-level worker poll cap bounds tai_disconnect latency. */
void tai_loopback_set_recv_cap_ms(uint32_t cap_ms);

/* Arm a send failure: the first `ok_calls` tcp_send calls succeed, then the
 * next fails (ok_calls<0 disarms). Exercises §6.3 — ok_calls=0 fails the first
 * write (control frame / SG header), ok_calls=1 fails the SG payload write
 * (mid-frame desync). The failed send returns TAI_ERR_NET to its caller; an
 * app-thread send leaves teardown to the app, while a failed worker Ping makes
 * the worker disconnect with TRANSPORT/NET_ERROR. */
void tai_loopback_fail_send_after(int ok_calls);

#ifdef __cplusplus
}
#endif

#endif /* TAI_PAL_LOOPBACK_H */
