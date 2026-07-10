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
 *
 * mbedTLS configuration is the integrator's responsibility.  This SDK does NOT
 * touch mbedTLS's process-global configuration -- the allocator
 * (mbedtls_platform_set_calloc_free), threading callbacks
 * (mbedtls_threading_set_alt), and record-buffer sizes
 * (MBEDTLS_SSL_{IN,OUT}_CONTENT_LEN) are owned by the application, because the
 * host may share the same mbedTLS instance and last-writer-wins on those
 * globals would corrupt its state.  To route mbedTLS through your own heap
 * (e.g. the same allocator the SDK's pal uses, to keep one coalescing heap),
 * call mbedtls_platform_set_calloc_free() yourself at startup -- once,
 * single-threaded, before any SDK init -- and build mbedTLS with
 * MBEDTLS_PLATFORM_MEMORY.
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

/* Peer-certificate verification mode used when no CA cert is supplied and no
 * cert-bundle callback is set. */
typedef enum {
    TLS_VERIFY_NONE = 0,   /* do not verify the peer certificate          */
    TLS_VERIFY_OPTIONAL,   /* verify but continue the handshake on failure */
    TLS_VERIFY_REQUIRED,   /* fail the handshake on verification error     */
} tls_verify_t;

/* Platform-supplied callback that attaches a trusted-cert bundle to the
 * mbedTLS SSL config.  The argument is a mbedtls_ssl_config* passed as void*
 * so the public header need not include mbedTLS.  When non-NULL, the TLS
 * layer sets VERIFY_REQUIRED and invokes this during setup.
 *
 * Example (ESP-IDF):  .cert_bundle_attach = (tls_cert_bundle_attach_fn)esp_crt_bundle_attach
 */
typedef void (*tls_cert_bundle_attach_fn)(void *ssl_config);

/* Connection configuration.  Borrowed pointers must outlive tls_connect(). */
typedef struct {
    const char  *host;            /* required: server hostname / IP            */
    uint16_t     port;            /* required: server port                     */
    const char  *sni;             /* SNI hostname; NULL -> use host            */
    const char  *cacert;          /* CA cert: full PEM or bare base64 body;
                                     NULL -> none (see verify / use_cert_bundle) */
    tls_verify_t verify;          /* applied only when cacert == NULL and
                                     cert_bundle_attach is NULL                */
    tls_cert_bundle_attach_fn cert_bundle_attach; /* platform cert-bundle attach
                                     callback; when set, verification is
                                     REQUIRED and this is called during setup.
                                     NULL -> use verify / cacert path.         */
    bool         force_tls12;     /* pin min == max == TLS 1.2                 */
    const int   *ciphersuites;    /* 0-terminated mbedTLS suite ids; NULL ->
                                     mbedTLS defaults                          */
    uint32_t     connect_timeout_ms;   /* single deadline bounding the whole
                                     connection establishment -- the TCP connect
                                     AND the TLS handshake share this one budget,
                                     so tls_connect returns within ~this value no
                                     matter which phase stalls. A peer that
                                     completes TCP connect but stalls the handshake
                                     can't hang the caller. 0 -> TLS_DEFAULT_CONNECT_TIMEOUT_MS. */
    const pal_t *pal;             /* required: platform abstraction layer      */
} tls_config_t;

/* Default overall connection-establishment (TCP connect + TLS handshake) deadline
 * when cfg->connect_timeout_ms == 0. */
#define TLS_DEFAULT_CONNECT_TIMEOUT_MS 10000U

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
 * Both phases share one deadline, cfg->connect_timeout_ms (0 -> default): the TLS
 * connect fails once that single establishment budget elapses, so a stalled peer
 * cannot hang the caller indefinitely.
 * Returns a handle on success, NULL on any failure (TCP, RNG, cert, handshake,
 * establishment timeout). */
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
