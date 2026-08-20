/**
 * @file iot_ota_verify_test.c
 * @brief Unit tests for the OTA firmware digest verifier (iot_ota_verify_*).
 *
 * Known-answer vectors generated with Python:
 *   key  = b"test_secret_key_0123456789abcd"
 *   data = bytes((i * 7 + 3) & 0xFF for i in range(1000))
 *   md5_hex  = hashlib.md5(data).hexdigest()
 *   hmac_hex = hmac.new(key, hashlib.sha256(data).hexdigest().upper().encode(),
 *                       hashlib.sha256).hexdigest()
 *
 * No mock server needed: iot_client_t is a public struct, so a stack instance
 * with .pal and .secret_key set is sufficient.
 */

#include <stdio.h>
#include <string.h>

#include "iot_client.h"
#include "iot_ota.h"
#include "iot_config_defaults.h"

#define TEST_KEY  "test_secret_key_0123456789abcd"  /* 30 chars + NUL fits secret_key[32] */
#define DATA_LEN  1000

/* Known-answer vectors (see file header for generation recipe). */
#define KAT_MD5      "10046f077f2082ac19676b8079f1cb1a"
#define KAT_MD5_BAD  "7a0d252e3886d50cf0f0963874e887a3"
#define KAT_HMAC     "9fe946a5023ee5e5d00faf7ab3e67658a9e26291f4cfb453ae3a0c06a1808f63"
#define KAT_HMAC_UP  "9FE946A5023EE5E5D00FAF7AB3E67658A9E26291F4CFB453AE3A0C06A1808F63"
#define KAT_HMAC_BAD "e432f4036382824babfa9e2290b5143d7816ac9ae985f24d6fac065fd809daa8"

static int tests_run = 0;
static int tests_passed = 0;

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

static uint8_t g_data[DATA_LEN];
static iot_client_t g_client;

static void test_setup(void)
{
    for (int i = 0; i < DATA_LEN; i++) {
        g_data[i] = (uint8_t)((i * 7 + 3) & 0xFF);
    }
}

/* Corrupt one byte of a private copy; g_data stays intact so a test that
 * returns early cannot leave the shared image corrupted for later tests. */
static void corrupt_byte(uint8_t *image, size_t offset)
{
    image[offset] ^= 1;
}

static int test_hmac_match_single_update(void)
{
    iot_ota_upgrade_info_t info = { .hmac = (char *)KAT_HMAC };
    iot_ota_verify_ctx_t *ctx = NULL;

    int ret = iot_ota_verify_init(&g_client, &info, &ctx);
    if (ret != OPRT_OK || ctx == NULL) {
        printf("  init failed: %d\n", ret);
        return -1;
    }
    ret = iot_ota_verify_update(ctx, g_data, DATA_LEN);
    if (ret != OPRT_OK) {
        printf("  update failed: %d\n", ret);
        iot_ota_verify_abort(ctx);
        return -1;
    }
    ret = iot_ota_verify_finish(ctx);
    if (ret != OPRT_OK) {
        printf("  finish returned %d, expected OPRT_OK\n", ret);
        return -1;
    }
    return 0;
}

static int test_hmac_match_streamed(void)
{
    iot_ota_upgrade_info_t info = { .hmac = (char *)KAT_HMAC };
    iot_ota_verify_ctx_t *ctx = NULL;

    int ret = iot_ota_verify_init(&g_client, &info, &ctx);
    if (ret != OPRT_OK) {
        printf("  init failed: %d\n", ret);
        return -1;
    }

    /* Feed in irregular chunks: 1, 13, 64, 128, ... remainder. */
    size_t off = 0;
    size_t chunk = 1;
    while (off < DATA_LEN) {
        if (off + chunk > DATA_LEN) chunk = DATA_LEN - off;
        ret = iot_ota_verify_update(ctx, g_data + off, chunk);
        if (ret != OPRT_OK) {
            printf("  update failed at %zu: %d\n", off, ret);
            iot_ota_verify_abort(ctx);
            return -1;
        }
        off += chunk;
        chunk = chunk >= 128 ? 13 : chunk * 2;
    }

    ret = iot_ota_verify_finish(ctx);
    if (ret != OPRT_OK) {
        printf("  streamed finish returned %d, expected OPRT_OK\n", ret);
        return -1;
    }
    return 0;
}

