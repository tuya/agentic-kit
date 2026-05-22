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

#ifdef __cplusplus
}
#endif

#endif /* TAI_PAL_LOOPBACK_H */
