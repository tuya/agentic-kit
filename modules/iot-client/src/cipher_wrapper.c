#include "cipher_wrapper.h"
#include "iot_config_defaults.h"

#include <mbedtls/gcm.h>
#include <mbedtls/md5.h>
#include <string.h>
#include <stdio.h>

#include "rng.h"

#define P23_PV "2.3"
#define P23_PV_LEN 3
#define P23_SEQ_LEN 4
#define P23_FROM_LEN 4
#define P23_RESERVED_LEN 1
#define P23_AAD_LEN (P23_PV_LEN + P23_SEQ_LEN + P23_FROM_LEN + P23_RESERVED_LEN)  // 12
#define P23_IV_LEN 12
#define P23_TAG_LEN 16

#define AES_KEY_SIZE 16
#define GCM_NONCE_SIZE 12
#define GCM_TAG_SIZE 16
#define MAX_RESPONSE_SIZE 4096

int mbedtls_cipher_auth_encrypt_wrapper(const cipher_params_t *params,
                                        unsigned char *output, size_t *olen,
                                        unsigned char *tag, size_t tag_len)
{
    if (!params || !output || !olen || !tag) {
        return OPRT_INVALID_PARAMETER;
    }

    int key_bits = 0;
    if (params->cipher_type == CIPHER_TYPE_AES_128_GCM) {
        key_bits = 128;
    } else if (params->cipher_type == CIPHER_TYPE_AES_256_GCM) {
        key_bits = 256;
    } else {
        return OPRT_NOT_SUPPORTED;
    }

    mbedtls_gcm_context gcm;
    mbedtls_gcm_init(&gcm);

    int ret = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, (const unsigned char *)params->key, key_bits);
    if (ret != 0) {
        mbedtls_gcm_free(&gcm);
        return ret;
    }

    ret = mbedtls_gcm_crypt_and_tag(&gcm, MBEDTLS_GCM_ENCRYPT,
                                     params->data_len,
                                     params->nonce, params->nonce_len,
                                     params->ad, params->ad_len,
                                     params->data, output,
                                     tag_len, tag);

    *olen = params->data_len;

    mbedtls_gcm_free(&gcm);
    return ret;
}

int mbedtls_cipher_auth_decrypt_wrapper(const cipher_params_t *params,
                                        unsigned char *output, size_t *olen,
                                        const unsigned char *tag, size_t tag_len)
{
    if (!params || !output || !olen || !tag) {
        return OPRT_INVALID_PARAMETER;
    }

    int key_bits = 0;
    if (params->cipher_type == CIPHER_TYPE_AES_128_GCM) {
        key_bits = 128;
    } else if (params->cipher_type == CIPHER_TYPE_AES_256_GCM) {
        key_bits = 256;
    } else {
        return OPRT_NOT_SUPPORTED;
    }

    mbedtls_gcm_context gcm;
    mbedtls_gcm_init(&gcm);

    int ret = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, (const unsigned char *)params->key, key_bits);
    if (ret != 0) {
        mbedtls_gcm_free(&gcm);
        return ret;
    }

    ret = mbedtls_gcm_auth_decrypt(&gcm, params->data_len,
                                    params->nonce, params->nonce_len,
                                    params->ad, params->ad_len,
                                    tag, tag_len,
                                    params->data, output);

    if (ret == 0) {
        *olen = params->data_len;
    }

    mbedtls_gcm_free(&gcm);
    return ret;
}


