#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "cipher_wrapper.h"
#include "iot_client.h"
#include "iot_config_defaults.h"
#include "rng.h"

static int tests_run = 0;
static int tests_passed = 0;

/* Real pal so rng_init()/rng_bytes() have a working mutex (rng now requires
 * one). The built-in default pal is fine for this single-threaded test. */
static const pal_t *g_test_pal;

#define RUN_TEST(fn)                                       \
    do {                                                   \
        tests_run++;                                       \
        printf("\n--- [%d] %s ---\n", tests_run, #fn);     \
        if ((fn)() == 0) {                                 \
            tests_passed++;                                \
            printf("  PASS\n");                            \
        } else {                                           \
            printf("  FAIL\n");                            \
        }                                                  \
    } while (0)

static int test_random_bytes(void)
{
    uint8_t buf1[32] = {0};
    uint8_t buf2[32] = {0};

    /* rng_init(NULL) above => no mutex; pal arg is unused, pass NULL. */
    int ret = rng_bytes(g_test_pal, buf1, sizeof(buf1));
    if (ret != 0) {
        printf("  rng_bytes failed: %d\n", ret);
        return -1;
    }

    ret = rng_bytes(g_test_pal, buf2, sizeof(buf2));
    if (ret != 0) {
        printf("  second rng_bytes failed: %d\n", ret);
        return -1;
    }

    if (memcmp(buf1, buf2, sizeof(buf1)) == 0) {
        printf("  two random outputs are identical — RNG broken\n");
        return -1;
    }

    uint8_t zeros[32] = {0};
    if (memcmp(buf1, zeros, sizeof(buf1)) == 0) {
        printf("  random output is all zeros\n");
        return -1;
    }

    return 0;
}

static int test_md5_password(void)
{
    char password[17] = {0};
    int ret = iot_md5_password("test_key_123", password);
    if (ret != 0) {
        printf("  iot_md5_password failed: %d\n", ret);
        return -1;
    }

    if (strlen(password) != 16) {
        printf("  expected 16-char password, got %zu\n", strlen(password));
        return -1;
    }

    for (int i = 0; i < 16; i++) {
        if (!((password[i] >= '0' && password[i] <= '9') ||
              (password[i] >= 'a' && password[i] <= 'f'))) {
            printf("  password char %d is not lowercase hex: '%c'\n", i, password[i]);
            return -1;
        }
    }

    char password2[17] = {0};
    ret = iot_md5_password("test_key_123", password2);
    if (ret != 0 || strcmp(password, password2) != 0) {
        printf("  same key produced different passwords\n");
        return -1;
    }

    char password3[17] = {0};
    ret = iot_md5_password("different_key", password3);
    if (ret != 0) {
        printf("  iot_md5_password failed for different key: %d\n", ret);
        return -1;
    }
    if (strcmp(password, password3) == 0) {
        printf("  different keys produced same password\n");
        return -1;
    }

    return 0;
}

static int test_pv23_encrypt_decrypt_roundtrip(void)
{
    const uint8_t key[16] = "0123456789abcdef";
    const char *plaintext = "Hello, PV2.3 encryption!";
    size_t plaintext_len = strlen(plaintext);

    uint8_t encrypted[256];
    size_t encrypted_len = 0;

    int ret = pv23_encrypt(g_test_pal, (const uint8_t *)plaintext, plaintext_len,
                           key, encrypted, &encrypted_len);
    if (ret != 0) {
        printf("  pv23_encrypt failed: %d\n", ret);
        return -1;
    }

    size_t expected_len = 12 + 12 + plaintext_len + 16;
    if (encrypted_len != expected_len) {
        printf("  unexpected encrypted length: %zu (expected %zu)\n",
               encrypted_len, expected_len);
        return -1;
    }

    if (memcmp(encrypted, "2.3", 3) != 0) {
        printf("  encrypted output missing PV2.3 header\n");
        return -1;
    }

    uint8_t decrypted[256];
    size_t decrypted_len = 0;

    ret = pv23_decrypt(g_test_pal, encrypted, encrypted_len, key, decrypted, &decrypted_len);
    if (ret != 0) {
        printf("  pv23_decrypt failed: %d\n", ret);
        return -1;
    }

    if (decrypted_len != plaintext_len) {
        printf("  decrypted length mismatch: %zu vs %zu\n", decrypted_len, plaintext_len);
        return -1;
    }

    if (memcmp(decrypted, plaintext, plaintext_len) != 0) {
        printf("  decrypted data doesn't match original\n");
        return -1;
    }

    return 0;
}

static int test_pv23_decrypt_wrong_key(void)
{
    const uint8_t key[16] = "0123456789abcdef";
    const uint8_t wrong_key[16] = "fedcba9876543210";
    const char *plaintext = "secret message";

    uint8_t encrypted[256];
    size_t encrypted_len = 0;

    int ret = pv23_encrypt(g_test_pal, (const uint8_t *)plaintext, strlen(plaintext),
                           key, encrypted, &encrypted_len);
    if (ret != 0) {
        printf("  pv23_encrypt failed: %d\n", ret);
        return -1;
    }

    uint8_t decrypted[256];
    size_t decrypted_len = 0;

    ret = pv23_decrypt(g_test_pal, encrypted, encrypted_len, wrong_key, decrypted, &decrypted_len);
    if (ret == 0) {
        printf("  pv23_decrypt should have failed with wrong key\n");
        return -1;
    }

    return 0;
}

static int test_pv23_decrypt_tampered(void)
{
    const uint8_t key[16] = "0123456789abcdef";
    const char *plaintext = "integrity check";

    uint8_t encrypted[256];
    size_t encrypted_len = 0;

    int ret = pv23_encrypt(g_test_pal, (const uint8_t *)plaintext, strlen(plaintext),
                           key, encrypted, &encrypted_len);
    if (ret != 0) {
        printf("  pv23_encrypt failed: %d\n", ret);
        return -1;
    }

    encrypted[30] ^= 0xFF;

    uint8_t decrypted[256];
    size_t decrypted_len = 0;

    ret = pv23_decrypt(g_test_pal, encrypted, encrypted_len, key, decrypted, &decrypted_len);
    if (ret == 0) {
        printf("  pv23_decrypt should have failed with tampered ciphertext\n");
        return -1;
    }

    return 0;
}

static int test_pv23_decrypt_too_short(void)
{
    const uint8_t key[16] = "0123456789abcdef";
    uint8_t short_data[10] = {0};
    uint8_t output[64];
    size_t output_len = 0;

    int ret = pv23_decrypt(g_test_pal, short_data, sizeof(short_data), key, output, &output_len);
    if (ret == 0) {
        printf("  pv23_decrypt should reject too-short input\n");
        return -1;
    }

    return 0;
}

static int test_pv23_null_params(void)
{
    const uint8_t key[16] = "0123456789abcdef";
    uint8_t buf[128];
    size_t len = 0;

    if (pv23_encrypt(g_test_pal, NULL, 0, key, buf, &len) != 0) {
        printf("  pv23_encrypt with empty plaintext should succeed... ");
    }

    if (pv23_decrypt(g_test_pal, NULL, 64, key, buf, &len) != OPRT_INVALID_PARAMETER) {
        printf("  pv23_decrypt(NULL, ...) should return OPRT_INVALID_PARAMETER\n");
        return -1;
    }

    if (pv23_encrypt(g_test_pal, (const uint8_t *)"x", 1, NULL, buf, &len) != OPRT_INVALID_PARAMETER) {
        printf("  pv23_encrypt with NULL key should return OPRT_INVALID_PARAMETER\n");
        return -1;
    }

    return 0;
}

static int test_pv23_empty_plaintext(void)
{
    const uint8_t key[16] = "0123456789abcdef";
    uint8_t encrypted[128];
    size_t encrypted_len = 0;

    int ret = pv23_encrypt(g_test_pal, NULL, 0, key, encrypted, &encrypted_len);
    if (ret != 0) {
        printf("  pv23_encrypt with empty plaintext failed: %d\n", ret);
        return -1;
    }

    size_t expected_len = 12 + 12 + 0 + 16;
    if (encrypted_len != expected_len) {
        printf("  unexpected length for empty plaintext: %zu (expected %zu)\n",
               encrypted_len, expected_len);
        return -1;
    }

    uint8_t decrypted[128];
    size_t decrypted_len = 0;

    ret = pv23_decrypt(g_test_pal, encrypted, encrypted_len, key, decrypted, &decrypted_len);
    if (ret != 0) {
        printf("  pv23_decrypt empty ciphertext failed: %d\n", ret);
        return -1;
    }

    if (decrypted_len != 0) {
        printf("  expected 0 decrypted bytes, got %zu\n", decrypted_len);
        return -1;
    }

    return 0;
}

static int test_gcm_encrypt_decrypt_wrapper(void)
{
    const unsigned char key[16] = "0123456789abcdef";
    const unsigned char nonce[12] = "unique_nonce";
    const unsigned char aad[] = "additional data";
    const char *plaintext = "GCM wrapper test";
    size_t plaintext_len = strlen(plaintext);

    cipher_params_t params = {
        .cipher_type = CIPHER_TYPE_AES_128_GCM,
        .key = key,
        .key_len = sizeof(key),
        .nonce = nonce,
        .nonce_len = sizeof(nonce),
        .ad = aad,
        .ad_len = sizeof(aad),
        .data = (const unsigned char *)plaintext,
        .data_len = plaintext_len,
    };

    unsigned char encrypted[256];
    size_t olen = 0;
    unsigned char tag[16];

    int ret = mbedtls_cipher_auth_encrypt_wrapper(&params, encrypted, &olen, tag, sizeof(tag));
    if (ret != 0) {
        printf("  encrypt_wrapper failed: %d\n", ret);
        return -1;
    }
    if (olen != plaintext_len) {
        printf("  encrypt output length %zu != %zu\n", olen, plaintext_len);
        return -1;
    }

    cipher_params_t dec_params = params;
    dec_params.data = encrypted;
    dec_params.data_len = olen;

    unsigned char decrypted[256];
    size_t dec_len = 0;

    ret = mbedtls_cipher_auth_decrypt_wrapper(&dec_params, decrypted, &dec_len, tag, sizeof(tag));
    if (ret != 0) {
        printf("  decrypt_wrapper failed: %d\n", ret);
        return -1;
    }
    if (dec_len != plaintext_len || memcmp(decrypted, plaintext, plaintext_len) != 0) {
        printf("  decrypt mismatch\n");
        return -1;
    }

    return 0;
}

static int test_gcm_wrapper_null_params(void)
{
    unsigned char buf[64];
    size_t len = 0;
    unsigned char tag[16];

    if (mbedtls_cipher_auth_encrypt_wrapper(NULL, buf, &len, tag, 16) != OPRT_INVALID_PARAMETER) {
        printf("  encrypt_wrapper(NULL) should return OPRT_INVALID_PARAMETER\n");
        return -1;
    }

    if (mbedtls_cipher_auth_decrypt_wrapper(NULL, buf, &len, tag, 16) != OPRT_INVALID_PARAMETER) {
        printf("  decrypt_wrapper(NULL) should return OPRT_INVALID_PARAMETER\n");
        return -1;
    }

    return 0;
}

static int test_gcm_wrapper_unsupported_cipher(void)
{
    const unsigned char key[16] = "0123456789abcdef";
    cipher_params_t params = {
        .cipher_type = 99,
        .key = key,
        .key_len = sizeof(key),
        .nonce = key,
        .nonce_len = 12,
        .ad = NULL,
        .ad_len = 0,
        .data = key,
        .data_len = 1,
    };

    unsigned char buf[64];
    size_t len = 0;
    unsigned char tag[16];

    if (mbedtls_cipher_auth_encrypt_wrapper(&params, buf, &len, tag, 16) != OPRT_NOT_SUPPORTED) {
        printf("  should return OPRT_NOT_SUPPORTED for invalid cipher type\n");
        return -1;
    }

    return 0;
}

static int test_pv23_unique_iv(void)
{
    const uint8_t key[16] = "0123456789abcdef";
    const char *plaintext = "same data";

    uint8_t enc1[128], enc2[128];
    size_t len1 = 0, len2 = 0;

    int ret = pv23_encrypt(g_test_pal, (const uint8_t *)plaintext, strlen(plaintext),
                           key, enc1, &len1);
    if (ret != 0) return -1;

    ret = pv23_encrypt(g_test_pal, (const uint8_t *)plaintext, strlen(plaintext),
                       key, enc2, &len2);
    if (ret != 0) return -1;

    if (len1 != len2) {
        printf("  lengths differ: %zu vs %zu\n", len1, len2);
        return -1;
    }

    if (memcmp(enc1 + 12, enc2 + 12, 12) == 0) {
        printf("  two encryptions produced identical IVs — nonce reuse!\n");
        return -1;
    }

    if (memcmp(enc1, enc2, len1) == 0) {
        printf("  two encryptions produced identical output\n");
        return -1;
    }

    return 0;
}

int main(void)
{
    printf("========== Cipher Test Suite ==========\n");

    /* Seed the shared DRBG up front, with a real pal for the rng mutex. */
    g_test_pal = get_default_pal();
    if (rng_init(g_test_pal) != 0) {
        printf("rng_init() failed — cannot run cipher tests\n");
        return 1;
    }

    RUN_TEST(test_random_bytes);
    RUN_TEST(test_md5_password);
    RUN_TEST(test_pv23_encrypt_decrypt_roundtrip);
    RUN_TEST(test_pv23_decrypt_wrong_key);
    RUN_TEST(test_pv23_decrypt_tampered);
    RUN_TEST(test_pv23_decrypt_too_short);
    RUN_TEST(test_pv23_null_params);
    RUN_TEST(test_pv23_empty_plaintext);
    RUN_TEST(test_pv23_unique_iv);
    RUN_TEST(test_gcm_encrypt_decrypt_wrapper);
    RUN_TEST(test_gcm_wrapper_null_params);
    RUN_TEST(test_gcm_wrapper_unsupported_cipher);

    printf("\n========== Results: %d/%d passed ==========\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
