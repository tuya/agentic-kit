/*
 * tls.c -- Shared TLS-over-TCP transport, backed by mbedTLS.
 *
 * Generalised from rtc-tcp-client's tai_tls.c and iot-client's inline TLS in
 * mqtt.c / http_client_interface.c.  See tls.h for the contract and I/O model.
 */

#include "tls.h"
#include "log.h"
#include "rng.h"

#include <string.h>
#include <stdio.h>

#include "mbedtls/ssl.h"
#include "mbedtls/x509_crt.h"
#include "mbedtls/error.h"
#include "mbedtls/net_sockets.h"
#include "mbedtls/version.h"

/* =========================================================================
 * tls_t -- per-connection state
 * ========================================================================= */
typedef enum {
    TLS_REQ_READ,
    TLS_REQ_WRITE,
} tls_req_type_t;

typedef struct {
    tls_req_type_t type;
    uint8_t       *read_buf;
    const uint8_t *write_buf;
    size_t         len;
    size_t         written;
    uint64_t       deadline_ms;
    uint32_t       timeout_ms;
    int            wait_events;
    int            result;
} tls_io_req_t;

struct tls_conn {
    mbedtls_ssl_context  ssl;
    mbedtls_ssl_config   conf;
    mbedtls_x509_crt     ca_chain;
    void                *tcp_handle;
    const pal_t         *pal;

    void *owner_thread;
    void *state_mutex;
    void *read_mutex;
    void *write_mutex;
    void *wake_sem;
    void *read_done_sem;
    void *write_done_sem;
    tls_io_req_t *read_req;
    tls_io_req_t *write_req;
    tls_io_req_t *active_req;
    uint8_t *rx_staging;
    size_t rx_staging_off;
    size_t rx_staging_len;
    volatile int owner_running;
    volatile int closing;
    volatile int broken;
    int peer_eof;
};

#ifndef TLS_OWNER_POLL_SLICE_MS
#define TLS_OWNER_POLL_SLICE_MS 20U
#endif
#ifndef TLS_OWNER_RX_STAGING_SIZE
#define TLS_OWNER_RX_STAGING_SIZE 4096U
#endif

/* =========================================================================
 * RNG callback for the TLS handshake.
 *
 * Forwards to the shared process-wide CTR-DRBG (common/rng.c), which mbedTLS
 * seeds once from the platform entropy source and then expands cheaply -- the
 * right shape for the many f_rng calls a handshake makes. The f_rng ctx carries
 * the pal (set via mbedtls_ssl_conf_rng below) so rng_bytes() can lock the DRBG.
 * ========================================================================= */
static int tls_rng(void *ctx, unsigned char *buf, size_t len)
{
    const pal_t *pal = (const pal_t *)ctx;
    return rng_bytes(pal, buf, len) == 0 ? 0 : -1;
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
    if (t->rx_staging_len > 0) {
        size_t n = len < t->rx_staging_len ? len : t->rx_staging_len;
        memcpy(buf, t->rx_staging + t->rx_staging_off, n);
        t->rx_staging_off += n;
        t->rx_staging_len -= n;
        if (t->rx_staging_len == 0) t->rx_staging_off = 0;
        return (int)n;
    }
    if (t->peer_eof) return 0;

    int rc = t->pal->tcp_recv(t->tcp_handle, buf, len, 0);
    if (rc > 0) return rc;
    if (rc == 0) { t->peer_eof = 1; return 0; }
    if (rc == PAL_ERR_AGAIN) return MBEDTLS_ERR_SSL_WANT_READ;
    return MBEDTLS_ERR_NET_RECV_FAILED;
}

static int tls_bio_recv_timeout(void *ctx, unsigned char *buf,
                                size_t len, uint32_t timeout)
{
    tls_t *t = (tls_t *)ctx;
    if (t->rx_staging_len > 0 || t->peer_eof)
        return tls_bio_recv(ctx, buf, len);

    int rc = t->pal->tcp_recv(t->tcp_handle, buf, len, timeout);
    if (rc > 0) return rc;
    if (rc == 0) { t->peer_eof = 1; return 0; }
    if (rc == PAL_ERR_AGAIN) return MBEDTLS_ERR_SSL_WANT_READ;
    return MBEDTLS_ERR_NET_RECV_FAILED;
}

