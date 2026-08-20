#include "iot_ota.h"
#include "iot_config_defaults.h"

#include <mbedtls/md5.h>
#include <mbedtls/sha256.h>
#include <mbedtls/md.h>

#include <string.h>

/**
 * @file iot_ota_verify.c
 * @brief Streaming firmware digest verification for OTA downloads.
 *
 * Replicates the validation performed by TuyaOpen's tuya_ota.c:
 *   expected = HMAC-SHA256(device secret_key, UPPERCASE_hex(SHA-256(image)))
 * i.e. the HMAC message is the 64-character hex STRING of the SHA-256
 * digest (uppercase, matching TuyaOpen's hex2str), not the raw 32-byte
 * digest. MD5 is provided as a fallback for upgrade responses that carry
 * no "hmac" field.
 */

#define OTA_MD5_HEX_LEN    32
#define OTA_SHA256_HEX_LEN 64

typedef enum {
    OTA_VERIFY_HMAC_SHA256,
    OTA_VERIFY_MD5,
} ota_verify_algo_t;

struct iot_ota_verify_ctx {
    const pal_t *pal;          /* borrowed from client */
    ota_verify_algo_t algo;
    char expected[OTA_SHA256_HEX_LEN + 1]; /* lowercased cloud value */
    union {
        mbedtls_sha256_context sha256;
        mbedtls_md5_context    md5;
    } digest;
    uint8_t key[32];           /* secret_key copy (HMAC path) */
    size_t  key_len;
};

static void bytes_to_hex_lower(const uint8_t *src, size_t len, char *dst)
{
    static const char hexdig[] = "0123456789abcdef";
    for (size_t i = 0; i < len; i++) {
        dst[i * 2]     = hexdig[src[i] >> 4];
        dst[i * 2 + 1] = hexdig[src[i] & 0x0F];
    }
    dst[len * 2] = '\0';
}

/* TuyaOpen's hex2str produces UPPERCASE hex; the cloud's HMAC is computed
 * over the uppercase hex string of the SHA-256 digest, so we must match. */
static void bytes_to_hex_upper(const uint8_t *src, size_t len, char *dst)
{
    static const char hexdig[] = "0123456789ABCDEF";
    for (size_t i = 0; i < len; i++) {
        dst[i * 2]     = hexdig[src[i] >> 4];
        dst[i * 2 + 1] = hexdig[src[i] & 0x0F];
    }
    dst[len * 2] = '\0';
}

static void str_to_lower(char *s)
{
    for (; *s; s++) {
        if (*s >= 'A' && *s <= 'Z') {
            *s += 'a' - 'A';
        }
    }
}

/* Compare without early exit on the first differing byte. */
static int hex_equal_consttime(const char *a, const char *b, size_t len)
{
    unsigned char diff = 0;
    for (size_t i = 0; i < len; i++) {
        diff |= (unsigned char)(a[i] ^ b[i]);
    }
    return diff == 0;
}

static void ctx_free(iot_ota_verify_ctx_t *ctx)
{
    if (ctx == NULL) {
        return;
    }
    if (ctx->algo == OTA_VERIFY_HMAC_SHA256) {
        mbedtls_sha256_free(&ctx->digest.sha256);
    } else if (ctx->algo == OTA_VERIFY_MD5) {
        mbedtls_md5_free(&ctx->digest.md5);
    }
    ctx->pal->free(ctx);
}

