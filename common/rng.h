/*
 * rng.h -- Process-wide cryptographic RNG for agentic-kit.
 *
 * A single source of random bytes shared by every SDK component that needs
 * them: the TLS handshake, AES-GCM IVs (cipher_wrapper), and key-derivation
 * nonces (tai_crypto). Backed by one mbedTLS CTR-DRBG.
 *
 * Lifecycle: rng_init() must be called once at startup (single-threaded,
 * before any worker threads) before any rng_bytes() call. rng_bytes() does NOT
 * seed lazily -- it fails if rng_init() has not succeeded -- so seeding is
 * always explicit and never races. Concurrent rng_bytes() afterwards is
 * serialized by a mutex created from the (required) pal passed to rng_init(),
 * so the single shared DRBG is safe to call from any thread.
 */

#ifndef COMMON_RNG_H
#define COMMON_RNG_H

#include <stdint.h>
#include <stddef.h>

#include "pal.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Seed the shared DRBG. Must be called once during startup (single-threaded)
 * before any rng_bytes() call. Idempotent: a second call is a no-op once
 * seeded (the first caller's pal owns the rng mutex).
 *
 * @param pal  platform adapter (required, with mutex_* hooks); a mutex created
 *             from it serializes concurrent rng_bytes() calls.
 * @return 0 on success, non-zero if pal is unusable or the DRBG could not be
 *         seeded (e.g. no strong entropy source).
 */
int rng_init(const pal_t *pal);

/*
 * Fill buf[0..len) with cryptographically secure random bytes from the shared
 * DRBG. Requires a prior successful rng_init(); fails (does not seed lazily)
 * otherwise.
 *
 * @param pal  platform adapter used to lock the shared DRBG; pass the same
 *             process pal given to rng_init() (must be non-NULL once the RNG is
 *             initialized).
 * @return 0 on success; non-zero on failure (not initialized, or DRBG error).
 */
int rng_bytes(const pal_t *pal, uint8_t *buf, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* COMMON_RNG_H */
