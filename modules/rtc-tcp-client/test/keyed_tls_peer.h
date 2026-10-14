/* Local verified TLS peer. Reuses committed, synthetic IoT test certificates
 * (valid through February 2036); no generated credentials or external process. */
#ifndef KEYED_TLS_PEER_H
#define KEYED_TLS_PEER_H

#include <errno.h>
#include <poll.h>
#include <sys/time.h>
#include "mbedtls/ssl.h"
#include "mbedtls/x509_crt.h"
#include "mbedtls/pk.h"
#include "rng.h"

typedef struct {
    int listener, socket, mode, error;
    uint16_t port;
    pthread_t thread;
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config config;
    mbedtls_x509_crt cert;
    mbedtls_pk_context key;
    uint8_t handshake[8192];
    size_t handshake_len;
} keyed_peer_t;

static keyed_peer_t keyed_peer;
static char keyed_ca[4096];

static int keyed_fixture_path(char *path, size_t cap, const char *name)
{
    const char *slash = strrchr(__FILE__, '/');
    if (!slash) return -1;
    int n = snprintf(path, cap, "%.*s/../../iot-client/test/config/%s",
                     (int)(slash - __FILE__), __FILE__, name);
    return n > 0 && (size_t)n < cap ? 0 : -1;
}

static int keyed_rng(void *arg, unsigned char *buf, size_t len)
{
    (void)arg;
    return rng_bytes(tai_pal_posix(), buf, len);
}

static int keyed_bio_send(void *arg, const unsigned char *buf, size_t len)
{
    int fd = *(int *)arg;
#ifdef MSG_NOSIGNAL
    int n = (int)send(fd, buf, len, MSG_NOSIGNAL);
#else
    int n = (int)send(fd, buf, len, 0);
#endif
    return n > 0 ? n : MBEDTLS_ERR_SSL_INTERNAL_ERROR;
}

static int keyed_bio_recv(void *arg, unsigned char *buf, size_t len)
{
    int n = (int)recv(*(int *)arg, buf, len, 0);
    return n >= 0 ? n : MBEDTLS_ERR_SSL_INTERNAL_ERROR;
}

static int keyed_read_exact(keyed_peer_t *p, uint8_t *buf, size_t len)
{
    while (len) {
        int n = mbedtls_ssl_read(&p->ssl, buf, len);
        if (n <= 0) return -1;
        buf += n;
        len -= (size_t)n;
    }
    return 0;
}

static int keyed_read_frame(keyed_peer_t *p, uint8_t *buf, size_t cap)
{
    if (cap < 5 || keyed_read_exact(p, buf, 5)) return -1;
    size_t n = ((size_t)buf[3] << 8) | buf[4];
    if (n > cap - 5 || keyed_read_exact(p, buf + 5, n)) return -1;
    return (int)n + 5;
}

static int keyed_peer_write(const uint8_t *buf, size_t len)
{
    while (len) {
        int n = mbedtls_ssl_write(&keyed_peer.ssl, buf, len);
        if (n <= 0) return -1;
        buf += n;
        len -= (size_t)n;
    }
    return 0;
}

