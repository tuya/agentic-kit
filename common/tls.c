/*
 * tls.c -- Shared TLS-over-TCP transport, backed by mbedTLS.
 *
 * Generalised from rtc-tcp-client's tai_tls.c and iot-client's inline TLS in
 * mqtt.c / http_client_interface.c.  See tls.h for the contract and I/O model.
 */

#include "tls.h"
#include "log.h"

#include <string.h>
#include <stdio.h>

#include "mbedtls/ssl.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/entropy.h"
#include "mbedtls/x509_crt.h"
#include "mbedtls/error.h"
#include "mbedtls/net_sockets.h"
#include "mbedtls/version.h"

#ifdef CONFIG_IDF_TARGET
#include "esp_crt_bundle.h"
#endif

/* =========================================================================
 * tls_t -- per-connection state
 * ========================================================================= */
struct tls_conn {
    mbedtls_ssl_context  ssl;
    mbedtls_ssl_config   conf;
    mbedtls_x509_crt     ca_chain;
    void                *tcp_handle;
    const pal_t         *pal;
    void                *yield_mutex;  /* serialises ssl_read/ssl_write       */
};

/* =========================================================================
 * Process-wide DRBG -- lazily seeded, shared by every connection's handshake.
 *
 * The guard mutex is created on first use.  Both current consumers issue their
 * first tls_connect() before spawning the threads that later share a
 * connection, so the one-time guard creation itself is never contended.
 * ========================================================================= */
static mbedtls_entropy_context  g_entropy;
static mbedtls_ctr_drbg_context g_drbg;
static int                      g_drbg_ready = 0;
static void                    *g_drbg_lock  = NULL;

static mbedtls_ctr_drbg_context *tls_drbg(const pal_t *pal)
{
    if (!g_drbg_lock) g_drbg_lock = pal->mutex_create();
    if (g_drbg_lock) pal->mutex_lock(g_drbg_lock);
    if (!g_drbg_ready) {
        mbedtls_entropy_init(&g_entropy);
        mbedtls_ctr_drbg_init(&g_drbg);
        static const unsigned char pers[] = "agentic_kit_tls";
        if (mbedtls_ctr_drbg_seed(&g_drbg, mbedtls_entropy_func, &g_entropy,
                                  pers, sizeof(pers) - 1) == 0) {
            g_drbg_ready = 1;
        }
    }
    if (g_drbg_lock) pal->mutex_unlock(g_drbg_lock);
    return g_drbg_ready ? &g_drbg : NULL;
}

const int *tls_ciphersuites_tuya_default(void)
{
    static const int suites[] = {
        MBEDTLS_TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256,
        MBEDTLS_TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256,
        0
    };
    return suites;
}

static int tls_map_verify(tls_verify_t v)
{
    switch (v) {
    case TLS_VERIFY_REQUIRED: return MBEDTLS_SSL_VERIFY_REQUIRED;
    case TLS_VERIFY_OPTIONAL: return MBEDTLS_SSL_VERIFY_OPTIONAL;
    default:                  return MBEDTLS_SSL_VERIFY_NONE;
    }
}

/* =========================================================================
 * BIO callbacks -- adapt PAL tcp_send/recv to mbedTLS f_send/f_recv.
 *
 * Always non-blocking (timeout 0); the tls_write/tls_read loops poll between
 * attempts.  tcp_send/tcp_recv return >0 bytes, 0 nothing, PAL_ERR_AGAIN
 * would-block, <0 fatal.
 * ========================================================================= */
static int tls_bio_send(void *ctx, const unsigned char *buf, size_t len)
{
    tls_t *t = (tls_t *)ctx;
    int rc = t->pal->tcp_send(t->tcp_handle, buf, len, 0);
    if (rc > 0) return rc;
    if (rc == 0 || rc == PAL_ERR_AGAIN) return MBEDTLS_ERR_SSL_WANT_WRITE;
    return MBEDTLS_ERR_NET_SEND_FAILED;
}

static int tls_bio_recv(void *ctx, unsigned char *buf, size_t len)
{
    tls_t *t = (tls_t *)ctx;
    int rc = t->pal->tcp_recv(t->tcp_handle, buf, len, 0);
    if (rc > 0) return rc;
    if (rc == 0) return 0;
    if (rc == PAL_ERR_AGAIN) return MBEDTLS_ERR_SSL_WANT_READ;
    return MBEDTLS_ERR_NET_RECV_FAILED;
}

static int tls_bio_recv_timeout(void *ctx, unsigned char *buf,
                                size_t len, uint32_t timeout)
{
    tls_t *t = (tls_t *)ctx;
    int rc = t->pal->tcp_recv(t->tcp_handle, buf, len, timeout);
    if (rc > 0) return rc;
    if (rc == 0) return 0;
    if (rc == PAL_ERR_AGAIN) return MBEDTLS_ERR_SSL_WANT_READ;
    return MBEDTLS_ERR_NET_RECV_FAILED;
}