static void tls_complete_request(tls_t *t, tls_io_req_t *req, int result)
{
    const pal_t *pal = t->pal;
    pal->mutex_lock(t->state_mutex);
    if (req->type == TLS_REQ_READ) {
        if (t->read_req == req) t->read_req = NULL;
    } else {
        if (t->write_req == req) t->write_req = NULL;
    }
    req->result = result;
    pal->mutex_unlock(t->state_mutex);
    pal->sem_give(req->type == TLS_REQ_READ ? t->read_done_sem
                                             : t->write_done_sem);
}

static int tls_request_expired(tls_t *t, const tls_io_req_t *req)
{
    return req->timeout_ms == 0 || t->pal->time_ms() >= req->deadline_ms;
}

static int tls_drive_write(tls_t *t, tls_io_req_t *req)
{
    int n = mbedtls_ssl_write(&t->ssl,
                              req->write_buf + req->written,
                              req->len - req->written);
    if (n > 0) {
        req->written += (size_t)n;
        /* Partial write: more bytes remain. The socket was just writable, so
         * poll for writable (not "no events") and retry immediately. Setting
         * wait_events to 0 here made the owner poll only for readability and
         * stall each remaining fragment by a full poll slice. */
        req->wait_events = 2;
        return req->written == req->len ? TLS_OK : TLS_ERR_AGAIN;
    }
    if (n == MBEDTLS_ERR_SSL_WANT_WRITE || n == MBEDTLS_ERR_SSL_WANT_READ) {
        if (tls_request_expired(t, req)) return TLS_ERR_NET;
        req->wait_events = n == MBEDTLS_ERR_SSL_WANT_READ ? 1 : 2;
        return TLS_ERR_AGAIN;
    }
    return TLS_ERR_NET;
}

static int tls_drive_read(tls_t *t, tls_io_req_t *req)
{
    int n = mbedtls_ssl_read(&t->ssl, req->read_buf, req->len);
    if (n > 0) return n;
    if (n == 0 || n == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) return 0;
    if (n == MBEDTLS_ERR_SSL_WANT_READ ||
        n == MBEDTLS_ERR_SSL_WANT_WRITE ||
        n == MBEDTLS_ERR_SSL_TIMEOUT) {
        if (tls_request_expired(t, req)) return TLS_ERR_AGAIN;
        req->wait_events = n == MBEDTLS_ERR_SSL_WANT_WRITE ? 2 : 1;
        return TLS_ERR_AGAIN;
    }
    return TLS_ERR_NET;
}

static int tls_stage_socket_input(tls_t *t)
{
    if (t->peer_eof || t->rx_staging_len != 0) return 0;
    int n = t->pal->tcp_recv(t->tcp_handle, t->rx_staging,
                             TLS_OWNER_RX_STAGING_SIZE, 0);
    if (n > 0) {
        t->rx_staging_off = 0;
        t->rx_staging_len = (size_t)n;
        return 1;
    }
    if (n == 0) t->peer_eof = 1;
    return 0;
}

