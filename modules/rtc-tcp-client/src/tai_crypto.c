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
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"

#define TAG "crypto"

/* =========================================================================
 * Module-level DRBG (lazily initialized, shared by crypto + TLS)
 * ========================================================================= */
static mbedtls_entropy_context  g_entropy;
static mbedtls_ctr_drbg_context g_drbg;
static int                      g_drbg_ready = 0;

static int ensure_drbg(void)
{
    if (g_drbg_ready) return 0;
    mbedtls_entropy_init(&g_entropy);
    mbedtls_ctr_drbg_init(&g_drbg);
    static const unsigned char pers[] = "tuya_ai_sdk";
    int rc = mbedtls_ctr_drbg_seed(&g_drbg, mbedtls_entropy_func,
                                    &g_entropy, pers, sizeof(pers) - 1);
    if (rc != 0) return rc;
    g_drbg_ready = 1;
    return 0;
}

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

/* =========================================================================
 * tai_random_bytes
 * ========================================================================= */
int tai_random_bytes(uint8_t *buf, size_t len)
{
    if (ensure_drbg() != 0) return -1;
    return mbedtls_ctr_drbg_random(&g_drbg, buf, len) == 0 ? 0 : -1;
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
    if (rc != 0) return TAI_ERR_CRYPTO;

    rc = tai_hkdf_sha256(ikm, ikm_len,
                          encrypt_random, rand_len,
                          out_sign_key, 32);
    if (rc != 0) return TAI_ERR_CRYPTO;

    return TAI_OK;
}