/* =========================================================================
 * tls_connect
 * ========================================================================= */
tls_t *tls_connect(const tls_config_t *cfg)
{
    if (!cfg || !cfg->host || !cfg->pal) return NULL;
    const pal_t *pal = cfg->pal;

    tls_t *t = (tls_t *)pal->malloc(sizeof(tls_t));
    if (!t) return NULL;
    memset(t, 0, sizeof(*t));
    t->pal = pal;
    t->yield_mutex = pal->mutex_create();
    if (!t->yield_mutex) { pal->free(t); return NULL; }

    mbedtls_ssl_init(&t->ssl);
    mbedtls_ssl_config_init(&t->conf);
    mbedtls_x509_crt_init(&t->ca_chain);

    bool has_ca = (cfg->cacert && cfg->cacert[0] != '\0');

    /* Parse the CA certificate (full PEM, or wrap a bare base64 body). */
    if (has_ca) {
        int pret;
        if (strstr(cfg->cacert, "-----BEGIN CERTIFICATE-----") != NULL) {
            pret = mbedtls_x509_crt_parse(&t->ca_chain,
                                          (const unsigned char *)cfg->cacert,
                                          strlen(cfg->cacert) + 1);
        } else {
            size_t pem_len = strlen(cfg->cacert) + 64 + 2;
            char *pem = (char *)pal->malloc(pem_len);
            if (!pem) goto fail;
            memset(pem, 0, pem_len);
            snprintf(pem, pem_len,
                     "-----BEGIN CERTIFICATE-----\n%s\n-----END CERTIFICATE-----\n",
                     cfg->cacert);
            pret = mbedtls_x509_crt_parse(&t->ca_chain,
                                          (const unsigned char *)pem,
                                          strlen(pem) + 1);
            pal->free(pem);
        }
        if (pret != 0) {
            log_emit(LOG_ERROR, "[tls] failed to parse CA certificate: -0x%04X",
                     (unsigned)-pret);
            goto fail;
        }
    }

    t->tcp_handle = pal->tcp_connect(cfg->host, cfg->port);
    if (!t->tcp_handle) {
        log_emit(LOG_ERROR, "[tls] TCP connect failed to %s:%u",
                 cfg->host, (unsigned)cfg->port);
        goto fail;
    }

    if (mbedtls_ssl_config_defaults(&t->conf, MBEDTLS_SSL_IS_CLIENT,
                                    MBEDTLS_SSL_TRANSPORT_STREAM,
                                    MBEDTLS_SSL_PRESET_DEFAULT) != 0)
        goto fail;

    mbedtls_ctr_drbg_context *drbg = tls_drbg(pal);
    if (!drbg) { log_emit(LOG_ERROR, "[tls] DRBG seed failed"); goto fail; }
    mbedtls_ssl_conf_rng(&t->conf, mbedtls_ctr_drbg_random, drbg);

    if (cfg->force_tls12) {
#if MBEDTLS_VERSION_MAJOR >= 3
        mbedtls_ssl_conf_min_tls_version(&t->conf, MBEDTLS_SSL_VERSION_TLS1_2);
        mbedtls_ssl_conf_max_tls_version(&t->conf, MBEDTLS_SSL_VERSION_TLS1_2);
#else
        mbedtls_ssl_conf_min_version(&t->conf, MBEDTLS_SSL_MAJOR_VERSION_3,
                                     MBEDTLS_SSL_MINOR_VERSION_3);
        mbedtls_ssl_conf_max_version(&t->conf, MBEDTLS_SSL_MAJOR_VERSION_3,
                                     MBEDTLS_SSL_MINOR_VERSION_3);
#endif
    }
    if (cfg->ciphersuites)
        mbedtls_ssl_conf_ciphersuites(&t->conf, cfg->ciphersuites);

    /* Peer-certificate verification policy. */
    if (has_ca) {
        mbedtls_ssl_conf_authmode(&t->conf, MBEDTLS_SSL_VERIFY_REQUIRED);
        mbedtls_ssl_conf_ca_chain(&t->conf, &t->ca_chain, NULL);
    } else {
#ifdef CONFIG_IDF_TARGET
        if (cfg->use_cert_bundle) {
            mbedtls_ssl_conf_authmode(&t->conf, MBEDTLS_SSL_VERIFY_REQUIRED);
            esp_crt_bundle_attach(&t->conf);
        } else {
            mbedtls_ssl_conf_authmode(&t->conf, tls_map_verify(cfg->verify));
        }
#else
        mbedtls_ssl_conf_authmode(&t->conf, tls_map_verify(cfg->verify));
        if (cfg->verify == TLS_VERIFY_NONE)
            log_emit(LOG_WARN,
                     "[tls] peer verification disabled (no CA certificate)");
#endif
    }

    if (mbedtls_ssl_setup(&t->ssl, &t->conf) != 0) goto fail;

    if (mbedtls_ssl_set_hostname(&t->ssl, cfg->sni ? cfg->sni : cfg->host) != 0)
        goto fail;

    mbedtls_ssl_set_bio(&t->ssl, t,
                        tls_bio_send, tls_bio_recv, tls_bio_recv_timeout);

    /* Handshake.  BIO is non-blocking, so WANT_READ/WANT_WRITE means "no data
     * yet" -- poll the socket before retrying instead of busy-looping. */
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
        log_emit(LOG_ERROR, "[tls] handshake failed to %s:%u: -0x%04X",
                 cfg->host, (unsigned)cfg->port, (unsigned)-ret);
        goto fail;
    }

    /* VERIFY_REQUIRED already failed the handshake on a bad chain; surface the
     * result on the optional path for diagnostics. */
    if (!has_ca && cfg->verify == TLS_VERIFY_OPTIONAL) {
        uint32_t flags = mbedtls_ssl_get_verify_result(&t->ssl);
        if (flags != 0)
            log_emit(LOG_WARN, "[tls] peer certificate not verified (0x%08X)",
                     (unsigned)flags);
    }

    log_emit(LOG_INFO, "[tls] connected to %s:%u (%s, %s)",
             cfg->host, (unsigned)cfg->port,
             mbedtls_ssl_get_version(&t->ssl),
             mbedtls_ssl_get_ciphersuite(&t->ssl));
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
 * tls_write -- write ALL bytes within timeout_ms, polling between writes
 * ========================================================================= */
