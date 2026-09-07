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
#include <errno.h>

#include "mbedtls/ssl.h"
#include "mbedtls/x509_crt.h"
#include "mbedtls/error.h"
#include "mbedtls/net_sockets.h"
#include "mbedtls/version.h"
#include "mbedtls/platform_util.h"   /* mbedtls_platform_zeroize for the key log */

/* The key-log file sink is the only libc file I/O in library code. It is
 * compiled in where a writable filesystem is the norm (hosts, ESP-IDF's VFS)
 * and out elsewhere, so a bare newlib port without _open/_close/_lseek stubs
 * still links tls.o. Override with -DTLS_KEYLOG_FILE_SINK=0/1. */
#ifndef TLS_KEYLOG_FILE_SINK
#  if defined(__unix__) || defined(__APPLE__) || defined(ESP_PLATFORM)
#    define TLS_KEYLOG_FILE_SINK 1
#  else
#    define TLS_KEYLOG_FILE_SINK 0
#  endif
#endif

/* On POSIX the file is created with open(2) -- mode from creation, no symlink
 * following, no inheritance across exec -- and fsync'ed per line. */
#if TLS_KEYLOG_FILE_SINK && (defined(__unix__) || defined(__APPLE__))
#  define TLS_KEYLOG_POSIX_FILE 1
#  include <fcntl.h>
#  include <sys/stat.h>
#  include <unistd.h>
#else
#  define TLS_KEYLOG_POSIX_FILE 0
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
    tls_keylog_fn        keylog_fn;    /* key-log sink, snapshotted by tls_connect */
    void                *keylog_ctx;
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
 * Process-wide like the DRBG above: one sink covers every connection. The app
 * installs it once at startup, single-threaded, before the first tls_connect()
 * (tls.h); tls_connect() then snapshots the pair into the tls_t on the
 * connecting thread, and mbedTLS's export callback reads only its own
 * connection's copy. Thread creation orders that install before any later
 * handshake, so the globals need neither volatile nor a lock -- and neither
 * would make a *swap* during a handshake safe, which is why tls.h forbids one.
 * ========================================================================= */

static tls_keylog_fn g_keylog_fn  = NULL;
static void         *g_keylog_ctx = NULL;

#if TLS_KEYLOG_FILE_SINK
/* Set once a write has failed, so the failure is logged once rather than on
 * every handshake. Reset when the file sink is (re)installed. */
static bool g_keylog_file_failed = false;

static void tls_keylog_file_write(void *ctx, const char *line)
{
    FILE *f = (FILE *)ctx;
    /* Unbuffered stream (see open_file): fputs() is one write(2). A failed write
     * -- ENOSPC, a VFS gone read-only -- would otherwise drop the line silently
     * and leave a capture that will not decrypt, with nothing in the log to say
     * why. */
    if (fputs(line, f) == EOF) {
        if (!g_keylog_file_failed) {
            g_keylog_file_failed = true;
            log_emit(LOG_ERROR, "[tls] key log write failed (%s); later lines are lost",
                     strerror(errno));
        }
        return;
    }
#if TLS_KEYLOG_POSIX_FILE
    /* write(2) alone leaves the line in the page cache; tls.h promises it
     * survives a reset mid-session, so push it through. */
    (void)fsync(fileno(f));
#endif
}
#endif /* TLS_KEYLOG_FILE_SINK */

/* The one place the (fn, ctx) pair changes. Detaches the outgoing sink before
 * releasing anything it owns, publishes the new pair, and emits the single
 * "key logging ENABLED" line the docs tell operators to grep production logs
 * for. `where` names the sink for that line. */
static void keylog_install(tls_keylog_fn fn, void *ctx, const char *where)
{
    tls_keylog_fn old_fn  = g_keylog_fn;
    void         *old_ctx = g_keylog_ctx;
    g_keylog_fn  = NULL;
    g_keylog_ctx = NULL;
#if TLS_KEYLOG_FILE_SINK
    if (old_fn == tls_keylog_file_write) {
        fclose((FILE *)old_ctx);
        g_keylog_file_failed = false;
    }
#else
    (void)old_fn;
    (void)old_ctx;
#endif
    g_keylog_ctx = ctx;
    g_keylog_fn  = fn;
    if (fn)
        log_emit(LOG_WARN, "[tls] key logging ENABLED -- session secrets are "
                           "being exported to %s; do not use in production", where);
}

void tls_set_keylog_handler(tls_keylog_fn fn, void *ctx)
{
    keylog_install(fn, ctx, "a custom sink");
}

int tls_keylog_open_file(const char *path)
{
    if (!path || path[0] == '\0') return TLS_ERR_ARGS;
#if TLS_KEYLOG_FILE_SINK
    if (g_keylog_fn == tls_keylog_file_write) {
        log_emit(LOG_ERROR, "[tls] key log file already open");
        return TLS_ERR_ARGS;
    }
    FILE *f = NULL;
#if TLS_KEYLOG_POSIX_FILE
    /* The file IS the secret: 0600 from the moment it exists (fopen("a") +
     * fchmod would leave it at 0666 & ~umask in between), never through a
     * symlink someone planted at the documented path, never inherited by a
     * child the app execs. */
    int fd = open(path, O_WRONLY | O_CREAT | O_APPEND | O_NOFOLLOW | O_CLOEXEC,
                  S_IRUSR | S_IWUSR);
    if (fd >= 0) {
        f = fdopen(fd, "a");
        if (!f) close(fd);
    }
#else
    f = fopen(path, "a");
#endif
    if (!f) {
        log_emit(LOG_ERROR, "[tls] cannot open key log file '%s': %s",
                 path, strerror(errno));
        return TLS_ERR_ARGS;
    }
    /* No stdio buffering: a buffer holding secrets cannot be wiped, and an
     * unflushed line is a capture that will not decrypt. A stream that cannot be
     * made unbuffered is refused rather than silently buffered. */
    if (setvbuf(f, NULL, _IONBF, 0) != 0) {
        log_emit(LOG_ERROR, "[tls] cannot make key log file '%s' unbuffered", path);
        fclose(f);
        return TLS_ERR_ARGS;
    }
    keylog_install(tls_keylog_file_write, f, path);
    return TLS_OK;
#else
    log_emit(LOG_ERROR, "[tls] key log file sink not compiled in "
                        "(TLS_KEYLOG_FILE_SINK=0); use tls_set_keylog_handler()");
    return TLS_ERR_ARGS;
#endif
}

