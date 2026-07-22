/*
 * rng.c -- Process-wide cryptographic RNG shared across the SDK.
 *
 * One mbedTLS CTR-DRBG, seeded from the platform entropy source. This
 * consolidates what used to be three separate entropy+CTR-DRBG instances
 * (common/tls.c, iot-client/cipher_wrapper.c, rtc-tcp-client/tai_crypto.c)
 * into a single ~740-byte singleton.
 *
 * Entropy comes from mbedtls_entropy_func, which abstracts the platform:
 * /dev/urandom on POSIX, the hardware RNG via mbedtls_hardware_poll on
 * ESP-IDF. If no strong entropy source exists, seeding fails and the RNG fails
 * closed rather than emitting predictable bytes.
 *
 * rng_init() must be called once at startup before any rng_bytes() call;
 * rng_bytes() fails (rather than lazily seeding) if the DRBG is not ready, so
 * seeding is always explicit and never races. Do the init single-threaded,
 * before spawning workers.
 *
 * Thread-safety of rng_bytes(): the DRBG is a single shared instance and
 * mbedtls_ctr_drbg_random() mutates it on every call (counter / key / reseed
 * state), so concurrent callers from unrelated subsystems (TLS handshakes,
 * AES-GCM nonces) must be serialized. rng_init() creates a mutex from the
 * (required) pal and rng_bytes() locks it via the pal it is passed -- staying
 * within the SDK's platform abstraction rather than calling an OS threading API
 * from common/. Callers pass their own pal (the one process-wide platform
 * adapter), which owns the mutex.
 */

#include "rng.h"
#include "log.h"
#include "pal.h"

#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"

static mbedtls_entropy_context  g_entropy;
static mbedtls_ctr_drbg_context g_drbg;
/* volatile so the "seeded" flag set by rng_init() is re-read (not cached) by
 * rng_bytes() callers on other threads. */
static volatile int             g_ready = 0;

/* Mutex serializing rng_bytes(), created in rng_init() from its pal. Non-NULL
 * once rng_init() has succeeded, so rng_bytes() can lock unconditionally. */
static void *g_rng_mutex = NULL;

int rng_init(const pal_t *pal)
{
    if (g_ready) {
        return 0;
    }
    if (!pal || !pal->mutex_create) {
        log_emit(LOG_ERROR, "[rng] rng_init requires a pal with mutex support");
        return -1;
    }
    mbedtls_entropy_init(&g_entropy);
    mbedtls_ctr_drbg_init(&g_drbg);
    static const unsigned char pers[] = "agentic_kit_rng";
    int rc = mbedtls_ctr_drbg_seed(&g_drbg, mbedtls_entropy_func, &g_entropy,
                                   pers, sizeof(pers) - 1);
    if (rc != 0) {
        log_emit(LOG_ERROR, "[rng] DRBG seed failed: -0x%04x", (unsigned) -rc);
        return -1;
    }
    g_rng_mutex = pal->mutex_create();
    if (!g_rng_mutex) {
        log_emit(LOG_ERROR, "[rng] mutex_create failed");
        return -1;
    }
    g_ready = 1;
    return 0;
}

int rng_bytes(const pal_t *pal, uint8_t *buf, size_t len)
{
    /* pal is required: it locks the shared DRBG mutex below. Reject NULL up
     * front rather than dereferencing it. */
    if (!pal || !buf) {
        return -1;
    }
    if (len == 0) {
        return 0;
    }
    if (!g_ready) {
        log_emit(LOG_ERROR, "[rng] rng_bytes() called before rng_init()");
        return -1;
    }
    /* g_rng_mutex is always set once g_ready is true; the caller's pal (the one
     * process pal that created it) locks it. */
    pal->mutex_lock(g_rng_mutex);
    int rc = mbedtls_ctr_drbg_random(&g_drbg, buf, len);
    pal->mutex_unlock(g_rng_mutex);
    if (rc != 0) {
        log_emit(LOG_ERROR, "[rng] DRBG random failed: -0x%04x", (unsigned) -rc);
    }
    return rc == 0 ? 0 : -1;
}