int tls_write(tls_t *t, const uint8_t *buf, size_t len, uint32_t timeout_ms)
{
    if (!t) return TLS_ERR_ARGS;
    size_t written = 0;
    uint64_t start = t->pal->time_ms();

    while (written < len) {
        t->pal->mutex_lock(t->yield_mutex);
        int n = mbedtls_ssl_write(&t->ssl, buf + written, len - written);
        t->pal->mutex_unlock(t->yield_mutex);

        if (n > 0) { written += (size_t)n; continue; }
        if (n == MBEDTLS_ERR_SSL_WANT_WRITE || n == MBEDTLS_ERR_SSL_WANT_READ) {
            uint64_t elapsed = t->pal->time_ms() - start;
            if (elapsed >= timeout_ms) return TLS_ERR_NET;
            uint32_t remaining = (uint32_t)(timeout_ms - elapsed);
            int ev = (n == MBEDTLS_ERR_SSL_WANT_READ) ? 1 : 2;
            t->pal->tcp_poll(t->tcp_handle, ev, remaining);
            continue;
        }
        return TLS_ERR_NET;
    }
    return TLS_OK;
}

/* =========================================================================
 * tls_read -- read up to len bytes, blocking at most timeout_ms (0 = peek)
 * ========================================================================= */
int tls_read(tls_t *t, uint8_t *buf, size_t len, uint32_t timeout_ms)
{
    if (!t) return TLS_ERR_ARGS;
    uint64_t start = t->pal->time_ms();

    for (;;) {
        t->pal->mutex_lock(t->yield_mutex);
        int n = mbedtls_ssl_read(&t->ssl, buf, len);
        t->pal->mutex_unlock(t->yield_mutex);

        if (n > 0) return n;
        if (n == 0 || n == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) return 0;
        if (n != MBEDTLS_ERR_SSL_WANT_READ &&
            n != MBEDTLS_ERR_SSL_WANT_WRITE &&
            n != MBEDTLS_ERR_SSL_TIMEOUT) {
            return TLS_ERR_NET;
        }

        uint64_t elapsed = t->pal->time_ms() - start;
        if (elapsed >= timeout_ms) return TLS_ERR_AGAIN;

        uint32_t remaining = timeout_ms - (uint32_t)elapsed;
        int ev = (n == MBEDTLS_ERR_SSL_WANT_WRITE) ? 2 : 1;
        t->pal->tcp_poll(t->tcp_handle, ev, remaining);
    }
}

/* =========================================================================
 * tls_get_tcp_handle / tls_close
 * ========================================================================= */
void *tls_get_tcp_handle(tls_t *t)
{
    return t ? t->tcp_handle : NULL;
}

void tls_close(tls_t *t)
{
    if (!t) return;
    mbedtls_ssl_close_notify(&t->ssl);
    if (t->tcp_handle)
        t->pal->tcp_close(t->tcp_handle);
    mbedtls_ssl_free(&t->ssl);
    mbedtls_ssl_config_free(&t->conf);
    mbedtls_x509_crt_free(&t->ca_chain);
    t->pal->mutex_destroy(t->yield_mutex);
    t->pal->free(t);
}
