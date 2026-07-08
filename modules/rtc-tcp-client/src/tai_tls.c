/*
 * tai_tls.c -- Internal TLS layer using mbedTLS.
 *
 * Provides TLS client functionality on top of the PAL's raw TCP socket
 * callbacks.  The SDK owns the mbedTLS SSL context; users never touch TLS.
 */

#include "tai_internal.h"

#include "mbedtls/ssl.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/x509_crt.h"
#include "mbedtls/error.h"
#include "mbedtls/net_sockets.h"

#ifdef CONFIG_IDF_TARGET
#include "esp_crt_bundle.h"
#endif

#define TAG "tls"

/* =========================================================================
 * tai_tls_t -- internal TLS state
 * ========================================================================= */
struct tai_tls {
    mbedtls_ssl_context  ssl;
    mbedtls_ssl_config   conf;
    mbedtls_x509_crt     ca_chain;
    void                *tcp_handle;
    const pal_t     *pal;
    void                *yield_mutex;
};

/* =========================================================================
 * BIO callbacks -- adapt PAL tcp_send/recv to mbedTLS f_send/f_recv
 *
 * tcp_send returns: >0 bytes sent, 0 nothing sent, TAI_ERR_AGAIN would-block,
 *                   <0 other = error.
 * ========================================================================= */
static int tls_bio_send(void *ctx, const unsigned char *buf, size_t len)
{
    tai_tls_t *t = (tai_tls_t *)ctx;
    int rc = t->pal->tcp_send(t->tcp_handle, buf, len, 0);
    if (rc > 0) return rc;
    if (rc == 0 || rc == TAI_ERR_AGAIN) return MBEDTLS_ERR_SSL_WANT_WRITE;
    return MBEDTLS_ERR_NET_SEND_FAILED;
}

static int tls_bio_recv(void *ctx, unsigned char *buf, size_t len)
{
    tai_tls_t *t = (tai_tls_t *)ctx;
    int rc = t->pal->tcp_recv(t->tcp_handle, buf, len, 0);
    if (rc > 0) return rc;
    if (rc == 0) return 0;
    if (rc == TAI_ERR_AGAIN) return MBEDTLS_ERR_SSL_WANT_READ;
    return MBEDTLS_ERR_NET_RECV_FAILED;
}

static int tls_bio_recv_timeout(void *ctx, unsigned char *buf,
                                 size_t len, uint32_t timeout)
{
    tai_tls_t *t = (tai_tls_t *)ctx;
    int rc = t->pal->tcp_recv(t->tcp_handle, buf, len, timeout);
    if (rc > 0) return rc;
    if (rc == 0) return 0;
    if (rc == TAI_ERR_AGAIN) return MBEDTLS_ERR_SSL_WANT_READ;
    return MBEDTLS_ERR_NET_RECV_FAILED;
}

/* =========================================================================
 * tai_tls_connect
 * ========================================================================= */
tai_tls_t *tai_tls_connect(const char *host, uint16_t port,
                            const char *sni, const pal_t *pal)
{
    if (!host) return NULL;

    tai_tls_t *t = (tai_tls_t *)pal->malloc(sizeof(tai_tls_t));
    if (!t) return NULL;
    memset(t, 0, sizeof(*t));
    t->pal = pal;
    t->yield_mutex = pal->mutex_create();
    if (!t->yield_mutex) { pal->free(t); return NULL; }

    mbedtls_ssl_init(&t->ssl);
    mbedtls_ssl_config_init(&t->conf);
    mbedtls_x509_crt_init(&t->ca_chain);

    /* TCP connect */
    t->tcp_handle = pal->tcp_connect(host, port);
    if (!t->tcp_handle) goto fail;

    /* SSL config */
    if (mbedtls_ssl_config_defaults(&t->conf,
                                     MBEDTLS_SSL_IS_CLIENT,
                                     MBEDTLS_SSL_TRANSPORT_STREAM,
                                     MBEDTLS_SSL_PRESET_DEFAULT) != 0)
        goto fail;

    mbedtls_ctr_drbg_context *drbg = tai_crypto_get_drbg();
    if (!drbg) goto fail;
    mbedtls_ssl_conf_rng(&t->conf, mbedtls_ctr_drbg_random, drbg);

    /* Certificate verification */
#ifdef CONFIG_IDF_TARGET
    mbedtls_ssl_conf_authmode(&t->conf, MBEDTLS_SSL_VERIFY_REQUIRED);
    esp_crt_bundle_attach(&t->conf);
#else
    mbedtls_ssl_conf_authmode(&t->conf, MBEDTLS_SSL_VERIFY_OPTIONAL);
#endif

    if (mbedtls_ssl_setup(&t->ssl, &t->conf) != 0) goto fail;

    /* SNI */
    const char *hostname = sni ? sni : host;
    if (mbedtls_ssl_set_hostname(&t->ssl, hostname) != 0) goto fail;

    /* BIO */
    mbedtls_ssl_set_bio(&t->ssl, t,
                         tls_bio_send, tls_bio_recv, tls_bio_recv_timeout);

    /* Handshake. BIO is non-blocking (tcp_recv with timeout=0), so WANT_READ /
     * WANT_WRITE means "no data yet" — poll the socket before retrying instead
     * of busy-looping. */
    int ret;
    do {
        ret = mbedtls_ssl_handshake(&t->ssl);
        if (ret == MBEDTLS_ERR_SSL_WANT_READ) {
            pal->tcp_poll(t->tcp_handle, 1, 100);
        } else if (ret == MBEDTLS_ERR_SSL_WANT_WRITE) {
            pal->tcp_poll(t->tcp_handle, 2, 100);
        }
    } while (ret == MBEDTLS_ERR_SSL_WANT_READ ||
             ret == MBEDTLS_ERR_SSL_WANT_WRITE);
    if (ret != 0) {
        TAI_LOGE(pal, TAG, "TLS handshake failed: -0x%04X", (unsigned)-ret);
        goto fail;
    }

    return t;

fail:
    if (t->tcp_handle) pal->tcp_close(t->tcp_handle);
    mbedtls_ssl_free(&t->ssl);
    mbedtls_ssl_config_free(&t->conf);
    mbedtls_x509_crt_free(&t->ca_chain);
    pal->mutex_destroy(t->yield_mutex);
    pal->free(t);
    return NULL;
}

