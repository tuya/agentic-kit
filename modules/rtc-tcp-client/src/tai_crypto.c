/*
 * tai_crypto.c -- Cryptographic primitives and key derivation.
 *
 * Implements HMAC-SHA256, HKDF-SHA256, and random byte generation using
 * mbedTLS directly.  Also provides HKDF-based key derivation per Section 6.1
 * of the Tuya AI Foundation 2.1 spec.
 */

#include "tai_internal.h"

#include "mbedtls/md.h"
#include "mbedtls/hkdf.h"
#include "mbedtls/gcm.h"
#include "mbedtls/platform_util.h"

#include "rng.h"

#define TAG "crypto"

/* =========================================================================
 * tai_hmac_sha256
 * ========================================================================= */
int tai_hmac_sha256(const uint8_t *key, size_t key_len,
                    const uint8_t *data, size_t data_len,
                    uint8_t out[32])
{
    const mbedtls_md_info_t *md = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (!md) return -1;
    return mbedtls_md_hmac(md, key, key_len, data, data_len, out) == 0 ? 0 : -1;
}

/* =========================================================================
 * tai_hkdf_sha256
 * ========================================================================= */
int tai_hkdf_sha256(const uint8_t *ikm, size_t ikm_len,
                    const uint8_t *salt, size_t salt_len,
                    uint8_t *out, size_t out_len)
{
    const mbedtls_md_info_t *md = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (!md) return -1;
    return mbedtls_hkdf(md, salt, salt_len,
                        ikm, ikm_len,
                        (const unsigned char *)"", 0,
                        out, out_len) == 0 ? 0 : -1;
}

static int buffers_overlap(const void *a, size_t a_len,
                           const void *b, size_t b_len)
{
    uintptr_t x = (uintptr_t)a, y = (uintptr_t)b;
    if (!a_len || !b_len) return 0;
    return x <= y ? y - x < a_len : x - y < b_len;
}

int tai_image_gcm_encrypt(const uint8_t key[TAI_IMAGE_KEY_LEN],
                          const uint8_t iv[16],
                          const uint8_t *data, size_t len,
                          uint8_t *out, size_t out_size)
{
    if (!key || !iv || !data || !out ||
        len > SIZE_MAX - 16 || len > PTRDIFF_MAX - 16)
        return TAI_ERR_ARGS;
    size_t body_len = len + 16;
    if (out_size < body_len) return TAI_ERR_MEM;
    if (buffers_overlap(out, body_len, data, len) ||
        buffers_overlap(out, body_len, key, TAI_IMAGE_KEY_LEN) ||
        buffers_overlap(out, body_len, iv, 16))
        return TAI_ERR_ARGS;

    mbedtls_gcm_context gcm;
    mbedtls_gcm_init(&gcm);
    int rc = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, key, 128);
    if (rc == 0)
        rc = mbedtls_gcm_crypt_and_tag(&gcm, MBEDTLS_GCM_ENCRYPT, len,
                                      iv, 16, NULL, 0, data, out,
                                      16, out + len);
    mbedtls_gcm_free(&gcm);
    if (rc != 0) {
        mbedtls_platform_zeroize(out, body_len);
        return TAI_ERR_CRYPTO;
    }
    return TAI_OK;
}

int tai_image_gcm_decrypt(const uint8_t key[TAI_IMAGE_KEY_LEN],
                          const uint8_t iv[16],
                          const uint8_t *body, size_t body_len,
                          uint8_t *out, size_t out_size)
{
    if (!key || !iv || !body || !out || body_len < 16)
        return TAI_ERR_ARGS;
    size_t len = body_len - 16;
    if (out_size < len) return TAI_ERR_MEM;
    if (buffers_overlap(out, len, body, body_len) ||
        buffers_overlap(out, len, key, TAI_IMAGE_KEY_LEN) ||
        buffers_overlap(out, len, iv, 16))
        return TAI_ERR_ARGS;

    mbedtls_gcm_context gcm;
    mbedtls_gcm_init(&gcm);
    int rc = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, key, 128);
    if (rc == 0)
        rc = mbedtls_gcm_auth_decrypt(&gcm, len, iv, 16, NULL, 0,
                                     body + len, 16, body, out);
    mbedtls_gcm_free(&gcm);
    if (rc != 0) {
        mbedtls_platform_zeroize(out, len);
        return TAI_ERR_CRYPTO;
    }
    return TAI_OK;
}

/* =========================================================================
 * tai_random_bytes
 * ========================================================================= */
int tai_random_bytes(const pal_t *pal, uint8_t *buf, size_t len)
{
    return rng_bytes(pal, buf, len);
}

/* =========================================================================
 * tai_crypto_derive_keys
 *
 * Derive encrypt_key and sign_key from IKM (local_key) and encrypt_random.
 * ========================================================================= */
int tai_crypto_derive_keys(uint8_t proto_ver,
                            const uint8_t *ikm, size_t ikm_len,
                            const uint8_t *encrypt_random, size_t rand_len,
                            uint8_t out_encrypt_key[32],
                            uint8_t out_sign_key[32],
                            const pal_t *pal)
{
    (void)proto_ver;
    (void)pal;

    if (!ikm || ikm_len == 0 || !encrypt_random || rand_len == 0
            || !out_encrypt_key || !out_sign_key)
        return TAI_ERR_ARGS;

    int rc = tai_hkdf_sha256(ikm, ikm_len,
                              encrypt_random, rand_len,
                              out_encrypt_key, 32);
    if (rc != 0) {
        TAI_LOGE(pal, TAG, "HKDF encrypt_key derivation failed: %d", rc);
        return TAI_ERR_CRYPTO;
    }

    rc = tai_hkdf_sha256(ikm, ikm_len,
                          encrypt_random, rand_len,
                          out_sign_key, 32);
    if (rc != 0) {
        TAI_LOGE(pal, TAG, "HKDF sign_key derivation failed: %d", rc);
        return TAI_ERR_CRYPTO;
    }

    return TAI_OK;
}
