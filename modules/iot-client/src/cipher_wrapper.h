#ifndef __CIPHER_WRAPPER_H__
#define __CIPHER_WRAPPER_H__

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "iot_client.h"

typedef enum {
    CIPHER_TYPE_AES_128_GCM = 0,
    CIPHER_TYPE_AES_256_GCM = 1
} cipher_type_t;

typedef struct {
    cipher_type_t cipher_type;
    const unsigned char *key;
    size_t key_len;
    const unsigned char *nonce;
    size_t nonce_len;
    const unsigned char *ad;
    size_t ad_len;
    const unsigned char *data;
    size_t data_len;
} cipher_params_t;

/**
 * @brief Authenticated encryption wrapper
 * 
 * @param params Cipher parameters
 * @param output Output buffer for encrypted data
 * @param olen Output length
 * @param tag Authentication tag output
 * @param tag_len Tag length
 * @return int 0 on success
 */
int mbedtls_cipher_auth_encrypt_wrapper(const cipher_params_t *params,
                                        unsigned char *output, size_t *olen,
                                        unsigned char *tag, size_t tag_len);

/**
 * @brief Authenticated decryption wrapper
 * 
 * @param params Cipher parameters
 * @param output Output buffer for decrypted data
 * @param olen Output length
 * @param tag Authentication tag
 * @param tag_len Tag length
 * @return int 0 on success
 */
int mbedtls_cipher_auth_decrypt_wrapper(const cipher_params_t *params,
                                        unsigned char *output, size_t *olen,
                                        const unsigned char *tag, size_t tag_len);

/**
 * @brief Pv2.3 protocol encryption
 *
 * Encrypts plaintext using AES-GCM with protocol version 2.3
 *
 * @param plaintext Plain text data
 * @param plaintext_len Length of plaintext
 * @param key Encryption key (16 bytes)
 * @param output Buffer to store encrypted data
 * @param output_len Pointer to store output length
 * @return int 0 on success, -1 on failure
 */
int pv23_encrypt(const uint8_t *plaintext, size_t plaintext_len,
    const uint8_t *key, uint8_t *output, size_t *output_len);

/**
 * @brief Pv2.3 protocol decryption
 *
 * Decrypts data using AES-GCM with protocol version 2.3
 *
 * @param ciphertext Encrypted data
 * @param ciphertext_len Length of ciphertext
 * @param key Decryption key (16 bytes)
 * @param output Buffer to store decrypted data
 * @param output_len Pointer to store output length
 * @return int 0 on success, -1 on failure
 */
int pv23_decrypt(const uint8_t *ciphertext, size_t ciphertext_len,
    const uint8_t *key, uint8_t *output, size_t *output_len);

/**
 * @brief Generate cryptographically random bytes via mbedtls CTR-DRBG.
 *
 * @param output Output buffer
 * @param len Number of bytes to generate
 * @return 0 on success, non-zero on failure
 */
int iot_random_bytes(uint8_t *output, size_t len);

/**
 * @brief Compute MQTT password from a key: MD5(key) bytes 4..11 as 16-char hex string.
 *
 * @param key Input key string
 * @param password_out Output buffer (at least 17 bytes)
 * @return int 0 on success
 */
int iot_md5_password(const char *key, char *password_out);

#endif /* __CIPHER_WRAPPER_H__ */

