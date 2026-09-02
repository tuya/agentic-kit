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
#include "mbedtls/platform_util.h"   /* mbedtls_platform_zeroize for the key log */

#if defined(__unix__) || defined(__APPLE__)
#include <sys/stat.h>                /* fchmod: keep the key-log file owner-only */
#include <unistd.h>
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
 * TLS key log (NSS SSLKEYLOGFILE format) -- opt-in, off by default.
 *
 * Process-wide like the DRBG above: one sink covers every connection. The
 * handler is read inside the mbedTLS key-export callback, i.e. during the
 * handshake, so a connection picks up whatever was installed by then. See
 * tls.h for the contract and the warning about what these lines are.
 * ========================================================================= */

/* volatile: installed on the app's startup thread, read from whichever thread
 * runs a handshake (the rtc worker as well as the app loop). */
static tls_keylog_fn volatile g_keylog_fn  = NULL;
static void *volatile         g_keylog_ctx = NULL;

/* File sink state, owned by tls_keylog_open_file / _close_file. */
static FILE *g_keylog_file = NULL;

static void tls_keylog_file_write(void *ctx, const char *line)
{
    FILE *f = (FILE *)ctx;
    if (!f) return;
    /* Unbuffered (see below), so this reaches the file before we return -- a
     * capture is usable even if the device is reset mid-session. */
    fputs(line, f);
}

void tls_set_keylog_handler(tls_keylog_fn fn, void *ctx)
{
    /* Installing a different sink while the file sink is active would leave the
     * FILE* open with nothing writing to it; close it so _close_file stays
     * idempotent and the fd is not leaked. */
    if (g_keylog_file && fn != tls_keylog_file_write) {
        fclose(g_keylog_file);
        g_keylog_file = NULL;
    }
    g_keylog_ctx = ctx;
    g_keylog_fn  = fn;   /* last: ctx must be visible before the fn is callable */
    if (fn)
        log_emit(LOG_WARN, "[tls] key logging ENABLED -- session secrets are "
                           "being exported; do not use in production");
}

int tls_keylog_open_file(const char *path)
{
    if (!path || path[0] == '\0') return TLS_ERR_ARGS;
    if (g_keylog_file) {
        log_emit(LOG_ERROR, "[tls] key log file already open");
        return TLS_ERR_ARGS;
    }
    FILE *f = fopen(path, "a");
    if (!f) {
        log_emit(LOG_ERROR, "[tls] cannot open key log file '%s'", path);
        return TLS_ERR_ARGS;
    }
#if defined(__unix__) || defined(__APPLE__)
    /* The file IS the secret; the default 0666 & ~umask is too generous for it. */
    (void)fchmod(fileno(f), S_IRUSR | S_IWUSR);
#endif
    /* No stdio buffering: a buffer holding secrets cannot be wiped, and an
     * unflushed line is a capture that will not decrypt. */
    setvbuf(f, NULL, _IONBF, 0);
    g_keylog_file = f;
    tls_set_keylog_handler(tls_keylog_file_write, f);
    log_emit(LOG_WARN, "[tls] key log file: %s", path);
    return TLS_OK;
}

void tls_keylog_close_file(void)
{
    if (g_keylog_fn == tls_keylog_file_write) {
        g_keylog_fn  = NULL;   /* stop the handshake path before closing the FILE* */
        g_keylog_ctx = NULL;
    }
    if (g_keylog_file) {
        fclose(g_keylog_file);
        g_keylog_file = NULL;
    }
}

/* NSS label for one mbedTLS export type, or NULL for a type Wireshark has no
 * label for (nothing is written then, rather than a line it would ignore). */
static const char *tls_keylog_label(mbedtls_ssl_key_export_type type)
{
    switch (type) {
    case MBEDTLS_SSL_KEY_EXPORT_TLS12_MASTER_SECRET:
        return "CLIENT_RANDOM";
#if defined(MBEDTLS_SSL_PROTO_TLS1_3)
    case MBEDTLS_SSL_KEY_EXPORT_TLS1_3_CLIENT_EARLY_SECRET:
        return "CLIENT_EARLY_TRAFFIC_SECRET";
    case MBEDTLS_SSL_KEY_EXPORT_TLS1_3_EARLY_EXPORTER_SECRET:
        return "EARLY_EXPORTER_SECRET";
    case MBEDTLS_SSL_KEY_EXPORT_TLS1_3_CLIENT_HANDSHAKE_TRAFFIC_SECRET:
        return "CLIENT_HANDSHAKE_TRAFFIC_SECRET";
    case MBEDTLS_SSL_KEY_EXPORT_TLS1_3_SERVER_HANDSHAKE_TRAFFIC_SECRET:
        return "SERVER_HANDSHAKE_TRAFFIC_SECRET";
    case MBEDTLS_SSL_KEY_EXPORT_TLS1_3_CLIENT_APPLICATION_TRAFFIC_SECRET:
        return "CLIENT_TRAFFIC_SECRET_0";
    case MBEDTLS_SSL_KEY_EXPORT_TLS1_3_SERVER_APPLICATION_TRAFFIC_SECRET:
        return "SERVER_TRAFFIC_SECRET_0";
#endif
    default:
        return NULL;
    }
}