static int test_hmac_mismatch(void)
{
    iot_ota_upgrade_info_t info = { .hmac = (char *)KAT_HMAC };
    iot_ota_verify_ctx_t *ctx = NULL;

    uint8_t image[DATA_LEN];
    memcpy(image, g_data, DATA_LEN);
    corrupt_byte(image, 500);

    int ret = iot_ota_verify_init(&g_client, &info, &ctx);
    if (ret != OPRT_OK) {
        printf("  init failed: %d\n", ret);
        return -1;
    }
    iot_ota_verify_update(ctx, image, DATA_LEN);
    ret = iot_ota_verify_finish(ctx);

    if (ret != OPRT_OTA_VERIFY_FAILED) {
        printf("  finish returned %d, expected OPRT_OTA_VERIFY_FAILED\n", ret);
        return -1;
    }
    return 0;
}

static int test_hmac_wrong_expected(void)
{
    /* Correct data, but the cloud hmac is for a different image. */
    iot_ota_upgrade_info_t info = { .hmac = (char *)KAT_HMAC_BAD };
    iot_ota_verify_ctx_t *ctx = NULL;

    int ret = iot_ota_verify_init(&g_client, &info, &ctx);
    if (ret != OPRT_OK) {
        printf("  init failed: %d\n", ret);
        return -1;
    }
    iot_ota_verify_update(ctx, g_data, DATA_LEN);
    ret = iot_ota_verify_finish(ctx);
    if (ret != OPRT_OTA_VERIFY_FAILED) {
        printf("  finish returned %d, expected OPRT_OTA_VERIFY_FAILED\n", ret);
        return -1;
    }
    return 0;
}

static int test_hmac_uppercase_expected(void)
{
    iot_ota_upgrade_info_t info = { .hmac = (char *)KAT_HMAC_UP };
    iot_ota_verify_ctx_t *ctx = NULL;

    int ret = iot_ota_verify_init(&g_client, &info, &ctx);
    if (ret != OPRT_OK) {
        printf("  init failed: %d\n", ret);
        return -1;
    }
    iot_ota_verify_update(ctx, g_data, DATA_LEN);
    ret = iot_ota_verify_finish(ctx);
    if (ret != OPRT_OK) {
        printf("  finish returned %d, expected OPRT_OK (case-insensitive)\n", ret);
        return -1;
    }
    return 0;
}

static int test_md5_match_and_mismatch(void)
{
    iot_ota_upgrade_info_t info = { .md5 = (char *)KAT_MD5 };
    iot_ota_verify_ctx_t *ctx = NULL;

    int ret = iot_ota_verify_init(&g_client, &info, &ctx);
    if (ret != OPRT_OK) {
        printf("  init failed: %d\n", ret);
        return -1;
    }
    iot_ota_verify_update(ctx, g_data, DATA_LEN);
    ret = iot_ota_verify_finish(ctx);
    if (ret != OPRT_OK) {
        printf("  md5 finish returned %d, expected OPRT_OK\n", ret);
        return -1;
    }

    /* Mismatch: correct data, wrong expected md5. */
    info.md5 = (char *)KAT_MD5_BAD;
    ret = iot_ota_verify_init(&g_client, &info, &ctx);
    if (ret != OPRT_OK) {
        printf("  init (bad) failed: %d\n", ret);
        return -1;
    }
    iot_ota_verify_update(ctx, g_data, DATA_LEN);
    ret = iot_ota_verify_finish(ctx);
    if (ret != OPRT_OTA_VERIFY_FAILED) {
        printf("  md5 finish returned %d, expected OPRT_OTA_VERIFY_FAILED\n", ret);
        return -1;
    }
    return 0;
}

static int test_no_digest_unsupported(void)
{
    iot_ota_upgrade_info_t info = {0};
    iot_ota_verify_ctx_t *ctx = NULL;

    int ret = iot_ota_verify_init(&g_client, &info, &ctx);
    if (ret != OPRT_NOT_SUPPORTED) {
        printf("  init returned %d, expected OPRT_NOT_SUPPORTED\n", ret);
        return -1;
    }
    return 0;
}

/* An empty hmac means the cloud configured none: fall through to the md5. */
static int test_empty_hmac_falls_back_to_md5(void)
{
    iot_ota_upgrade_info_t info = { .hmac = (char *)"", .md5 = (char *)KAT_MD5 };
    iot_ota_verify_ctx_t *ctx = NULL;

    int ret = iot_ota_verify_init(&g_client, &info, &ctx);
    if (ret != OPRT_OK || ctx == NULL) {
        printf("  init returned %d, expected OPRT_OK via md5\n", ret);
        return -1;
    }
    iot_ota_verify_update(ctx, g_data, DATA_LEN);
    ret = iot_ota_verify_finish(ctx);
    if (ret != OPRT_OK) {
        printf("  finish returned %d, expected OPRT_OK\n", ret);
        return -1;
    }

    /* Both digests empty is the same as neither being present. */
    info.md5 = (char *)"";
    ctx = NULL;
    ret = iot_ota_verify_init(&g_client, &info, &ctx);
    if (ret != OPRT_NOT_SUPPORTED) {
        printf("  empty md5+hmac returned %d, expected OPRT_NOT_SUPPORTED\n", ret);
        return -1;
    }
    return 0;
}