void tls_keylog_close_file(void)
{
#if TLS_KEYLOG_FILE_SINK
    if (g_keylog_fn == tls_keylog_file_write)
        keylog_install(NULL, NULL, NULL);
#endif
}

/* The export side needs mbedTLS 3.x (mbedtls_ssl_set_export_keys_cb and the
 * mbedtls_ssl_key_export_type enum replaced the 2.x conf_export_keys_cb API);
 * against 2.x the sink can be installed but nothing is exported, and
 * tls_connect() says so. */
#if MBEDTLS_VERSION_MAJOR >= 3

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

/* Longest secret any export type carries: the TLS 1.2 master secret is 48
 * bytes; a TLS 1.3 traffic secret is one hash, 64 bytes with SHA-512 enabled --
 * the same bound mbedTLS uses (MBEDTLS_TLS1_3_MD_MAX_SIZE). Anything longer is
 * not a line Wireshark expects and is dropped rather than truncated into a
 * silently undecryptable log. */
#define TLS_KEYLOG_MAX_SECRET 64
#define TLS_KEYLOG_MAX_LABEL  40
_Static_assert(sizeof("CLIENT_HANDSHAKE_TRAFFIC_SECRET") - 1 <= TLS_KEYLOG_MAX_LABEL,
               "longest NSS key-log label must fit TLS_KEYLOG_MAX_LABEL");
/* label + ' ' + 32-byte client random in hex + ' ' + secret in hex + "\n\0" */
#define TLS_KEYLOG_LINE_SIZE  (TLS_KEYLOG_MAX_LABEL + 1 + 64 + 1 + \
                               TLS_KEYLOG_MAX_SECRET * 2 + 2)

/* Same loop as iot_ota_verify.c's bytes_to_hex_lower; duplicated on purpose,
 * common/ has no hex helper and a module's static is not reachable from here. */
static void tls_hex(char *out, const unsigned char *in, size_t len)
{
    static const char hexdig[] = "0123456789abcdef";
    for (size_t i = 0; i < len; i++) {
        out[i * 2]     = hexdig[in[i] >> 4];
        out[i * 2 + 1] = hexdig[in[i] & 0x0F];
    }
}

/* mbedTLS key-export callback: format one NSS key-log line and hand it to this
 * connection's sink. Runs on the handshaking thread; registered by tls_connect()
 * only when a sink was installed, so p_expkey is always a tls_t with one. */
static void tls_keylog_export(void *p_expkey,
                              mbedtls_ssl_key_export_type type,
                              const unsigned char *secret,
                              size_t secret_len,
                              const unsigned char client_random[32],
                              const unsigned char server_random[32],
                              mbedtls_tls_prf_types tls_prf_type)
{
    tls_t *t = (tls_t *)p_expkey;
    (void)server_random;
    (void)tls_prf_type;

    const char *label = tls_keylog_label(type);
    if (!label || secret_len == 0) return;

    char line[TLS_KEYLOG_LINE_SIZE];
    size_t label_len = strlen(label);
    /* One bound, checked against the real buffer so it cannot drift from it. */
    if (label_len + 1 + 64 + 1 + secret_len * 2 + 2 > sizeof(line)) return;
    size_t n = 0;

    memcpy(line + n, label, label_len);      n += label_len;
    line[n++] = ' ';
    tls_hex(line + n, client_random, 32);    n += 64;
    line[n++] = ' ';
    tls_hex(line + n, secret, secret_len);   n += secret_len * 2;
    line[n++] = '\n';
    line[n]   = '\0';

    t->keylog_fn(t->keylog_ctx, line);

    /* The line is a copy of the secret; do not leave it on the stack. */
    mbedtls_platform_zeroize(line, sizeof(line));
}

#endif /* MBEDTLS_VERSION_MAJOR >= 3 */

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

    /* Snapshot the key-log sink on the connecting thread and register the export
     * callback only when one is installed: a connection with key logging off
     * carries no callback and pays nothing at the bottom of the handshake. Every
     * export call site is inside the handshake and tls.c never enables
     * renegotiation, so this is the "read at handshake time" tls.h documents. */
    t->keylog_fn  = g_keylog_fn;
    t->keylog_ctx = g_keylog_ctx;
    if (t->keylog_fn) {
#if MBEDTLS_VERSION_MAJOR >= 3
        mbedtls_ssl_set_export_keys_cb(&t->ssl, tls_keylog_export, t);
#else
        log_emit(LOG_WARN, "[tls] key logging needs mbedTLS 3.x; nothing is exported");
#endif
    }

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