// Pv2.3 protocol encryption
// Output format: | PV_23 (3) | Seq (4) | From (4) | Reserved (1) | IV (12) | Ciphertext (N) | Tag (16) |
int pv23_encrypt(const pal_t *pal, const uint8_t *plaintext, size_t plaintext_len,
    const uint8_t *key, uint8_t *output, size_t *output_len) {
    /* pal is required: rng_bytes() below uses it to lock the shared DRBG. */
    if (!pal || !key || !output || !output_len) {
        return OPRT_INVALID_PARAMETER;
    }

    if (plaintext_len > 0 && !plaintext) {
        return OPRT_INVALID_PARAMETER;
    }

    mbedtls_gcm_context gcm;
    uint8_t iv[P23_IV_LEN];
    int ret = OPRT_COMMUNICATION_ERROR;

    mbedtls_gcm_init(&gcm);

    /* IV — random nonce from the shared process-wide DRBG (common/rng.c); the
     * caller's pal locks that DRBG. */
    if (rng_bytes(pal, iv, P23_IV_LEN) != 0) {
        log_error("Failed to generate IV via RNG");
        mbedtls_gcm_free(&gcm);
        return OPRT_COMMUNICATION_ERROR;
    }

    // Set up GCM
    ret = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, key, 128);
    if (ret != 0) {
        log_error("Failed to set GCM key: -0x%04x", -ret);
        goto cleanup;
    }

    // Prepare AAD: PV_23 (3) + Seq (4) + From (4) + Reserved (1) = 12 bytes
    uint8_t aad[P23_AAD_LEN];
    memcpy(aad, P23_PV, P23_PV_LEN);           // "2.3"
    memset(aad + P23_PV_LEN, 0, P23_SEQ_LEN);  // Seq = 0
    memset(aad + P23_PV_LEN + P23_SEQ_LEN, 0, P23_FROM_LEN);  // From = 0
    memset(aad + P23_PV_LEN + P23_SEQ_LEN + P23_FROM_LEN, 0, P23_RESERVED_LEN);  // Reserved = 0

    // Prepare output buffer: AAD (12) + IV (12) + Ciphertext (N) + Tag (16)
    size_t offset = 0;
    memcpy(output + offset, aad, P23_AAD_LEN);
    offset += P23_AAD_LEN;
    memcpy(output + offset, iv, P23_IV_LEN);
    offset += P23_IV_LEN;

    // Encrypt and generate tag
    uint8_t tag[P23_TAG_LEN];
    ret = mbedtls_gcm_crypt_and_tag(&gcm, MBEDTLS_GCM_ENCRYPT,
                            plaintext_len, iv, P23_IV_LEN,
                            aad, P23_AAD_LEN,
                            plaintext, output + offset,
                            P23_TAG_LEN, tag);
    if (ret != 0) {
        log_error("Failed to encrypt: -0x%04x", -ret);
        goto cleanup;
    }

    // Append tag to output
    memcpy(output + offset + plaintext_len, tag, P23_TAG_LEN);
    *output_len = offset + plaintext_len + P23_TAG_LEN;

cleanup:
    mbedtls_gcm_free(&gcm);
    return ret;
}

// Pv2.3 protocol decryption
// Input format: | PV_23 (3) | Seq (4) | From (4) | Reserved (1) | IV (12) | Ciphertext (N) | Tag (16) |
int pv23_decrypt(const pal_t *pal, const uint8_t *ciphertext, size_t ciphertext_len,
    const uint8_t *key, uint8_t *output, size_t *output_len) {
    (void)pal;   /* decryption needs no RNG; pal is for symmetry with pv23_encrypt */
    if (!ciphertext || !key || !output || !output_len) {
        log_error("Invalid parameters for p23_decrypt");
        return OPRT_INVALID_PARAMETER;
    }

    if (ciphertext_len < P23_AAD_LEN + P23_IV_LEN + P23_TAG_LEN) {
        log_error("Ciphertext too short for P2.3 decryption: %u bytes", (unsigned)ciphertext_len);
        return OPRT_INVALID_PARAMETER;
    }

    // Extract AAD from message (first 12 bytes: PV_23 + Seq + From + Reserved)
    const uint8_t *aad = ciphertext;

    // Extract IV (next 12 bytes after AAD)
    const uint8_t *iv = ciphertext + P23_AAD_LEN;

    // Calculate encrypted data length
    size_t encrypted_len = ciphertext_len - (P23_AAD_LEN + P23_IV_LEN);
    const uint8_t *encrypted_data = ciphertext + P23_AAD_LEN + P23_IV_LEN;

    // Extract tag (last 16 bytes)
    const uint8_t *tag = encrypted_data + encrypted_len - P23_TAG_LEN;
    encrypted_len -= P23_TAG_LEN;

    // Set up GCM
    mbedtls_gcm_context gcm;
    mbedtls_gcm_init(&gcm);

    int ret = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, key, 128);
    if (ret != 0) {
        log_error("Failed to set GCM key for decryption: -0x%04x", -ret);
        mbedtls_gcm_free(&gcm);
        return ret;
    }

    // Decrypt and verify tag using AAD from message
    ret = mbedtls_gcm_auth_decrypt(&gcm, encrypted_len,
                            iv, P23_IV_LEN,
                            aad, P23_AAD_LEN,
                            tag, P23_TAG_LEN,
                            encrypted_data, output);

    if (ret == 0) {
        *output_len = encrypted_len;
    } else {
        log_error("Failed to decrypt: -0x%04x", -ret);
    }

    mbedtls_gcm_free(&gcm);
    return ret;
}

int iot_md5_password(const char *key, char *password_out)
{
    uint8_t md5_hash[16];
    mbedtls_md5_context md5_ctx;

    mbedtls_md5_init(&md5_ctx);
    int ret = mbedtls_md5_starts(&md5_ctx);
    if (ret == 0) ret = mbedtls_md5_update(&md5_ctx, (const uint8_t *)key, strlen(key));
    if (ret == 0) ret = mbedtls_md5_finish(&md5_ctx, md5_hash);
    mbedtls_md5_free(&md5_ctx);
    if (ret != 0) {
        log_error("MD5 computation failed: -0x%04x", -ret);
        return ret;
    }

    for (int i = 0; i < 8; i++)
        snprintf(password_out + i * 2, 3, "%02x", md5_hash[4 + i]);
    password_out[16] = '\0';
    return 0;
}