/* Longest secret any export type carries here: the TLS 1.2 master secret is 48
 * bytes and a TLS 1.3 traffic secret is one hash (SHA-384 -> 48). Anything
 * larger is not a line Wireshark expects, so it is dropped rather than truncated
 * into a silently undecryptable log. */
#define TLS_KEYLOG_MAX_SECRET 64
/* Longest label above is 31 chars (CLIENT_HANDSHAKE_TRAFFIC_SECRET). */
#define TLS_KEYLOG_MAX_LABEL  40
/* label + ' ' + 32-byte client random in hex + ' ' + secret in hex + "\n\0" */
#define TLS_KEYLOG_LINE_SIZE  (TLS_KEYLOG_MAX_LABEL + 1 + 64 + 1 + \
                               TLS_KEYLOG_MAX_SECRET * 2 + 2)

static void tls_hex(char *out, const unsigned char *in, size_t len)
{
    static const char hexdig[] = "0123456789abcdef";
    for (size_t i = 0; i < len; i++) {
        out[i * 2]     = hexdig[in[i] >> 4];
        out[i * 2 + 1] = hexdig[in[i] & 0x0F];
    }
}

/* mbedTLS key-export callback: format one NSS key-log line and hand it to the
 * active sink. Runs on the handshaking thread. */
static void tls_keylog_export(void *p_expkey,
                              mbedtls_ssl_key_export_type type,
                              const unsigned char *secret,
                              size_t secret_len,
                              const unsigned char client_random[32],
                              const unsigned char server_random[32],
                              mbedtls_tls_prf_types tls_prf_type)
{
    (void)p_expkey;
    (void)server_random;
    (void)tls_prf_type;

    /* Read the handler once: the app may swap it concurrently, and a NULL check
     * against one value followed by a call through another would fault. */
    tls_keylog_fn fn = g_keylog_fn;
    if (!fn) return;

    const char *label = tls_keylog_label(type);
    if (!label || secret_len == 0 || secret_len > TLS_KEYLOG_MAX_SECRET) return;

    char line[TLS_KEYLOG_LINE_SIZE];
    size_t label_len = strlen(label);
    /* A future label longer than the budget would overrun `line`; drop the line
     * rather than write past it (no -Wall here, so nothing else would say so). */
    if (label_len > TLS_KEYLOG_MAX_LABEL) return;
    size_t n = 0;

    memcpy(line + n, label, label_len);      n += label_len;
    line[n++] = ' ';
    tls_hex(line + n, client_random, 32);    n += 64;
    line[n++] = ' ';
    tls_hex(line + n, secret, secret_len);   n += secret_len * 2;
    line[n++] = '\n';
    line[n]   = '\0';

    fn(g_keylog_ctx, line);

    /* The line is a copy of the secret; do not leave it on the stack. */
    mbedtls_platform_zeroize(line, sizeof(line));
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

    /* Key export is attached unconditionally -- the callback returns immediately
     * unless a sink is installed, so this costs one pointer and keeps the
     * "enabled at handshake time" semantics tls.h documents. */
    mbedtls_ssl_set_export_keys_cb(&t->ssl, tls_keylog_export, NULL);

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
            if (elapsed >= timeout_ms) {
                /* Deadline hit while still draining -- distinguish this from a
                 * hard mbedTLS error below so a slow/backed-up link is not
                 * misread as a broken connection. */
                log_emit(LOG_ERROR,
                         "[tls] write timed out after %ums (%u/%u bytes sent)",
                         (unsigned)timeout_ms, (unsigned)written, (unsigned)len);
                return TLS_ERR_NET;
            }
            uint32_t remaining = (uint32_t)(timeout_ms - elapsed);
            int ev = (n == MBEDTLS_ERR_SSL_WANT_READ) ? 1 : 2;
            t->pal->tcp_poll(t->tcp_handle, ev, remaining);
            continue;
        }
        /* Fatal write error -- log the raw mbedTLS cause before collapsing. */
        log_emit(LOG_ERROR, "[tls] write failed: -0x%04X", (unsigned)-n);
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
#ifdef MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET
        /* TLS 1.3: the server may deliver a NewSessionTicket between app records.
         * mbedtls surfaces it here as a non-fatal signal — just read again, or
         * the whole session would tear down mid-stream at random. */
        if (n == MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET) continue;
#endif
        if (n != MBEDTLS_ERR_SSL_WANT_READ &&
            n != MBEDTLS_ERR_SSL_WANT_WRITE &&
            n != MBEDTLS_ERR_SSL_TIMEOUT) {
            /* Fatal read error (peer reset, TLS record/MAC failure, fatal alert,
             * underlying socket error). `n` is the raw mbedTLS cause -- log it
             * before collapsing to TLS_ERR_NET, which otherwise hides which
             * fault occurred from callers that only see -3. */
            log_emit(LOG_ERROR, "[tls] read failed: -0x%04X", (unsigned)-n);
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