static void *tls_owner_thread(void *arg)
{
    tls_t *t = (tls_t *)arg;
    const pal_t *pal = t->pal;

    for (;;) {
        pal->mutex_lock(t->state_mutex);
        tls_io_req_t *read_req = t->read_req;
        tls_io_req_t *write_req = t->write_req;
        int closing = t->closing;
        if (!t->active_req && write_req)
            t->active_req = write_req;
        tls_io_req_t *active_write = t->active_req;
        pal->mutex_unlock(t->state_mutex);

        if (closing) {
            if (!t->broken) (void)mbedtls_ssl_close_notify(&t->ssl);
            if (read_req) tls_complete_request(t, read_req, TLS_ERR_NET);
            if (write_req) tls_complete_request(t, write_req, TLS_ERR_NET);
            break;
        }

        if (active_write) {
            int rc = tls_drive_write(t, active_write);
            int complete = rc != TLS_ERR_AGAIN ||
                           tls_request_expired(t, active_write);
            if (complete) {
                if (rc == TLS_ERR_AGAIN) rc = TLS_ERR_NET;
                if (rc == TLS_ERR_NET) t->broken = 1;
                pal->mutex_lock(t->state_mutex);
                t->active_req = NULL;
                pal->mutex_unlock(t->state_mutex);
                tls_complete_request(t, active_write, rc);
                continue;
            }

            /* While a write owns the mbedTLS context we cannot run a read, so
             * drain any inbound bytes into the staging buffer. Without this the
             * ESP receive window fills during a blocked write and the peer
             * stalls, which is the exact full-duplex deadlock being fixed. */
            tls_stage_socket_input(t);

            int events = active_write->wait_events;
            if (t->rx_staging_len == 0) events |= 1;
            pal->tcp_poll(t->tcp_handle, events, TLS_OWNER_POLL_SLICE_MS);
            continue;
        }

        if (read_req) {
            int rc = tls_drive_read(t, read_req);
            if (rc != TLS_ERR_AGAIN || tls_request_expired(t, read_req)) {
                tls_complete_request(t, read_req, rc);
                continue;
            }
            pal->tcp_poll(t->tcp_handle, read_req->wait_events,
                          TLS_OWNER_POLL_SLICE_MS);
            continue;
        }

        pal->sem_take(t->wake_sem, UINT32_MAX);
    }

    t->owner_running = 0;
    return NULL;
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
    t->state_mutex = pal->mutex_create();
    t->read_mutex = pal->mutex_create();
    t->write_mutex = pal->mutex_create();
    t->wake_sem = pal->sem_create(0);
    t->read_done_sem = pal->sem_create(0);
    t->write_done_sem = pal->sem_create(0);
    t->rx_staging = (uint8_t *)pal->malloc(TLS_OWNER_RX_STAGING_SIZE);
    if (!t->state_mutex || !t->read_mutex || !t->write_mutex ||
        !t->wake_sem || !t->read_done_sem || !t->write_done_sem ||
        !t->rx_staging) {
        goto fail;
    }

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
            /* snprintf NUL-terminates and returns the body length, so there is
             * no need to pre-zero the buffer or re-strlen it. */
            int wn = snprintf(pem, pem_len,
                     "-----BEGIN CERTIFICATE-----\n%s\n-----END CERTIFICATE-----\n",
                     cfg->cacert);
            if (wn < 0 || (size_t)wn >= pem_len) { pal->free(pem); goto fail; }
            pret = mbedtls_x509_crt_parse(&t->ca_chain,
                                          (const unsigned char *)pem,
                                          (size_t)wn + 1);  /* +1: parser wants the NUL */
            pal->free(pem);
        }
        if (pret != 0) {
            log_emit(LOG_ERROR, "[tls] failed to parse CA certificate: -0x%04X",
                     (unsigned)-pret);
            goto fail;
        }
    }

    /* One deadline for the whole connection establishment: the TCP connect and the
     * TLS handshake below share this single budget, so tls_connect returns within
     * ~connect_timeout_ms regardless of which phase stalls. */
    uint32_t est_to = cfg->connect_timeout_ms ? cfg->connect_timeout_ms
                                              : TLS_DEFAULT_CONNECT_TIMEOUT_MS;
    uint64_t deadline = pal->time_ms() + est_to;

    t->tcp_handle = pal->tcp_connect(cfg->host, cfg->port, est_to);
    if (!t->tcp_handle) {
        log_emit(LOG_ERROR, "[tls] TCP connect failed to %s:%u",
                 cfg->host, (unsigned)cfg->port);
        goto fail;
    }
    if (pal->time_ms() >= deadline) {
        log_emit(LOG_ERROR,
                 "[tls] connect to %s:%u exhausted %ums budget during TCP connect",
                 cfg->host, (unsigned)cfg->port, (unsigned)est_to);
        goto fail;
    }

    if (mbedtls_ssl_config_defaults(&t->conf, MBEDTLS_SSL_IS_CLIENT,
                                    MBEDTLS_SSL_TRANSPORT_STREAM,
                                    MBEDTLS_SSL_PRESET_DEFAULT) != 0)
        goto fail;

    mbedtls_ssl_conf_rng(&t->conf, tls_rng, (void *)t->pal);  /* ctx = pal for rng_bytes lock */

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
    } else if (cfg->cert_bundle_attach) {
        mbedtls_ssl_conf_authmode(&t->conf, MBEDTLS_SSL_VERIFY_REQUIRED);
        cfg->cert_bundle_attach(&t->conf);
    } else {
        mbedtls_ssl_conf_authmode(&t->conf, tls_map_verify(cfg->verify));
        if (cfg->verify == TLS_VERIFY_NONE)
            log_emit(LOG_WARN,
                     "[tls] peer verification disabled (no CA certificate)");
    }

    if (mbedtls_ssl_setup(&t->ssl, &t->conf) != 0) goto fail;

    if (mbedtls_ssl_set_hostname(&t->ssl, cfg->sni ? cfg->sni : cfg->host) != 0)
        goto fail;

    mbedtls_ssl_set_bio(&t->ssl, t,
                        tls_bio_send, tls_bio_recv, tls_bio_recv_timeout);

    /* Handshake.  BIO is non-blocking, so WANT_READ/WANT_WRITE means "no data
     * yet" -- poll the socket before retrying instead of busy-looping.  Shares
     * the establishment `deadline` computed above: a peer that completes the TCP
     * connect but stalls the handshake (never progresses past WANT_READ) must not
     * hang the caller forever, so fail once that single budget elapses. */
    int ret;
    for (;;) {
        ret = mbedtls_ssl_handshake(&t->ssl);
        if (ret != MBEDTLS_ERR_SSL_WANT_READ &&
            ret != MBEDTLS_ERR_SSL_WANT_WRITE)
            break;

        uint64_t now = pal->time_ms();
        if (now >= deadline) {
            log_emit(LOG_ERROR, "[tls] connect to %s:%u timed out after %ums (handshake)",
                     cfg->host, (unsigned)cfg->port, (unsigned)est_to);
            goto fail;
        }
        /* Cap the poll wait to the remaining budget so we never overshoot. */
        uint32_t remaining = (uint32_t)(deadline - now);
        uint32_t poll_ms = remaining < 100 ? remaining : 100;
        int ev = (ret == MBEDTLS_ERR_SSL_WANT_READ) ? 1 : 2;
        /* <0 is a socket error: fail fast instead of re-polling a dead fd until
         * the deadline (which would burn CPU on a tight retry loop). */
        if (pal->tcp_poll(t->tcp_handle, ev, poll_ms) < 0) {
            log_emit(LOG_ERROR, "[tls] handshake poll error on %s:%u",
                     cfg->host, (unsigned)cfg->port);
            goto fail;
        }
    }
    if (ret != 0) {
        log_emit(LOG_ERROR, "[tls] handshake failed to %s:%u: -0x%04X",
                 cfg->host, (unsigned)cfg->port, (unsigned)-ret);
        goto fail;
    }

    /* VERIFY_REQUIRED already failed the handshake on a bad chain; surface the
     * result on the optional path for diagnostics. */
    if (!has_ca && cfg->verify == TLS_VERIFY_OPTIONAL) {
        uint32_t flags = mbedtls_ssl_get_verify_result(&t->ssl);
        if (flags != 0) {
            /* Decode the flag bits into human-readable reasons (CN mismatch,
             * expired, untrusted chain, ...) so cert failures are diagnosable
             * in production, not just an opaque hex code. */
            char vrfy_buf[512];
            int vn = mbedtls_x509_crt_verify_info(vrfy_buf, sizeof(vrfy_buf),
                                                  "  ! ", flags);
            if (vn > 0)
                log_emit(LOG_WARN,
                         "[tls] peer certificate not verified (0x%08X):\n%s",
                         (unsigned)flags, vrfy_buf);
            else
                log_emit(LOG_WARN, "[tls] peer certificate not verified (0x%08X)",
                         (unsigned)flags);
        }
    }

    log_emit(LOG_INFO, "[tls] connected to %s:%u (%s, %s)",
             cfg->host, (unsigned)cfg->port,
             mbedtls_ssl_get_version(&t->ssl),
             mbedtls_ssl_get_ciphersuite(&t->ssl));
    t->owner_running = 1;
    if (pal->thread_create(&t->owner_thread, tls_owner_thread, t) != 0) {
        t->owner_running = 0;
        log_emit(LOG_ERROR, "[tls] failed to create TLS owner thread");
        goto fail;
    }
    return t;