/* A non-empty hmac that is malformed must fail, never silently downgrade to
 * the weaker md5 the cloud also sent. */
static int test_malformed_hmac_never_downgrades(void)
{
    iot_ota_upgrade_info_t info = { .hmac = (char *)"deadbeef",
                                    .md5  = (char *)KAT_MD5 };
    iot_ota_verify_ctx_t *ctx = NULL;

    int ret = iot_ota_verify_init(&g_client, &info, &ctx);
    if (ret == OPRT_OK) {
        printf("  malformed hmac was downgraded to md5\n");
        iot_ota_verify_abort(ctx);
        return -1;
    }
    if (ret != OPRT_INVALID_PARAMETER) {
        printf("  returned %d, expected OPRT_INVALID_PARAMETER\n", ret);
        return -1;
    }
    return 0;
}

static int test_bad_length_rejected(void)
{
    iot_ota_verify_ctx_t *ctx = NULL;

    /* hmac too short */
    iot_ota_upgrade_info_t info = { .hmac = (char *)KAT_MD5 };  /* 32 chars, not 64 */
    int ret = iot_ota_verify_init(&g_client, &info, &ctx);
    if (ret != OPRT_INVALID_PARAMETER) {
        printf("  short hmac returned %d, expected OPRT_INVALID_PARAMETER\n", ret);
        return -1;
    }

    /* md5 too long */
    info.hmac = NULL;
    info.md5 = (char *)KAT_HMAC;  /* 64 chars, not 32 */
    ret = iot_ota_verify_init(&g_client, &info, &ctx);
    if (ret != OPRT_INVALID_PARAMETER) {
        printf("  long md5 returned %d, expected OPRT_INVALID_PARAMETER\n", ret);
        return -1;
    }
    return 0;
}

static int test_null_params(void)
{
    iot_ota_upgrade_info_t info = { .hmac = (char *)KAT_HMAC };
    iot_ota_verify_ctx_t *ctx = NULL;

    if (iot_ota_verify_init(NULL, &info, &ctx) != OPRT_INVALID_PARAMETER ||
        iot_ota_verify_init(&g_client, NULL, &ctx) != OPRT_INVALID_PARAMETER ||
        iot_ota_verify_init(&g_client, &info, NULL) != OPRT_INVALID_PARAMETER) {
        printf("  init NULL guards failed\n");
        return -1;
    }
    if (iot_ota_verify_update(NULL, g_data, DATA_LEN) != OPRT_INVALID_PARAMETER) {
        printf("  update NULL guard failed\n");
        return -1;
    }
    if (iot_ota_verify_finish(NULL) != OPRT_INVALID_PARAMETER) {
        printf("  finish NULL guard failed\n");
        return -1;
    }
    iot_ota_verify_abort(NULL);  /* must be a safe no-op */
    return 0;
}

static int test_abort_after_partial(void)
{
    iot_ota_upgrade_info_t info = { .hmac = (char *)KAT_HMAC };
    iot_ota_verify_ctx_t *ctx = NULL;

    int ret = iot_ota_verify_init(&g_client, &info, &ctx);
    if (ret != OPRT_OK) {
        printf("  init failed: %d\n", ret);
        return -1;
    }
    iot_ota_verify_update(ctx, g_data, 100);
    iot_ota_verify_abort(ctx);  /* must free without crashing (leaks-checked) */
    return 0;
}

int main(void)
{
    printf("========== OTA Verify Test Suite ==========\n");

    g_client.pal = get_default_pal();
    strcpy(g_client.secret_key, TEST_KEY);
    test_setup();

    RUN_TEST(test_hmac_match_single_update);
    RUN_TEST(test_hmac_match_streamed);
    RUN_TEST(test_hmac_mismatch);
    RUN_TEST(test_hmac_wrong_expected);
    RUN_TEST(test_hmac_uppercase_expected);
    RUN_TEST(test_md5_match_and_mismatch);
    RUN_TEST(test_no_digest_unsupported);
    RUN_TEST(test_empty_hmac_falls_back_to_md5);
    RUN_TEST(test_malformed_hmac_never_downgrades);
    RUN_TEST(test_bad_length_rejected);
    RUN_TEST(test_null_params);
    RUN_TEST(test_abort_after_partial);

    printf("\n========== Results: %d/%d passed ==========\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
