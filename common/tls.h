/*
 * tls.h -- Shared TLS-over-TCP transport for agentic-kit.
 *
 * A thin client-side TLS layer on top of the PAL's raw TCP socket callbacks,
 * backed by mbedTLS.  One implementation, consumed by both iot-client (MQTT /
 * HTTPS, driven by coreMQTT/coreHTTP transport interfaces) and rtc-tcp-client
 * (its own non-blocking send/recv loops).  Callers never touch mbedTLS.
 *
 * I/O model.  The underlying BIO is always non-blocking (PAL tcp_send/tcp_recv
 * with timeout 0); the tls_write/tls_read loops poll via pal->tcp_poll:
 *
 *   tls_write  -- blocking "write all" within timeout_ms.
 *   tls_read   -- blocking read up to timeout_ms (0 = single peek).
 *
 * Thread-safety.  Each tls_t serialises its mbedTLS read/write with an internal
 * recursive mutex, releasing it during tcp_poll waits, so a worker thread and a
 * sender thread may interleave freely (the rtc model).  The handshake DRBG is a
 * process-wide, lazily-seeded singleton shared by all connections.
 */

#ifndef COMMON_TLS_H
#define COMMON_TLS_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "pal.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Peer-certificate verification mode used when no CA cert (and no ESP cert
 * bundle) is supplied. */
typedef enum {
    TLS_VERIFY_NONE = 0,   /* do not verify the peer certificate          */
    TLS_VERIFY_OPTIONAL,   /* verify but continue the handshake on failure */
    TLS_VERIFY_REQUIRED,   /* fail the handshake on verification error     */
} tls_verify_t;

/* Connection configuration.  Borrowed pointers must outlive tls_connect(). */
typedef struct {
    const char  *host;            /* required: server hostname / IP            */
    uint16_t     port;            /* required: server port                     */
    const char  *sni;             /* SNI hostname; NULL -> use host            */
    const char  *cacert;          /* CA cert: full PEM or bare base64 body;
                                     NULL -> none (see verify / use_cert_bundle) */
    tls_verify_t verify;          /* applied only when cacert == NULL and no
                                     cert bundle is attached                   */
    bool         use_cert_bundle; /* ESP-IDF only: attach esp_crt_bundle and
                                     require verification (ignored elsewhere)  */
    bool         force_tls12;     /* pin min == max == TLS 1.2                 */
    const int   *ciphersuites;    /* 0-terminated mbedTLS suite ids; NULL ->
                                     mbedTLS defaults                          */
    uint32_t     handshake_timeout_ms; /* overall TLS-handshake deadline; the
                                     handshake fails (tls_connect returns NULL)
                                     once it elapses, so a peer that completes
                                     TCP connect but stalls the handshake can't
                                     hang the caller. 0 -> TLS_DEFAULT_HANDSHAKE_TIMEOUT_MS. */
    const pal_t *pal;             /* required: platform abstraction layer      */
} tls_config_t;

/* Default overall handshake deadline when cfg->handshake_timeout_ms == 0. */
#define TLS_DEFAULT_HANDSHAKE_TIMEOUT_MS 10000U

/* Return codes.  Aligned with PAL_ERR_* so adapters may forward directly. */
#define TLS_OK         0
#define TLS_ERR_NET    PAL_ERR_NET    /* fatal network / TLS error (-3) */
#define TLS_ERR_AGAIN  PAL_ERR_AGAIN  /* would block / timed out    (-7) */
#define TLS_ERR_ARGS   (-1)           /* invalid argument               */

/* Opaque TLS connection handle. */
typedef struct tls_conn tls_t;

/* Convenience: a 0-terminated mbedTLS suite list of ECDHE-ECDSA / ECDHE-RSA
 * with AES-128-GCM-SHA256 (what Tuya cloud endpoints expect).  Assign to
 * tls_config_t.ciphersuites and pair with force_tls12 = true.  Lets callers
 * select this profile without including any mbedTLS header. */
const int *tls_ciphersuites_tuya_default(void);

/* Open a TCP connection to cfg->host:cfg->port and perform the TLS handshake.
 * The handshake is bounded by cfg->handshake_timeout_ms (0 -> default); it fails
 * once that elapses so a stalled peer cannot hang the caller indefinitely.
 * Returns a handle on success, NULL on any failure (TCP, RNG, cert, handshake,
 * handshake timeout). */
tls_t *tls_connect(const tls_config_t *cfg);

/* Write all len bytes, blocking at most timeout_ms in total; polls between
 * attempts.  Returns: TLS_OK on success, TLS_ERR_NET on error/timeout,
 * TLS_ERR_ARGS if t is NULL. */
int tls_write(tls_t *t, const uint8_t *buf, size_t len, uint32_t timeout_ms);

/* Read up to len bytes, blocking at most timeout_ms (0 = single peek).
 * Returns: >0 bytes read, 0 on peer close, TLS_ERR_AGAIN on timeout with no
 * data, TLS_ERR_NET on error, TLS_ERR_ARGS if t is NULL. */
int tls_read(tls_t *t, uint8_t *buf, size_t len, uint32_t timeout_ms);

/* Underlying raw TCP handle, for socket-level polling without holding the SSL
 * mutex (read-only select/poll on the fd is safe).  NULL if t is NULL. */
void *tls_get_tcp_handle(tls_t *t);

/* Send close_notify, close the socket, and free all resources.  NULL-safe. */
void tls_close(tls_t *t);

#ifdef __cplusplus
}
#endif

#endif /* COMMON_TLS_H */