fail:
    if (t->tcp_handle) pal->tcp_close(t->tcp_handle);
    mbedtls_ssl_free(&t->ssl);
    mbedtls_ssl_config_free(&t->conf);
    mbedtls_x509_crt_free(&t->ca_chain);
    if (t->rx_staging) pal->free(t->rx_staging);
    if (t->read_done_sem) pal->sem_destroy(t->read_done_sem);
    if (t->write_done_sem) pal->sem_destroy(t->write_done_sem);
    if (t->wake_sem) pal->sem_destroy(t->wake_sem);
    if (t->read_mutex) pal->mutex_destroy(t->read_mutex);
    if (t->write_mutex) pal->mutex_destroy(t->write_mutex);
    if (t->state_mutex) pal->mutex_destroy(t->state_mutex);
    pal->free(t);
    return NULL;
}

/* =========================================================================
 * Synchronous request submission
 * ========================================================================= */
static int tls_submit_request(tls_t *t, tls_io_req_t *req)
{
    const pal_t *pal = t->pal;
    void *direction_mutex = req->type == TLS_REQ_READ ? t->read_mutex
                                                       : t->write_mutex;
    void *done_sem = req->type == TLS_REQ_READ ? t->read_done_sem
                                                : t->write_done_sem;

    pal->mutex_lock(direction_mutex);
    pal->mutex_lock(t->state_mutex);
    if (t->closing || t->broken || !t->owner_running) {
        pal->mutex_unlock(t->state_mutex);
        pal->mutex_unlock(direction_mutex);
        return TLS_ERR_NET;
    }
    if (req->type == TLS_REQ_READ) t->read_req = req;
    else                           t->write_req = req;
    pal->mutex_unlock(t->state_mutex);

    pal->sem_give(t->wake_sem);
    pal->sem_take(done_sem, UINT32_MAX);
    int result = req->result;
    pal->mutex_unlock(direction_mutex);
    return result;
}