int iot_ota_verify_init(iot_client_t *client,
                        const iot_ota_upgrade_info_t *info,
                        iot_ota_verify_ctx_t **ctx_out)
{
    if (client == NULL || info == NULL || ctx_out == NULL || client->pal == NULL) {
        return OPRT_INVALID_PARAMETER;
    }

    const char *expected = NULL;
    size_t expected_len = 0;
    ota_verify_algo_t algo;

    /* The cloud sends "" for a digest it has not configured, so an empty string
     * means absent: fall through to the next algorithm instead of rejecting the
     * upgrade. A non-empty value of the wrong length stays a hard error -- a
     * malformed hmac must never silently downgrade to the weaker md5. */
    if (info->hmac != NULL && info->hmac[0] != '\0') {
        algo = OTA_VERIFY_HMAC_SHA256;
        expected = info->hmac;
        expected_len = strlen(expected);
        if (expected_len != OTA_SHA256_HEX_LEN) {
            return OPRT_INVALID_PARAMETER;
        }
    } else if (info->md5 != NULL && info->md5[0] != '\0') {
        algo = OTA_VERIFY_MD5;
        expected = info->md5;
        expected_len = strlen(expected);
        if (expected_len != OTA_MD5_HEX_LEN) {
            return OPRT_INVALID_PARAMETER;
        }
    } else {
        return OPRT_NOT_SUPPORTED;
    }

    const pal_t *pal = client->pal;
    iot_ota_verify_ctx_t *ctx = pal->malloc(sizeof(*ctx));
    if (ctx == NULL) {
        return OPRT_MALLOC_FAILED;
    }
    memset(ctx, 0, sizeof(*ctx));

    ctx->pal = pal;
    ctx->algo = algo;
    memcpy(ctx->expected, expected, expected_len);
    ctx->expected[expected_len] = '\0';
    str_to_lower(ctx->expected);

    int ret = 0;
    if (algo == OTA_VERIFY_HMAC_SHA256) {
        ctx->key_len = strlen(client->secret_key);
        if (ctx->key_len == 0 || ctx->key_len > sizeof(ctx->key)) {
            pal->free(ctx);
            return OPRT_INVALID_PARAMETER;
        }
        memcpy(ctx->key, client->secret_key, ctx->key_len);

        mbedtls_sha256_init(&ctx->digest.sha256);
        ret = mbedtls_sha256_starts(&ctx->digest.sha256, 0);
        if (ret != 0) {
            log_error("sha256 starts failed: -0x%04x", -ret);
            ctx_free(ctx);
            return ret;
        }
    } else {
        mbedtls_md5_init(&ctx->digest.md5);
        ret = mbedtls_md5_starts(&ctx->digest.md5);
        if (ret != 0) {
            log_error("md5 starts failed: -0x%04x", -ret);
            ctx_free(ctx);
            return ret;
        }
    }

    *ctx_out = ctx;
    return OPRT_OK;
}

int iot_ota_verify_update(iot_ota_verify_ctx_t *ctx,
                          const uint8_t *data, size_t len)
{
    if (ctx == NULL || (len > 0 && data == NULL)) {
        return OPRT_INVALID_PARAMETER;
    }

    int ret;
    if (ctx->algo == OTA_VERIFY_HMAC_SHA256) {
        ret = mbedtls_sha256_update(&ctx->digest.sha256, data, len);
    } else {
        ret = mbedtls_md5_update(&ctx->digest.md5, data, len);
    }
    if (ret != 0) {
        log_error("digest update failed: -0x%04x", -ret);
        return ret;
    }
    return OPRT_OK;
}

int iot_ota_verify_finish(iot_ota_verify_ctx_t *ctx)
{
    if (ctx == NULL) {
        return OPRT_INVALID_PARAMETER;
    }

    char actual[OTA_SHA256_HEX_LEN + 1];
    int ret = OPRT_OK;

    if (ctx->algo == OTA_VERIFY_HMAC_SHA256) {
        uint8_t sha[32];
        ret = mbedtls_sha256_finish(&ctx->digest.sha256, sha);
        if (ret != 0) {
            log_error("sha256 finish failed: -0x%04x", -ret);
            ctx_free(ctx);
            return ret;
        }

        /* HMAC message is the UPPERCASE hex string of the SHA-256 digest
         * (matches TuyaOpen's hex2str which produces A-F, not a-f). */
        char sha_hex[OTA_SHA256_HEX_LEN + 1];
        bytes_to_hex_upper(sha, sizeof(sha), sha_hex);

        uint8_t mac[32];
        const mbedtls_md_info_t *md_info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
        ret = mbedtls_md_hmac(md_info, ctx->key, ctx->key_len,
                              (const uint8_t *)sha_hex, OTA_SHA256_HEX_LEN, mac);
        if (ret != 0) {
            log_error("hmac failed: -0x%04x", -ret);
            ctx_free(ctx);
            return ret;
        }
        bytes_to_hex_lower(mac, sizeof(mac), actual);
    } else {
        uint8_t md5[16];
        ret = mbedtls_md5_finish(&ctx->digest.md5, md5);
        if (ret != 0) {
            log_error("md5 finish failed: -0x%04x", -ret);
            ctx_free(ctx);
            return ret;
        }
        bytes_to_hex_lower(md5, sizeof(md5), actual);
    }

    size_t expected_len = strlen(ctx->expected);
    int match = (expected_len == strlen(actual)) &&
                hex_equal_consttime(ctx->expected, actual, expected_len);

    ctx_free(ctx);
    return match ? OPRT_OK : OPRT_OTA_VERIFY_FAILED;
}

void iot_ota_verify_abort(iot_ota_verify_ctx_t *ctx)
{
    ctx_free(ctx);
}