/* =========================================================================
 * tai_tls_send
 *
 * Self-locking, non-blocking-BIO write loop:
 *   - 3 second total timeout
 *   - Lock granularity = a single mbedtls_ssl_write call (cheap, returns
 *     quickly because BIO is non-blocking).
 *   - Lock is NOT held during tcp_poll wait, so the worker (or any other
 *     ssl_* caller) can interleave freely.
 *   - Caller does not need to hold any lock; the recursive yield_mutex
 *     makes nested external locking safe but unnecessary.
 * ========================================================================= */

int tai_tls_send(tai_tls_t *tls, const uint8_t *buf, size_t len)
{
    if (!tls) return TAI_ERR_ARGS;
    size_t written = 0;
    uint64_t start = tls->pal->time_ms();
    /* Whole-frame send deadline. Multimodal frames (a one-shot image can be
     * tens of KB) over a power-saving Wi-Fi link can stall for seconds while
     * the socket buffer drains; 3s was too tight and produced spurious
     * TAI_ERR_NET on large image frames. 15s only bites a genuinely stuck
     * link (small packets that succeed return immediately). */
    const uint64_t limit_ms = 15000;

    while (written < len) {
        tls->pal->mutex_lock(tls->yield_mutex);
        int n = mbedtls_ssl_write(&tls->ssl, buf + written, len - written);
        tls->pal->mutex_unlock(tls->yield_mutex);

        if (n > 0) {
            written += (size_t)n;
            continue;
        }
        if (n == MBEDTLS_ERR_SSL_WANT_WRITE || n == MBEDTLS_ERR_SSL_WANT_READ) {
            uint64_t elapsed = tls->pal->time_ms() - start;
            if (elapsed >= limit_ms) return TAI_ERR_NET;
            uint32_t remaining = (uint32_t)(limit_ms - elapsed);
            int ev = (n == MBEDTLS_ERR_SSL_WANT_READ) ? 1 : 2;
            tls->pal->tcp_poll(tls->tcp_handle, ev, remaining);
            continue;
        }
        return TAI_ERR_NET;
    }
    return TAI_OK;
}

/* =========================================================================
 * tai_tls_recv
 *
 * Self-locking, non-blocking-BIO read loop with caller-supplied timeout.
 *   - timeout_ms == 0  -> peek (single non-blocking attempt)
 *   - timeout_ms > 0   -> wait up to that many ms for at least 1 byte
 * ========================================================================= */
int tai_tls_recv(tai_tls_t *tls, uint8_t *buf, size_t buf_len,
                  uint32_t timeout_ms)
{
    if (!tls) return TAI_ERR_ARGS;
    uint64_t start = tls->pal->time_ms();

    while (1) {
        tls->pal->mutex_lock(tls->yield_mutex);
        int n = mbedtls_ssl_read(&tls->ssl, buf, buf_len);
        tls->pal->mutex_unlock(tls->yield_mutex);

        if (n > 0) return n;
        if (n == 0 || n == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) return 0;
        if (n != MBEDTLS_ERR_SSL_WANT_READ &&
            n != MBEDTLS_ERR_SSL_WANT_WRITE &&
            n != MBEDTLS_ERR_SSL_TIMEOUT) {
            return TAI_ERR_NET;
        }

        uint64_t elapsed = tls->pal->time_ms() - start;
        if (elapsed >= timeout_ms) return TAI_ERR_AGAIN;

        uint32_t remaining = timeout_ms - (uint32_t)elapsed;
        int ev = (n == MBEDTLS_ERR_SSL_WANT_WRITE) ? 2 : 1;
        tls->pal->tcp_poll(tls->tcp_handle, ev, remaining);
    }
}

/* =========================================================================
 * tai_tls_get_tcp_handle
 *
 * Returns the underlying raw TCP handle for socket-level polling.
 * Used by the worker thread to poll for readability without holding the
 * SSL mutex (safe because select/poll is read-only on the fd).
 * ========================================================================= */
void *tai_tls_get_tcp_handle(tai_tls_t *tls)
{
    return tls ? tls->tcp_handle : NULL;
}

/* =========================================================================
 * tai_tls_close
 * ========================================================================= */
void tai_tls_close(tai_tls_t *tls)
{
    if (!tls) return;
    mbedtls_ssl_close_notify(&tls->ssl);
    if (tls->tcp_handle)
        tls->pal->tcp_close(tls->tcp_handle);
    mbedtls_ssl_free(&tls->ssl);
    mbedtls_ssl_config_free(&tls->conf);
    mbedtls_x509_crt_free(&tls->ca_chain);
    tls->pal->mutex_destroy(tls->yield_mutex);
    tls->pal->free(tls);
}