static void *keyed_peer_handshake(void *arg)
{
    keyed_peer_t *p = arg;
    struct pollfd wait = { .fd = p->listener, .events = POLLIN };
    if (poll(&wait, 1, 5000) <= 0) { p->error = -1; return NULL; }
    p->socket = accept(p->listener, NULL, NULL);
    if (p->socket < 0) { p->error = -2; return NULL; }
    struct timeval timeout = { .tv_sec = 5 };
    setsockopt(p->socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(p->socket, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
#ifdef SO_NOSIGPIPE
    int one = 1;
    setsockopt(p->socket, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
#endif
    mbedtls_ssl_set_bio(&p->ssl, &p->socket, keyed_bio_send, keyed_bio_recv, NULL);
    p->error = mbedtls_ssl_handshake(&p->ssl);
    if (p->error) return NULL;
    for (int i = 0; i < 2; i++) {
        int n = keyed_read_frame(p, p->handshake + p->handshake_len,
                                sizeof(p->handshake) - p->handshake_len);
        if (n < 0) { p->error = -3; return NULL; }
        p->handshake_len += (size_t)n;
    }
    /* SessionNew status=0, no body. Mode 1 downgrades the Frame, mode 2
     * appends a legacy signature, mode 3 advertises a mismatched suite.
     * Mode 4 confirms via AuthenticateResponse status=200 instead. */
    uint8_t ack[96] = {0x23, 0, 1, 0, 11, 0x0f, 0, 0, 0, 6,
                       0, 44, 0, 2, 0, 0};
    size_t n = 16;
    if (p->mode == 1) ack[0] = 0x22;
    if (p->mode == 2) {
        /* A correctly computed legacy HMAC is still forbidden on this profile. */
        uint8_t key[32] = {0};
        ack[4] += 32;
        p->error = mbedtls_md_hmac(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256),
                                   key, sizeof(key), ack, n, ack + n);
        if (p->error) return NULL;
        n += 32;
    }
    if (p->mode == 3) {
        ack[9] += 40;
        ack[n++] = 0; ack[n++] = 10; ack[n++] = 0; ack[n++] = 36;
        memset(ack + n, 0, 36);
        ack[n + 33] = 4; ack[n + 34] = 3; ack[n + 35] = 2;
        n += 36; ack[4] += 40;
    }
    if (p->mode == 4) { ack[5] = 7; ack[11] = 30; ack[15] = 200; }
    p->error = keyed_peer_write(ack, n);
    return NULL;
}

static void keyed_peer_close(void)
{
    keyed_peer_t *p = &keyed_peer;
    if (p->socket >= 0) close(p->socket);
    if (p->listener >= 0) close(p->listener);
    mbedtls_ssl_free(&p->ssl);
    mbedtls_ssl_config_free(&p->config);
    mbedtls_x509_crt_free(&p->cert);
    mbedtls_pk_free(&p->key);
    p->socket = p->listener = -1;
}

static int keyed_peer_start(int mode)
{
    keyed_peer_t *p = &keyed_peer;
    memset(p, 0, sizeof(*p));
    p->socket = p->listener = -1;
    p->mode = mode;
    mbedtls_ssl_init(&p->ssl);
    mbedtls_ssl_config_init(&p->config);
    mbedtls_x509_crt_init(&p->cert);
    mbedtls_pk_init(&p->key);
    char path[4096];
    if (keyed_fixture_path(path, sizeof(path), "root_cert.pem")) goto fail;
    FILE *f = fopen(path, "rb");
    if (!f) goto fail;
    size_t n = fread(keyed_ca, 1, sizeof(keyed_ca) - 1, f);
    int bad = ferror(f) || !feof(f);
    fclose(f);
    keyed_ca[n] = 0;
    if (bad) goto fail;
    if (keyed_fixture_path(path, sizeof(path), "cert.pem") ||
        mbedtls_x509_crt_parse_file(&p->cert, path)) goto fail;
    if (keyed_fixture_path(path, sizeof(path), "key.pem") ||
        mbedtls_pk_parse_keyfile(&p->key, path, NULL, keyed_rng, NULL)) goto fail;
    if (mbedtls_ssl_config_defaults(&p->config, MBEDTLS_SSL_IS_SERVER,
            MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT)) goto fail;
    mbedtls_ssl_conf_rng(&p->config, keyed_rng, NULL);
    mbedtls_ssl_conf_max_tls_version(&p->config, MBEDTLS_SSL_VERSION_TLS1_2);
    if (mbedtls_ssl_conf_own_cert(&p->config, &p->cert, &p->key) ||
        mbedtls_ssl_setup(&p->ssl, &p->config)) goto fail;
    p->listener = socket(AF_INET, SOCK_STREAM, 0);
    if (p->listener < 0) goto fail;
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    socklen_t alen = sizeof(addr);
    if (bind(p->listener, (struct sockaddr *)&addr, alen) ||
        getsockname(p->listener, (struct sockaddr *)&addr, &alen) ||
        listen(p->listener, 1)) goto fail;
    p->port = ntohs(addr.sin_port);
    if (pthread_create(&p->thread, NULL, keyed_peer_handshake, p)) goto fail;
    return 0;
fail:
    keyed_peer_close();
    return -1;
}

#endif