int tls_write(tls_t *t, const uint8_t *buf, size_t len, uint32_t timeout_ms)
{
    if (!t || (!buf && len > 0)) return TLS_ERR_ARGS;
    if (len == 0) return TLS_OK;

    tls_io_req_t req = {
        .type = TLS_REQ_WRITE,
        .write_buf = buf,
        .len = len,
        .deadline_ms = t->pal->time_ms() + timeout_ms,
        .timeout_ms = timeout_ms,
        .result = TLS_ERR_NET,
    };
    return tls_submit_request(t, &req);
}

int tls_read(tls_t *t, uint8_t *buf, size_t len, uint32_t timeout_ms)
{
    if (!t || !buf || len == 0) return TLS_ERR_ARGS;

    tls_io_req_t req = {
        .type = TLS_REQ_READ,
        .read_buf = buf,
        .len = len,
        .deadline_ms = t->pal->time_ms() + timeout_ms,
        .timeout_ms = timeout_ms,
        .result = TLS_ERR_NET,
    };
    return tls_submit_request(t, &req);
}

void *tls_get_tcp_handle(tls_t *t)
{
    return t ? t->tcp_handle : NULL;
}

void tls_close(tls_t *t)
{
    if (!t) return;
    const pal_t *pal = t->pal;

    pal->mutex_lock(t->state_mutex);
    t->closing = 1;
    pal->mutex_unlock(t->state_mutex);
    pal->sem_give(t->wake_sem);
    if (t->owner_thread) {
        pal->thread_join(t->owner_thread);
        t->owner_thread = NULL;
    }

    if (t->tcp_handle)
        pal->tcp_close(t->tcp_handle);
    mbedtls_ssl_free(&t->ssl);
    mbedtls_ssl_config_free(&t->conf);
    mbedtls_x509_crt_free(&t->ca_chain);
    pal->free(t->rx_staging);
    pal->sem_destroy(t->read_done_sem);
    pal->sem_destroy(t->write_done_sem);
    pal->sem_destroy(t->wake_sem);
    pal->mutex_destroy(t->read_mutex);
    pal->mutex_destroy(t->write_mutex);
    pal->mutex_destroy(t->state_mutex);
    pal->free(t);
}
