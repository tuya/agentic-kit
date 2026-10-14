/*
 * tests/test_core.c — Unit tests for the Tuya AI Foundation C library.
 *
 * Tests attr encoding/decoding, packet encoding/decoding, transport
 * framing, HMAC, fragmentation, and varint.  No network or TLS required.
 *
 * Build & run:
 *   cmake --build build && ./build/tai_unit_tests
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <assert.h>

/* Pull in the full internal header so we can call internal functions */
#include "../src/tai_internal.h"
#include "rng.h"
#include "test_log.h"
#include "mbedtls/base64.h"

/* -------------------------------------------------------------------------
 * Minimal stub PAL.  TLS and crypto are no longer PAL responsibilities --
 * the SDK uses mbedTLS internally -- so the stub only needs time/memory/log.
 * ------------------------------------------------------------------------- */
static uint64_t stub_time(void) { return 1700000000000ULL; }
static void *stub_malloc(size_t s) { return malloc(s); }
static void  stub_free(void *p)    { free(p); }
/* Trivial single-threaded mutex stubs: the unit tests run on one thread, but
 * rng_init() now requires a pal that can create a mutex. */
static int   g_stub_mutex;
static void *stub_mutex_create(void)    { return &g_stub_mutex; }
static void  stub_mutex_lock(void *h)   { (void)h; }
static void  stub_mutex_unlock(void *h) { (void)h; }
static void  stub_mutex_destroy(void *h){ (void)h; }

static const pal_t g_stub_pal = {
    .tcp_connect      = NULL,
    .tcp_send         = NULL,
    .tcp_recv         = NULL,
    .tcp_close        = NULL,
    .tcp_poll         = NULL,
    .time_ms          = stub_time,
    .malloc           = stub_malloc,
    .free             = stub_free,
    .mutex_create     = stub_mutex_create,
    .mutex_lock       = stub_mutex_lock,
    .mutex_unlock     = stub_mutex_unlock,
    .mutex_destroy    = stub_mutex_destroy,
    .thread_create    = NULL,
    .thread_join      = NULL,
    .sleep_ms         = NULL,
};

/* -------------------------------------------------------------------------
 * Test framework
 * ------------------------------------------------------------------------- */
static int g_pass = 0, g_fail = 0;
static int g_log_arg_evals = 0;

#define CHECK(expr)                                               \
    do {                                                          \
        if (!(expr)) {                                            \
            fprintf(stderr, "  FAIL  %s:%d  %s\n",               \
                    __FILE__, __LINE__, #expr);                   \
            g_fail++;                                             \
        } else {                                                  \
            g_pass++;                                             \
        }                                                         \
    } while (0)

#define TEST(name) do { printf("  %-50s", name); } while (0)
#define PASS()     do { printf("OK\n"); } while (0)

static size_t counted_zero(void)
{
    g_log_arg_evals++;
    return 0;
}

static void test_compile_time_log_ceiling(void)
{
    TEST("compile-time log ceiling gates packet logs");

    g_log_arg_evals = 0;
    test_log_count_reset();
    test_log_set_mode(TEST_LOG_COUNT);

    TAI_LOGI((g_log_arg_evals++, &g_stub_pal), "test", "compiled log");
    tai_log_packet(TAI_VER_21, 1, TAI_PKT_SESSION_CLOSE,
                   NULL, 0, NULL, counted_zero());

#if AGENTIC_KIT_TAI_LOG_LEVEL >= 3
    CHECK(g_log_arg_evals == 2);
    CHECK(test_log_count_get() == 2);
#else
    CHECK(g_log_arg_evals == 0);
    CHECK(test_log_count_get() == 0);
#endif

#if AGENTIC_KIT_LOG_LEVEL >= 3 && AGENTIC_KIT_TAI_LOG_LEVEL < 3
    /* Module ceiling lowered below a still-open SDK-wide ceiling (built as
     * tai_log_module_zero_tests): the module vocabulary above is compiled
     * out by the TAI knob, while the facade itself still dispatches -- a
     * per-module knob only lowers its own module, it is not a second
     * global gate. */
    log_tag_info("test", "module ceiling does not touch the facade");
    CHECK(g_log_arg_evals == 0);
    CHECK(test_log_count_get() == 1);
#endif

    test_log_set_mode(TEST_LOG_PASSTHROUGH);
    PASS();
}

/* -------------------------------------------------------------------------
 * 1. Varint encode/decode
 * ------------------------------------------------------------------------- */
static void test_varint(void)
{
    printf("\n[varint]\n");
    uint8_t buf[8];
    int len;
    uint32_t val;
    size_t consumed;

    TEST("encode 0");
    len = tai_varint_encode(0, buf, sizeof(buf));
    CHECK(len == 1); CHECK(buf[0] == 0x00);
    PASS();

    TEST("encode 1");
    len = tai_varint_encode(1, buf, sizeof(buf));
    CHECK(len == 1); CHECK(buf[0] == 0x01);
    PASS();

    TEST("encode 127");
    len = tai_varint_encode(127, buf, sizeof(buf));
    CHECK(len == 1); CHECK(buf[0] == 0x7F);
    PASS();

    TEST("encode 128");
    len = tai_varint_encode(128, buf, sizeof(buf));
    CHECK(len == 2); CHECK(buf[0] == 0x80); CHECK(buf[1] == 0x01);
    PASS();

    TEST("encode 300");
    len = tai_varint_encode(300, buf, sizeof(buf));
    CHECK(len == 2); CHECK(buf[0] == 0xAC); CHECK(buf[1] == 0x02);
    PASS();

    TEST("roundtrip 16383");
    len = tai_varint_encode(16383, buf, sizeof(buf));
    CHECK(tai_varint_decode(buf, (size_t)len, &val, &consumed) == TAI_OK);
    CHECK(val == 16383); CHECK(consumed == (size_t)len);
    PASS();

    TEST("roundtrip 2097151");
    len = tai_varint_encode(2097151, buf, sizeof(buf));
    CHECK(tai_varint_decode(buf, (size_t)len, &val, &consumed) == TAI_OK);
    CHECK(val == 2097151);
    PASS();
}

/* -------------------------------------------------------------------------
 * 2. Attribute block encode/decode (v2.1)
 * ------------------------------------------------------------------------- */
static void test_attrs_v21(void)
{
    printf("\n[attrs v2.1]\n");
    uint8_t buf[256];
    tai_attr_t out[8];
    int count;

    /* Build two attrs: uint8 client-type=1, string client-id="testid" */
    uint8_t s_u8[1];
    tai_attr_t attrs[2];
    attrs[0] = tai_attr_u8v(TAI_ATTR_CLIENT_TYPE, s_u8, 1);
    attrs[1] = tai_attr_strv(TAI_ATTR_CLIENT_ID, "testid");

    TEST("encode 2 attrs");
    int written = tai_attrs_encode_block(TAI_VER_21, attrs, 2, buf, sizeof(buf));
    /* Expected: 4 (total_len prefix)
     *         + 4+1 (type:2 + len:2 + u8:1)
     *         + 4+6 (type:2 + len:2 + "testid":6)
     *         = 4 + 5 + 10 = 19 */
    CHECK(written == 19);
    /* Check total_len field = 15 */
    uint32_t block_len = ((uint32_t)buf[0]<<24)|((uint32_t)buf[1]<<16)|
                         ((uint32_t)buf[2]<<8)|buf[3];
    CHECK(block_len == 15);
    PASS();

    TEST("decode roundtrip");
    int consumed = tai_attrs_decode_block(TAI_VER_21, buf, (size_t)written,
                                           out, 8, &count);
    CHECK(consumed == 19);
    CHECK(count == 2);
    CHECK(out[0].type == TAI_ATTR_CLIENT_TYPE);
    CHECK(out[0].len  == 1);
    CHECK(tai_attr_u8(&out[0]) == 1);
    CHECK(out[1].type == TAI_ATTR_CLIENT_ID);
    CHECK(out[1].len  == 6);
    CHECK(memcmp(tai_attr_str(&out[1]), "testid", 6) == 0);
    PASS();

    TEST("attr_find present");
    const tai_attr_t *found = tai_attr_find(out, count, TAI_ATTR_CLIENT_ID);
    CHECK(found != NULL);
    CHECK(found->len == 6);
    PASS();

    TEST("attr_find absent");
    found = tai_attr_find(out, count, TAI_ATTR_SESSION_ID);
    CHECK(found == NULL);
    PASS();
}

/* -------------------------------------------------------------------------
 * 4. Packet encode/decode (v2.1)
 * ------------------------------------------------------------------------- */
static void test_packet_v21(void)
{
    printf("\n[packet v2.1]\n");
    uint8_t buf[512];

    /* Encode a Ping with one uint64 attr and no payload */
    uint8_t s_ts[8];
    tai_attr_t attrs[1];
    attrs[0] = tai_attr_u64v(TAI_ATTR_CLIENT_TIMESTAMP, s_ts, 0xDEADBEEFCAFEBABEULL);

    TEST("encode Ping");
    int written = tai_packet_encode(TAI_VER_21, TAI_PKT_PING,
                                     attrs, 1, NULL, 0, buf, sizeof(buf));
    CHECK(written > 0);
    /* header byte = (4<<1)|1 = 0x09 */
    CHECK(buf[0] == 0x09);
    PASS();

    TEST("decode Ping roundtrip");
    uint8_t pkt_type;
    tai_attr_t out_attrs[8];
    int out_count;
    const uint8_t *payload;
    size_t payload_len;
    int rc = tai_packet_decode(TAI_VER_21, buf, (size_t)written,
                                &pkt_type, out_attrs, 8, &out_count,
                                &payload, &payload_len);
    CHECK(rc == TAI_OK);
    CHECK(pkt_type == TAI_PKT_PING);
    CHECK(out_count == 1);
    CHECK(out_attrs[0].type == TAI_ATTR_CLIENT_TIMESTAMP);
    CHECK(tai_attr_u64(&out_attrs[0]) == 0xDEADBEEFCAFEBABEULL);
    CHECK(payload_len == 0);
    PASS();
}

/* -------------------------------------------------------------------------
 * 5. Transport frame encode/decode + HMAC
 * ------------------------------------------------------------------------- */
static void test_transport(void)
{
    printf("\n[transport]\n");
    uint8_t app[8]  = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
    uint8_t frame[256];
    uint8_t sign_key[32];
    memset(sign_key, 0xAB, 32);

    TEST("version detection v2.1");
    /* flags byte = 0x22: frag=0, reserve=2, ver=2 */
    CHECK(tai_frame_detect_version(0x22) == TAI_VER_21);
    CHECK(tai_frame_detect_version(0xFF) == TAI_ERR_PROTO);
    PASS();

    TEST("encode frame (sig_len=32)");
    int flen = tai_frame_encode(TAI_FRAG_NONE, 1,
                                 app, sizeof(app),
                                 sign_key, 32, &g_stub_pal,
                                 frame, sizeof(frame));
    /* Expected: 5 + 8 + 32 = 45 */
    CHECK(flen == 45);
    /* flags byte */
    CHECK((frame[0] & 0x0F) == 0x02);           /* version nibble */
    CHECK(((frame[0] >> 6) & 0x03) == 0x00);    /* frag = NONE */
    /* seq = 1 */
    CHECK(frame[1] == 0x00); CHECK(frame[2] == 0x01);
    /* length = 8+32 = 40 */
    CHECK(frame[3] == 0x00); CHECK(frame[4] == 0x28);
    PASS();

    TEST("frame_total_size");
    CHECK(tai_frame_total_size(frame, 5) == 45);
    PASS();

    TEST("decode frame");
    uint8_t frag;
    uint16_t seq;
    const uint8_t *payload;
    size_t payload_len;
    int rc = tai_frame_decode(frame, (size_t)flen, 32,
                               &frag, &seq, &payload, &payload_len);
    CHECK(rc == TAI_OK);
    CHECK(frag == TAI_FRAG_NONE);
    CHECK(seq  == 1);
    CHECK(payload_len == 8);
    CHECK(memcmp(payload, app, 8) == 0);
    PASS();

    TEST("verify HMAC (correct key)");
    rc = tai_frame_verify(frame, (size_t)flen, 32, sign_key, &g_stub_pal);
    CHECK(rc == TAI_OK);
    PASS();

    TEST("verify HMAC (wrong key)");
    uint8_t bad_key[32];
    memset(bad_key, 0x00, 32);
    rc = tai_frame_verify(frame, (size_t)flen, 32, bad_key, &g_stub_pal);
    CHECK(rc == TAI_ERR_HMAC);
    PASS();

    TEST("encode frame (sig_len=0, ClientHello style)");
    flen = tai_frame_encode(TAI_FRAG_NONE, 1,
                             app, sizeof(app),
                             sign_key, 0, &g_stub_pal,
                             frame, sizeof(frame));
    /* 5 + 8 + 0 = 13 */
    CHECK(flen == 13);
    PASS();
}

/* -------------------------------------------------------------------------
 * 7. Packet payload helpers
 * ------------------------------------------------------------------------- */
static void test_payloads(void)
{
    printf("\n[payload helpers]\n");
    uint8_t buf[64];

    TEST("pack_event Start (v2.1)");
    int len = tai_pack_event(TAI_VER_21, TAI_EVT_START, NULL, 0, buf, sizeof(buf));
    CHECK(len == 2);
    CHECK(buf[0] == 0x00); CHECK(buf[1] == 0x00);
    PASS();

    TEST("unpack_event Start (v2.1)");
    uint16_t evt_type;
    const uint8_t *data;
    size_t data_len;
    int rc = tai_unpack_event(TAI_VER_21, buf, (size_t)len,
                               &evt_type, &data, &data_len);
    CHECK(rc == TAI_OK);
    CHECK(evt_type == TAI_EVT_START);
    CHECK(data_len == 0);
    PASS();

    TEST("pack_event MCPCmd with data (v2.1)");
    const uint8_t mcp_data[] = {'{', '}' };
    len = tai_pack_event(TAI_VER_21, TAI_EVT_MCP_CMD, mcp_data, 2, buf, sizeof(buf));
    CHECK(len == 4);  /* 2 byte event_type + 2 bytes data */
    uint16_t et = ((uint16_t)buf[0]<<8)|buf[1];
    CHECK(et == TAI_EVT_MCP_CMD);
    PASS();

    TEST("pack_media_hdr v2.1");
    len = tai_pack_media_hdr(TAI_VER_21, 1, TAI_STREAM_MIDDLE,
                              1700000000000ULL, buf, sizeof(buf));
    CHECK(len == 8);
    /* data_id = 1 */
    uint16_t did = ((uint16_t)buf[0]<<8)|buf[1];
    CHECK(did == 1);
    /* flag in bits 47-46 of packed 48: (buf[2] >> 6) & 0x03 */
    uint8_t flag = (buf[2] >> 6) & 0x03;
    CHECK(flag == TAI_STREAM_MIDDLE);
    PASS();

    TEST("pack_text_hdr v2.1");
    len = tai_pack_text_hdr(TAI_VER_21, 3, TAI_STREAM_ONE_SHOT, 5, buf, sizeof(buf));
    CHECK(len >= 4); /* 2 id + 1 flags + 1+ varint */
    did = ((uint16_t)buf[0]<<8)|buf[1];
    CHECK(did == 3);
    flag = (buf[2] >> 6) & 0x03;
    CHECK(flag == TAI_STREAM_ONE_SHOT);
    PASS();
}

/* -------------------------------------------------------------------------
 * 8. Key derivation (HKDF)
 * ------------------------------------------------------------------------- */
static void test_crypto(void)
{
    printf("\n[crypto / HKDF]\n");

    /* Use a known IKM and salt, derive 32 bytes, verify non-zero */
    const uint8_t ikm[]  = "test-local-key-16";
    const uint8_t salt[] = {
        0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,
        0x09,0x0A,0x0B,0x0C,0x0D,0x0E,0x0F,0x10,
        0x11,0x12,0x13,0x14,0x15,0x16,0x17,0x18,
        0x19,0x1A,0x1B,0x1C,0x1D,0x1E,0x1F,0x20
    };
    uint8_t enc_key[32], sign_key[32];

    TEST("derive_keys v2.1");
    int rc = tai_crypto_derive_keys(TAI_VER_21,
                                     ikm, sizeof(ikm) - 1,
                                     salt, sizeof(salt),
                                     enc_key, sign_key, &g_stub_pal);
    CHECK(rc == TAI_OK);

    /* Keys must be non-zero */
    uint8_t zero[32] = {0};
    CHECK(memcmp(enc_key,  zero, 32) != 0);
    CHECK(memcmp(sign_key, zero, 32) != 0);

    /* In v2.1, both keys are derived from same IKM+salt → must be identical */
    CHECK(memcmp(enc_key, sign_key, 32) == 0);
    PASS();
}

/* Independent synthetic vectors, generated with Python cryptography AESGCM
 * and cross-checked with PyCryptodome AES.MODE_GCM (16-byte nonce/tag, no AAD).
 * First plaintext: bytes([0,255,128,1]) + bytes(range(33)); key: range(16),
 * IV: range(16,32). Other vectors use zero key/IV and 17 or 0 zero bytes.
 * These are primitive fixtures, NOT evidence for any image wire profile. */
static const uint8_t image_key[16] = {
    0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15
};
static const uint8_t image_iv[16] = {
    16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
};
static const uint8_t image_plain[37] = {
    0,255,128,1,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,
    15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32
};
static const uint8_t image_body[53] = {
    0xc5,0x61,0x77,0xb7,0x7d,0xb3,0x26,0x8b,0xd2,0x1d,0xc5,0x85,0xa9,0x26,0x25,0xdc,
    0xf9,0xf0,0x2e,0x90,0x7d,0x17,0xb5,0xeb,0xbd,0x93,0x91,0x81,0x5b,0x5a,0x69,0xfb,
    0x3f,0x39,0x75,0x18,0xc1,0xe0,0x83,0x79,0x48,0xa8,0x6f,0x6a,0xde,0xeb,0x9f,0x2f,
    0x76,0x14,0x26,0x8f,0x70
};

static void test_image_gcm(void)
{
    TEST("image GCM independent vectors and authentication");
    uint8_t out[38], zero[38] = {0};
    memset(out, 0xa5, sizeof(out));
    CHECK(tai_image_gcm_decrypt(image_key, image_iv, image_body,
                               sizeof(image_body), out, 37) == TAI_OK);
    CHECK(memcmp(out, image_plain, 37) == 0);
    CHECK(out[37] == 0xa5);

    static const uint8_t zero_body[33] = {
        0xa3,0xb2,0x2b,0x84,0x49,0xaf,0xaf,0xbc,0xd6,0xc0,0x9f,0x2c,0xfa,0x9d,0xe2,0xbe,
        0x93,0x5d,0x63,0x0c,0x18,0xef,0x6c,0x58,0x70,0xf0,0x97,0x45,0x51,0xd6,0x5f,0x20,0x23
    };
    static const uint8_t empty_tag[16] = {
        0xe8,0x23,0xb7,0xf1,0xa1,0xd3,0xf1,0xa0,0x46,0x2e,0xbd,0xb2,0xca,0xe3,0xb3,0x50
    };
    CHECK(tai_image_gcm_decrypt(zero, zero, zero_body, sizeof(zero_body),
                               out, sizeof(out)) == TAI_OK);
    CHECK(memcmp(out, zero, 17) == 0);
    out[0] = 0xa5;
    CHECK(tai_image_gcm_decrypt(zero, zero, empty_tag, 16, out, 0) == TAI_OK);
    CHECK(out[0] == 0xa5);
    uint8_t bad_tag[16];
    memcpy(bad_tag, empty_tag, 16);
    bad_tag[0] ^= 1;
    CHECK(tai_image_gcm_decrypt(zero, zero, bad_tag, 16, out, 0) == TAI_ERR_CRYPTO);
    CHECK(out[0] == 0xa5);

    /* Flip every byte in each authenticated input, including all tag bytes. */
    for (size_t i = 0; i < 16 + 16 + sizeof(image_body); i++) {
        uint8_t key[16], iv[16], body[sizeof(image_body)];
        memcpy(key, image_key, sizeof(key));
        memcpy(iv, image_iv, sizeof(iv));
        memcpy(body, image_body, sizeof(body));
        if (i < 16) key[i] ^= 1;
        else if (i < 32) iv[i - 16] ^= 1;
        else body[i - 32] ^= 1;
        memset(out, 0xa5, sizeof(out));
        CHECK(tai_image_gcm_decrypt(key, iv, body, sizeof(body),
                                   out, sizeof(out)) == TAI_ERR_CRYPTO);
        CHECK(memcmp(out, zero, 37) == 0);
        CHECK(out[37] == 0xa5);
    }
    PASS();
}

static void test_image_gcm_encrypt(void)
{
    TEST("image GCM encrypt independent vectors and bounds");
    uint8_t out[54], sentinel[54];
    memset(out, 0xa5, sizeof(out));
    CHECK(tai_image_gcm_encrypt(image_key, image_iv, image_plain, 37,
                               out, 53) == TAI_OK);
    CHECK(memcmp(out, image_body, 53) == 0);
    CHECK(out[53] == 0xa5);
    static const uint8_t zero_body[33] = {
        0xa3,0xb2,0x2b,0x84,0x49,0xaf,0xaf,0xbc,0xd6,0xc0,0x9f,0x2c,0xfa,0x9d,0xe2,0xbe,
        0x93,0x5d,0x63,0x0c,0x18,0xef,0x6c,0x58,0x70,0xf0,0x97,0x45,0x51,0xd6,0x5f,0x20,0x23
    };
    static const uint8_t empty_tag[16] = {
        0xe8,0x23,0xb7,0xf1,0xa1,0xd3,0xf1,0xa0,0x46,0x2e,0xbd,0xb2,0xca,0xe3,0xb3,0x50
    };
    uint8_t zero[17] = {0};
    CHECK(tai_image_gcm_encrypt(zero, zero, zero, 17, out, 33) == TAI_OK);
    CHECK(memcmp(out, zero_body, 33) == 0);
    CHECK(tai_image_gcm_encrypt(zero, zero, zero, 0, out, 16) == TAI_OK);
    CHECK(memcmp(out, empty_tag, 16) == 0);

    memset(sentinel, 0xa5, sizeof(sentinel));
    memcpy(out, sentinel, sizeof(out));
    CHECK(tai_image_gcm_encrypt(NULL, image_iv, image_plain, 37, out, 53) == TAI_ERR_ARGS);
    CHECK(tai_image_gcm_encrypt(image_key, NULL, image_plain, 37, out, 53) == TAI_ERR_ARGS);
    CHECK(tai_image_gcm_encrypt(image_key, image_iv, NULL, 37, out, 53) == TAI_ERR_ARGS);
    CHECK(tai_image_gcm_encrypt(image_key, image_iv, image_plain, 37, NULL, 53) == TAI_ERR_ARGS);
    CHECK(tai_image_gcm_encrypt(image_key, image_iv, image_plain, SIZE_MAX, out, SIZE_MAX) == TAI_ERR_ARGS);
    CHECK(tai_image_gcm_encrypt(image_key, image_iv, image_plain, PTRDIFF_MAX, out, SIZE_MAX) == TAI_ERR_ARGS);
    for (size_t n = 0; n < 53; n++)
        CHECK(tai_image_gcm_encrypt(image_key, image_iv, image_plain, 37, out, n) == TAI_ERR_MEM);
    CHECK(memcmp(out, sentinel, sizeof(out)) == 0);

    uint8_t storage[160], before[160];
    const size_t offsets[] = {64, 65, 63, 12}; /* Last overlaps only the output tag. */
    for (size_t i = 0; i < sizeof(offsets) / sizeof(offsets[0]); i++) {
        memset(storage, 0xa5, sizeof(storage));
        memcpy(storage + 64, image_plain, 37);
        memcpy(before, storage, sizeof(before));
        CHECK(tai_image_gcm_encrypt(image_key, image_iv, storage + 64, 37,
                                   storage + offsets[i], 53) == TAI_ERR_ARGS);
        CHECK(memcmp(storage, before, sizeof(storage)) == 0);
    }
    CHECK(tai_image_gcm_encrypt(storage, image_iv, image_plain, 37, storage, 53) == TAI_ERR_ARGS);
    CHECK(tai_image_gcm_encrypt(image_key, storage, image_plain, 37, storage, 53) == TAI_ERR_ARGS);
    memcpy(storage, image_plain, 37);
    CHECK(tai_image_gcm_encrypt(image_key, image_iv, storage, 37, storage + 37, 53) == TAI_OK);
    CHECK(memcmp(storage, image_plain, 37) == 0);
    CHECK(memcmp(storage + 37, image_body, 53) == 0);
    PASS();
}

static void test_image_gcm_bounds(void)
{
    TEST("image GCM short lengths, capacity and nonoverlap");
    uint8_t out[37], sentinel[37];
    memset(sentinel, 0xa5, sizeof(sentinel));
    memcpy(out, sentinel, sizeof(out));
    for (size_t n = 0; n < 16; n++) {
        CHECK(tai_image_gcm_decrypt(image_key, image_iv, image_body, n,
                                   out, sizeof(out)) == TAI_ERR_ARGS);
        CHECK(memcmp(out, sentinel, sizeof(out)) == 0);
    }
    CHECK(tai_image_gcm_decrypt(image_key, image_iv, image_body, 53,
                               out, 36) == TAI_ERR_MEM);
    CHECK(tai_image_gcm_decrypt(NULL, image_iv, image_body, 53,
                               out, sizeof(out)) == TAI_ERR_ARGS);
    CHECK(tai_image_gcm_decrypt(image_key, NULL, image_body, 53,
                               out, sizeof(out)) == TAI_ERR_ARGS);
    CHECK(tai_image_gcm_decrypt(image_key, image_iv, NULL, 53,
                               out, sizeof(out)) == TAI_ERR_ARGS);
    CHECK(tai_image_gcm_decrypt(image_key, image_iv, image_body, 53,
                               NULL, sizeof(out)) == TAI_ERR_ARGS);
    CHECK(memcmp(out, sentinel, sizeof(out)) == 0);
    for (size_t n = 16; n < sizeof(image_body); n++) {
        uint8_t zero[37] = {0};
        memcpy(out, sentinel, sizeof(out));
        CHECK(tai_image_gcm_decrypt(image_key, image_iv, image_body, n,
                                   out, sizeof(out)) == TAI_ERR_CRYPTO);
        CHECK(memcmp(out, zero, n - 16) == 0);
        CHECK(memcmp(out + (n - 16), sentinel, sizeof(out) - (n - 16)) == 0);
    }

    uint8_t storage[128], before[128];
    /* Output overlaps ciphertext, just the tag, or precedes the input. */
    const size_t offsets[] = {32, 33, 69, 31};
    for (size_t i = 0; i < sizeof(offsets) / sizeof(offsets[0]); i++) {
        memset(storage, 0xa5, sizeof(storage));
        memcpy(storage + 32, image_body, sizeof(image_body));
        memcpy(before, storage, sizeof(before));
        CHECK(tai_image_gcm_decrypt(image_key, image_iv, storage + 32, 53,
                                   storage + offsets[i], 37) == TAI_ERR_ARGS);
        CHECK(memcmp(storage, before, sizeof(storage)) == 0);
    }
    memcpy(storage, image_key, 16);
    CHECK(tai_image_gcm_decrypt(storage, image_iv, image_body, 53,
                               storage, 37) == TAI_ERR_ARGS);
    memcpy(storage, image_iv, 16);
    CHECK(tai_image_gcm_decrypt(image_key, storage, image_body, 53,
                               storage, 37) == TAI_ERR_ARGS);
    /* Adjacent, disjoint output is allowed. */
    memcpy(storage, image_body, 53);
    CHECK(tai_image_gcm_decrypt(image_key, image_iv, storage, 53,
                               storage + 53, 37) == TAI_OK);
    CHECK(memcmp(storage + 53, image_plain, 37) == 0);
    CHECK(memcmp(storage, image_body, 53) == 0);
    PASS();
}

/* -------------------------------------------------------------------------
 * 9. Protocol builder: ClientHello
 * ------------------------------------------------------------------------- */
static void test_proto_client_hello(void)
{
    printf("\n[protocol builders]\n");

    /* Build a minimal context */
    static uint8_t ctx_mem[sizeof(struct tai_ctx)];
    memset(ctx_mem, 0, sizeof(ctx_mem));
    struct tai_ctx *ctx = (struct tai_ctx *)ctx_mem;
    ctx->pal        = &g_stub_pal;
    ctx->proto_ver  = TAI_VER_21;
    ctx->client_type = TAI_CLIENT_DEVICE;
    ctx->sign_level  = TAI_SIGN_HMAC_SHA256;
    ctx->sig_len     = 32;
    ctx->client_id   = "test-client-id-abc";
    memset(ctx->encrypt_random, 0xBB, 32);

    uint8_t buf[512];
    TEST("build_client_hello");
    int len = tai_proto_build_client_hello(ctx, buf, sizeof(buf));
    CHECK(len > 0);
    /* Decode and verify */
    uint8_t pkt_type;
    tai_attr_t attrs[8];
    int nattrs;
    const uint8_t *payload;
    size_t payload_len;
    int rc = tai_packet_decode(TAI_VER_21, buf, (size_t)len,
                                &pkt_type, attrs, 8, &nattrs,
                                &payload, &payload_len);
    CHECK(rc == TAI_OK);
    CHECK(pkt_type == TAI_PKT_CLIENT_HELLO);
    CHECK(nattrs == 5);
    /* Attr 0: client-type */
    const tai_attr_t *ct = tai_attr_find(attrs, nattrs, TAI_ATTR_CLIENT_TYPE);
    CHECK(ct && tai_attr_u8(ct) == TAI_CLIENT_DEVICE);
    /* Attr: security-suit */
    const tai_attr_t *ss = tai_attr_find(attrs, nattrs, TAI_ATTR_SECURITY_SUIT);
    CHECK(ss && ss->len == 33);
    CHECK(ss->value[0] == TAI_SIGN_HMAC_SHA256);
    /* Remaining 32 bytes should be our fixed encrypt_random */
    uint8_t expected_random[32]; memset(expected_random, 0xBB, 32);
    CHECK(memcmp(ss->value + 1, expected_random, 32) == 0);
    /* Attr: max-fragment-len = AGENTIC_KIT_TAI_MAX_FRAGMENT_PAYLOAD */
    const tai_attr_t *mf = tai_attr_find(attrs, nattrs, TAI_ATTR_MAX_FRAGMENT_LEN);
    CHECK(mf && tai_attr_u32(mf) == AGENTIC_KIT_TAI_MAX_FRAGMENT_PAYLOAD);
    PASS();

    TEST("build_session_new");
    len = tai_proto_build_session_new(ctx, buf, sizeof(buf));
    CHECK(len > 0);
    rc = tai_packet_decode(TAI_VER_21, buf, (size_t)len,
                            &pkt_type, attrs, 8, &nattrs,
                            &payload, &payload_len);
    CHECK(rc == TAI_OK);
    CHECK(pkt_type == TAI_PKT_SESSION_NEW);
    /* session-id attr should be present */
    const tai_attr_t *sid = tai_attr_find(attrs, nattrs, TAI_ATTR_SESSION_ID);
    CHECK(sid && sid->len > 0);
    /* payload = TAI_SESSION_PAYLOAD (16 bytes) */
    CHECK(payload_len == 16);
    PASS();

    TEST("build_event_start + event_end");
    len = tai_proto_build_event_start(ctx, buf, sizeof(buf));
    CHECK(len > 0);
    rc = tai_packet_decode(TAI_VER_21, buf, (size_t)len,
                            &pkt_type, attrs, 8, &nattrs, &payload, &payload_len);
    CHECK(rc == TAI_OK);
    CHECK(pkt_type == TAI_PKT_EVENT);
    /* Unpack event type from payload */
    uint16_t evt_type;
    const uint8_t *evt_data;
    size_t evt_data_len;
    rc = tai_unpack_event(TAI_VER_21, payload, payload_len,
                           &evt_type, &evt_data, &evt_data_len);
    CHECK(rc == TAI_OK);
    CHECK(evt_type == TAI_EVT_START);
    PASS();

    TEST("build_ping");
    len = tai_proto_build_ping(ctx, buf, sizeof(buf));
    CHECK(len > 0);
    rc = tai_packet_decode(TAI_VER_21, buf, (size_t)len,
                            &pkt_type, attrs, 8, &nattrs, &payload, &payload_len);
    CHECK(rc == TAI_OK);
    CHECK(pkt_type == TAI_PKT_PING);
    const tai_attr_t *ts_attr = tai_attr_find(attrs, nattrs, TAI_ATTR_CLIENT_TIMESTAMP);
    CHECK(ts_attr && tai_attr_u64(ts_attr) == 1700000000000ULL);
    PASS();
}

/* -------------------------------------------------------------------------
 * 9. Scatter-gather HMAC (§6.1) — golden-standard parity test.
 *
 * tai_frame_hmac_sg signs a frame whose [frame-header || app-header || payload]
 * live in separate buffers. It MUST produce byte-for-byte the same signature as
 * signing that concatenation contiguously. We compare it against an independent
 * contiguous reference across a matrix that straddles every boundary the
 * Section-6.4 first-32/last-32 rule cares about (total around 64; app-header
 * spanning the first 32; payload slice shorter than 32).
 * ------------------------------------------------------------------------- */
static void hmac_ref(const uint8_t *buf, size_t total,
                     const uint8_t key[32], uint8_t sig_len, uint8_t *out)
{
    uint8_t data[64];
    size_t  dlen;
    if (total <= 64) {
        memcpy(data, buf, total);
        dlen = total;
    } else {
        memcpy(data, buf, 32);
        memcpy(data + 32, buf + total - 32, 32);
        dlen = 64;
    }
    uint8_t full[32];
    tai_hmac_sha256(key, 32, data, dlen, full);
    memcpy(out, full, sig_len);
}

static void test_hmac_sg(void)
{
    TEST("frame_hmac_sg matches contiguous (golden matrix)");

    uint8_t key[32];
    for (int i = 0; i < 32; i++) key[i] = (uint8_t)(0x10 + i);

    const size_t  ah_lens[] = { 0, 1, 9, 27, 32, 33 };
    const size_t  pl_lens[] = { 0, 1, 27, 31, 32, 33, 64, 32000 };
    const uint8_t sigs[]    = { 0, 20, 32 };

    static uint8_t app_hdr[64];
    static uint8_t payload[32000];
    static uint8_t contig[5 + 64 + 32000];
    for (size_t i = 0; i < sizeof(app_hdr); i++) app_hdr[i] = (uint8_t)(i * 3 + 1);
    for (size_t i = 0; i < sizeof(payload); i++) payload[i] = (uint8_t)(i * 7 + 2);

    uint8_t frame_hdr[5] = { 0x82, 0x00, 0x01, 0xAB, 0xCD };

    int mismatches = 0, errors = 0;
    for (size_t ai = 0; ai < sizeof(ah_lens)/sizeof(ah_lens[0]); ai++)
    for (size_t pi = 0; pi < sizeof(pl_lens)/sizeof(pl_lens[0]); pi++)
    for (size_t si = 0; si < sizeof(sigs)/sizeof(sigs[0]); si++) {
        size_t  ahl = ah_lens[ai], pll = pl_lens[pi];
        uint8_t sl  = sigs[si];

        size_t total = 5 + ahl + pll;
        memcpy(contig, frame_hdr, 5);
        memcpy(contig + 5, app_hdr, ahl);
        memcpy(contig + 5 + ahl, payload, pll);

        uint8_t ref[32] = {0}, got[32] = {0};
        if (sl) hmac_ref(contig, total, key, sl, ref);

        /* 3 logical segments + an empty one in the middle (zero-length seg). */
        tai_seg_t segs[4] = {
            { frame_hdr, 5 }, { app_hdr, ahl }, { NULL, 0 }, { payload, pll }
        };
        if (tai_frame_hmac_sg(segs, 4, key, sl, got) != TAI_OK) errors++;
        if (sl && memcmp(ref, got, sl) != 0) mismatches++;
    }
    CHECK(errors == 0);
    CHECK(mismatches == 0);
    PASS();
}

/* Complete offline PAL for constructor/lifecycle tests. Any network, worker,
 * sleep or PAL allocation activity is observable, without opening real sockets. */
static int image_network_calls, image_worker_calls, image_alloc_calls;
static int image_mutex_calls, image_mutex_fail_at, image_mutex_destroys;
static int image_callback_calls;

static void *image_tcp_connect(const char *host, uint16_t port, uint32_t timeout)
{
    (void)host; (void)port; (void)timeout;
    image_network_calls++;
    return NULL;
}
static int image_tcp_send(void *h, const uint8_t *b, size_t n, uint32_t t)
{
    (void)h; (void)b; (void)n; (void)t;
    image_network_calls++;
    return PAL_ERR_NET;
}
static int image_tcp_recv(void *h, uint8_t *b, size_t n, uint32_t t)
{
    (void)h; (void)b; (void)n; (void)t;
    image_network_calls++;
    return PAL_ERR_NET;
}
static void image_tcp_close(void *h)
{
    (void)h;
    image_network_calls++;
}
static int image_tcp_poll(void *h, int events, uint32_t timeout)
{
    (void)h; (void)events; (void)timeout;
    image_network_calls++;
    return PAL_ERR_NET;
}
static int image_thread_create(void **h, void *(*fn)(void *), void *arg)
{
    (void)h; (void)fn; (void)arg;
    image_worker_calls++;
    return -1;
}
static int image_thread_join(void *h)
{
    (void)h;
    image_worker_calls++;
    return 0;
}
static void image_sleep(uint32_t ms)
{
    (void)ms;
    image_worker_calls++;
}
static void *image_malloc(size_t n)
{
    image_alloc_calls++;
    return malloc(n);
}
static void *image_mutex_create(void)
{
    image_mutex_calls++;
    return image_mutex_calls == image_mutex_fail_at ? NULL : &g_stub_mutex;
}
static void image_mutex_destroy(void *h)
{
    (void)h;
    image_mutex_destroys++;
}
static void image_disconnect_cb(tai_ctx_t *ctx, const tai_disconnect_msg_t *m, void *u)
{
    (void)ctx; (void)m; (void)u;
    image_callback_calls++;
}
static pal_t image_test_pal(void)
{
    pal_t pal = g_stub_pal;
    pal.tcp_connect = image_tcp_connect;
    pal.tcp_send = image_tcp_send;
    pal.tcp_recv = image_tcp_recv;
    pal.tcp_close = image_tcp_close;
    pal.tcp_poll = image_tcp_poll;
    pal.thread_create = image_thread_create;
    pal.thread_join = image_thread_join;
    pal.sleep_ms = image_sleep;
    pal.malloc = image_malloc;
    pal.mutex_create = image_mutex_create;
    pal.mutex_destroy = image_mutex_destroy;
    return pal;
}

static size_t image_entropy_scratch_len;
static int image_entropy_allocs, image_entropy_frees;

static void *image_entropy_malloc(size_t len)
{
    image_entropy_scratch_len = len;
    image_entropy_allocs++;
    void *p = malloc(len);
    if (p) memset(p, 0xa5, len);
    return p;
}

static void image_entropy_free(void *p)
{
    int wiped = 1;
    for (size_t i = 0; i < image_entropy_scratch_len; i++)
        if (((uint8_t *)p)[i]) wiped = 0;
    CHECK(wiped);
    image_entropy_frees++;
    free(p);
}

/* Send-only stub, deliberately before rng_init: the real RNG fails closed
 * without an entropy seed. No Connection profile or handshake is exercised. */
static void test_image_send_only_rng_failure(void)
{
    TEST("image send-only RNG failure emits nothing");
    pal_t pal = image_test_pal();
    pal.malloc = image_entropy_malloc;
    pal.free = image_entropy_free;
    static struct tai_ctx ctx;
    const size_t lengths[] = {0, 1, sizeof(image_plain)};
    for (int api = 0; api < 3; api++) {
        for (size_t i = 0; i < sizeof(lengths) / sizeof(lengths[0]); i++) {
            memset(&ctx, 0, sizeof(ctx));
            ctx.pal = &pal;
            ctx.mutex = &g_stub_mutex;
            ctx.connected = ctx.disable_tls = ctx.has_image_key = 1;
            ctx.proto_ver = TAI_VER_21;
            ctx.frame_ver = 3;
            ctx.sig_len = 0;
            ctx.sign_level = TAI_SIGN_NONE;
            memcpy(ctx.image_key, image_key, 16);
            memcpy(ctx.event_id, "unchanged", 10);
            int rc;
            if (api == 0)
                rc = tai_send_image(&ctx, image_plain, lengths[i], TAI_IMG_JPEG, 640, 480);
            else if (api == 1)
                rc = tai_send_image_with_text(&ctx, "test", 4, image_plain, lengths[i], TAI_IMG_JPEG, 640, 480);
            else
                rc = tai_send_image_audio_start(&ctx, image_plain, lengths[i], TAI_IMG_JPEG, 640, 480,
                                               TAI_AUDIO_PCM, 1, 16, 16000);
            CHECK(rc == TAI_ERR_CRYPTO);
            CHECK(image_entropy_allocs == image_entropy_frees);
            CHECK(image_entropy_scratch_len == (lengths[i] ? lengths[i] + 32 : 16));
            CHECK(ctx.seq == 0 && ctx.event_open == 0 && ctx.audio_started == 0);
            CHECK(strcmp(ctx.event_id, "unchanged") == 0);
            CHECK(memcmp(ctx.image_key, image_key, 16) == 0);
        }
    }
    CHECK(image_entropy_allocs == 9 && image_entropy_frees == 9);
    CHECK(image_network_calls == 0 && image_worker_calls == 0);
    PASS();
}

/* Must run BEFORE the process-wide RNG is seeded so the second mutex failure
 * exercises the constructor's rng_init failure, not its already-ready path. */
static void test_image_key_init_failure(void)
{
    TEST("image key not retained on constructor failure");
    pal_t pal = image_test_pal();
    tai_config_t cfg = { .pal = &pal, .image_key = image_key };
    static struct tai_ctx mem;
    uint8_t zero[16] = {0};
    CHECK(tai_ctx_init(NULL, &cfg) == NULL);
    CHECK(tai_ctx_init(&mem, NULL) == NULL);
    cfg.pal = &g_stub_pal; /* Incomplete PAL rejected before construction. */
    CHECK(tai_ctx_init(&mem, &cfg) == NULL);
    CHECK(memcmp(mem.image_key, zero, 16) == 0);
    cfg.pal = &pal;
    for (int fail_at = 1; fail_at <= 2; fail_at++) {
        image_mutex_calls = image_mutex_destroys = 0;
        image_mutex_fail_at = fail_at;
        memset(&mem, 0xa5, sizeof(mem));
        CHECK(tai_ctx_init(&mem, &cfg) == NULL);
        CHECK(image_mutex_calls == fail_at);
        CHECK(image_mutex_destroys == fail_at - 1);
        CHECK(mem.has_image_key == 0);
        CHECK(memcmp(mem.image_key, zero, 16) == 0);
    }
    image_mutex_fail_at = 0;
    CHECK(image_network_calls == 0);
    CHECK(image_worker_calls == 0);
    CHECK(image_alloc_calls == 0);
    PASS();
}

static void test_image_key_ownership(void)
{
    TEST("image key owned, isolated, persistent and wiped");
    pal_t pal = image_test_pal();
    uint8_t caller_key[16], zero[16] = {0};
    memcpy(caller_key, image_key, 16);
    tai_config_t cfg = {
        .pal = &pal, .host = "offline.invalid", .port = 443,
        .device_id = "synthetic-device", .local_key = "synthetic-local-key",
        .on_disconnect = image_disconnect_cb
    };
    static struct tai_ctx legacy_mem, first_mem, second_mem;
    tai_ctx_t *legacy = tai_ctx_init(&legacy_mem, &cfg);
    CHECK(legacy != NULL);
    if (!legacy) return;
    CHECK(legacy->has_image_key == 0);
    CHECK(legacy->proto_ver == TAI_VER_21 && legacy->frame_ver == 2);
    CHECK(legacy->sign_level == TAI_SIGN_HMAC_SHA256 && legacy->sig_len == 32);
    CHECK(memcmp(legacy->image_key, zero, 16) == 0);
    cfg.image_key = caller_key;
    tai_ctx_t *first = tai_ctx_init(&first_mem, &cfg);
    CHECK(first != NULL);
    if (!first) { tai_ctx_deinit(legacy); return; }
    CHECK(first->has_image_key == 1);
    CHECK(memcmp(first->image_key, image_key, 16) == 0);
    memset(caller_key, 0, sizeof(caller_key));
    tai_ctx_t *second = tai_ctx_init(&second_mem, &cfg);
    CHECK(second != NULL);
    if (!second) { tai_ctx_deinit(first); tai_ctx_deinit(legacy); return; }
    CHECK(second->has_image_key == 1);
    CHECK(memcmp(second->image_key, zero, 16) == 0);
    CHECK(memcmp(first->image_key, image_key, 16) == 0);
    memset(caller_key, 0x5a, sizeof(caller_key));
    CHECK(image_network_calls == 0);
    CHECK(image_worker_calls == 0);
    CHECK(image_alloc_calls == 0);

    tai_ctx_t *contexts[] = {first, second};
    for (size_t i = 0; i < 2; i++) {
        tai_ctx_t *ctx = contexts[i];
        const uint8_t *expected = i ? zero : image_key;
        CHECK(ctx->proto_ver == TAI_VER_21 && ctx->frame_ver == 3);
        CHECK(ctx->sign_level == TAI_SIGN_NONE && ctx->sig_len == 0);
        /* Explicit legacy key derivation must not replace the fixed image key. */
        CHECK(tai_crypto_derive_keys(ctx->proto_ver,
                  (const uint8_t *)cfg.local_key, strlen(cfg.local_key),
                  image_iv, sizeof(image_iv), ctx->encrypt_key,
                  ctx->sign_key, ctx->pal) == TAI_OK);
        uint8_t frame_key[32], random_before[32];
        memcpy(frame_key, ctx->encrypt_key, 32);
        memcpy(random_before, ctx->encrypt_random, 32);
        for (int raw = 0; raw <= 1; raw++) {
            ctx->disable_tls = (uint8_t)raw;
            for (int attempt = 0; attempt < 2; attempt++) {
                CHECK(tai_connect(ctx) == TAI_ERR_TLS);
                CHECK(ctx->has_image_key == 1);
                CHECK(memcmp(ctx->image_key, expected, 16) == 0);
                CHECK(memcmp(ctx->encrypt_key, frame_key, 32) == 0);
                CHECK(memcmp(ctx->sign_key, frame_key, 32) == 0);
                CHECK(memcmp(ctx->encrypt_random, random_before, 32) == 0);
                CHECK(ctx->seq == 0 && ctx->session_id[0] == '\0');
                CHECK(ctx->tls == NULL && ctx->raw_tcp == NULL);
                CHECK(!ctx->connected && !ctx->connecting && !ctx->session_open);
                CHECK(!ctx->running && ctx->thread_handle == NULL);
                tai_disconnect(ctx);
                CHECK(ctx->has_image_key == 1);
                CHECK(memcmp(ctx->image_key, expected, 16) == 0);
            }
        }
    }
    CHECK(image_network_calls == 0);
    CHECK(image_worker_calls == 0);
    CHECK(image_alloc_calls == 0);
    CHECK(image_callback_calls == 0);
    tai_ctx_deinit(first);
    CHECK(first_mem.has_image_key == 0);
    CHECK(memcmp(first_mem.image_key, zero, 16) == 0);
    CHECK(second->has_image_key == 1);
    tai_ctx_deinit(second);
    CHECK(second_mem.has_image_key == 0);
    CHECK(memcmp(second_mem.image_key, zero, 16) == 0);
    for (size_t i = 0; i < sizeof(caller_key); i++) CHECK(caller_key[i] == 0x5a);
    /* No-key construction still reaches the legacy Connection path. */
    legacy->disable_tls = 1;
    CHECK(tai_connect(legacy) == TAI_ERR_NET);
    CHECK(image_network_calls == 1);
    tai_ctx_deinit(legacy);
    PASS();
}

static void image_cert_bundle_attach(void *ssl_config)
{
    (void)ssl_config;
    CHECK(0); /* Pre-network guards must never configure TLS. */
}

static void test_image_connection_guards(void)
{
    TEST("keyed Connection profile and pre-network TLS guards");
    pal_t pal = image_test_pal();
    const struct {
        const char *ca;
        int bundle, raw, expected;
    } cases[] = {
        {NULL, 0, 0, TAI_ERR_TLS},
        {"",   0, 0, TAI_ERR_TLS},
        {NULL, 0, 1, TAI_ERR_TLS},
        {"",   0, 1, TAI_ERR_TLS},
        {"synthetic-ca", 0, 1, TAI_ERR_TLS},
        {NULL, 1, 1, TAI_ERR_TLS},
        {"synthetic-ca", 1, 1, TAI_ERR_TLS},
        {"synthetic-ca", 0, 0, TAI_ERR_ARGS},
        {NULL, 1, 0, TAI_ERR_ARGS},
        {"",   1, 0, TAI_ERR_ARGS}
    };
    int networks = image_network_calls, workers = image_worker_calls;
    int allocs = image_alloc_calls, callbacks = image_callback_calls;
    static struct tai_ctx mem;
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        /* No endpoint: accepted trust configuration stops at argument validation,
         * before certificate parsing, entropy generation or networking. */
        tai_config_t cfg = {
            .pal = &pal, .image_key = image_key,
            .protocol_version = TAI_VER_21, .sign_level = TAI_SIGN_HMAC_SHA1,
            .ca_cert = cases[i].ca, .disable_tls = cases[i].raw,
            .cert_bundle_attach = cases[i].bundle ? image_cert_bundle_attach : NULL,
            .on_disconnect = image_disconnect_cb
        };
        tai_ctx_t *ctx = tai_ctx_init(&mem, &cfg);
        CHECK(ctx != NULL);
        if (!ctx) continue;
        CHECK(ctx->has_image_key && ctx->proto_ver == TAI_VER_21);
        CHECK(ctx->frame_ver == 3 && ctx->sig_len == 0 && ctx->sign_level == TAI_SIGN_NONE);
        CHECK(ctx->ca_cert == cfg.ca_cert && ctx->cert_bundle_attach == cfg.cert_bundle_attach);
        CHECK(tai_connect(ctx) == cases[i].expected);
        if (cases[i].expected == TAI_ERR_ARGS) {
            ctx->proto_ver = 20;
            CHECK(tai_connect(ctx) == TAI_ERR_PROTO);
            ctx->proto_ver = TAI_VER_21;
            ctx->frame_ver = 2;
            CHECK(tai_connect(ctx) == TAI_ERR_PROTO);
            ctx->frame_ver = 3;
            ctx->sign_level = TAI_SIGN_HMAC_SHA256;
            CHECK(tai_connect(ctx) == TAI_ERR_PROTO);
            ctx->sign_level = TAI_SIGN_NONE;
            ctx->sig_len = 32;
            CHECK(tai_connect(ctx) == TAI_ERR_PROTO);
        }
        CHECK(!ctx->connected && !ctx->connecting && !ctx->running);
        CHECK(ctx->tls == NULL && ctx->raw_tcp == NULL && ctx->thread_handle == NULL);
        CHECK(ctx->seq == 0 && ctx->session_id[0] == '\0');
        CHECK(memcmp(ctx->image_key, image_key, 16) == 0);
        tai_ctx_deinit(ctx);
    }
    CHECK(image_network_calls == networks && image_worker_calls == workers);
    CHECK(image_alloc_calls == allocs && image_callback_calls == callbacks);
    PASS();
}

/* Offline complete-Packet dispatch: independent ciphertext, synthetic Data ID.
 * This does not exercise the Connection handshake or TLS verification. */
static struct {
    int calls, allocs, frees, fail_at;
    tai_image_msg_t msg;
    uint8_t bytes[256];
    char event[64];
    void *alloc[2];
    size_t sizes[2];
} image_rx;

static void image_rx_cb(tai_ctx_t *ctx, const tai_image_msg_t *m, void *u)
{
    (void)ctx; (void)u;
    image_rx.calls++;
    image_rx.msg = *m;
    CHECK(m->len <= sizeof(image_rx.bytes));
    if (m->len <= sizeof(image_rx.bytes)) memcpy(image_rx.bytes, m->data, m->len);
    snprintf(image_rx.event, sizeof(image_rx.event), "%s", m->event_id);
}

static void *image_rx_malloc(size_t n)
{
    int i = image_rx.allocs++;
    if (image_rx.allocs == image_rx.fail_at) return NULL;
    CHECK(i < 2);
    if (i >= 2) return NULL;
    image_rx.alloc[i] = malloc(n);
    image_rx.sizes[i] = n;
    return image_rx.alloc[i];
}

static void image_rx_free(void *p)
{
    for (int i = 0; i < 2; i++) {
        if (p != image_rx.alloc[i]) continue;
        /* Fixture plaintext allocations must be wiped on success AND failure. */
        if (image_rx.sizes[i] == sizeof(image_plain) || image_rx.sizes[i] == 1) {
            int wiped = 1;
            for (size_t j = 0; j < image_rx.sizes[i]; j++)
                if (((uint8_t *)p)[j]) wiped = 0;
            CHECK(wiped);
        }
        image_rx.alloc[i] = NULL;
    }
    image_rx.frees++;
    free(p);
}

static void image_rx_init(tai_ctx_t *ctx, pal_t *pal)
{
    memset(ctx, 0, sizeof(*ctx));
    memset(&image_rx, 0, sizeof(image_rx));
    *pal = g_stub_pal;
    pal->malloc = image_rx_malloc;
    pal->free = image_rx_free;
    ctx->pal = pal;
    ctx->proto_ver = TAI_VER_21;
    ctx->frame_ver = 3;
    ctx->sig_len = 0;
    ctx->sign_level = TAI_SIGN_NONE;
    ctx->has_image_key = 1;
    memcpy(ctx->image_key, image_key, 16);
    ctx->on_image = image_rx_cb;
}

static size_t image_rx_payload(uint8_t *out, uint8_t flag, int base64)
{
    /* Independent header: Data ID 6, timestamp 0x0102030405, IV at offset 8. */
    const uint8_t header[8] = {0, 6, 0, 1, 2, 3, 4, 5};
    memcpy(out, header, 8);
    out[2] = flag << 6;
    memcpy(out + 8, image_iv, 16);
    size_t n = sizeof(image_body);
    if (base64)
        CHECK(mbedtls_base64_encode(out + 24, 128, &n, image_body, sizeof(image_body)) == 0);
    else
        memcpy(out + 24, image_body, n);
    return 24 + n;
}

static int image_rx_dispatch(tai_ctx_t *ctx, const char *params, const char *event,
                             const uint8_t *payload, size_t len)
{
    tai_attr_t attrs[2];
    int n = 0;
    if (params) attrs[n++] = tai_attr_strv(TAI_ATTR_IMAGE_PARAMS, params);
    if (event) attrs[n++] = tai_attr_strv(TAI_ATTR_EVENT_ID, event);
    /* All scratch must have been released by the preceding callback/dispatch. */
    CHECK(!image_rx.alloc[0] && !image_rx.alloc[1]);
    image_rx.allocs = image_rx.frees = 0;
    return tai_proto_dispatch(ctx, TAI_PKT_IMAGE, attrs, n, payload, len);
}

static void test_image_receive_types(void)
{
    TEST("image receive RAW/Base64 exact plaintext and URL");
    static struct tai_ctx ctx;
    pal_t pal;
    uint8_t payload[160], before[160];
    for (int b64 = 0; b64 <= 1; b64++) {
        image_rx_init(&ctx, &pal);
        size_t len = image_rx_payload(payload, TAI_STREAM_ONE_SHOT, b64);
        memcpy(before, payload, len);
        CHECK(image_rx_dispatch(&ctx, b64 ? "1 2 640 480" : "0 2 640 480",
                                "image-event", payload, len) == TAI_OK);
        CHECK(image_rx.calls == 1 && image_rx.msg.len == sizeof(image_plain));
        CHECK(memcmp(image_rx.bytes, image_plain, sizeof(image_plain)) == 0);
        CHECK(image_rx.msg.payload_type == TAI_IMG_PAYLOAD_RAW);
        CHECK(image_rx.msg.format == TAI_IMG_PNG && image_rx.msg.width == 640 && image_rx.msg.height == 480);
        CHECK(image_rx.msg.data_id == 6 && image_rx.msg.timestamp_ms == 0x0102030405ULL);
        CHECK(image_rx.msg.stream_flag == TAI_STREAM_ONE_SHOT);
        CHECK(strcmp(image_rx.event, "image-event") == 0);
        CHECK(image_rx.allocs == b64 + 1 && image_rx.frees == image_rx.allocs);
        CHECK(memcmp(before, payload, len) == 0 && !ctx.rx_image_active);
    }
    image_rx_init(&ctx, &pal);
    image_rx_payload(payload, TAI_STREAM_ONE_SHOT, 0);
    const char url[] = "https://example.invalid/image";
    memcpy(payload + 24, url, sizeof(url) - 1);
    CHECK(image_rx_dispatch(&ctx, "2 1 0 0", NULL, payload, 24 + sizeof(url) - 1) == TAI_OK);
    CHECK(image_rx.calls == 1 && image_rx.msg.payload_type == TAI_IMG_PAYLOAD_URL);
    CHECK(image_rx.msg.len == sizeof(url) - 1 && memcmp(image_rx.bytes, url, sizeof(url) - 1) == 0);
    CHECK(image_rx.allocs == 0);

    /* Legacy body includes everything after byte 8, even an IV/ciphertext. */
    for (int type = 0; type < 3; type++) {
        image_rx_init(&ctx, &pal);
        ctx.has_image_key = 0;
        ctx.frame_ver = 2;
        ctx.sig_len = 32;
        ctx.sign_level = TAI_SIGN_HMAC_SHA256;
        size_t len = image_rx_payload(payload, TAI_STREAM_ONE_SHOT, 0);
        char params[] = "0 1 640 480";
        params[0] += type;
        CHECK(image_rx_dispatch(&ctx, params, NULL, payload, len) == TAI_OK);
        CHECK(image_rx.msg.payload_type == type && image_rx.msg.len == len - 8);
        CHECK(memcmp(image_rx.bytes, payload + 8, len - 8) == 0 && image_rx.allocs == 0);
    }
    PASS();
}

static void test_image_receive_streams(void)
{
    TEST("image Stream identity, omitted attrs and resets");
    static struct tai_ctx ctx;
    pal_t pal;
    uint8_t payload[160], evt[2] = {0, TAI_EVT_END};
    image_rx_init(&ctx, &pal);
    size_t len = image_rx_payload(payload, TAI_STREAM_START, 1);
    CHECK(image_rx_dispatch(&ctx, "1 1 640 480", "event-a", payload, len) == TAI_OK);
    CHECK(ctx.rx_image_active);
    payload[2] = TAI_STREAM_MIDDLE << 6;
    CHECK(image_rx_dispatch(&ctx, NULL, NULL, payload, len) == TAI_OK);
    CHECK(image_rx.calls == 2 && image_rx.msg.format == 0 && image_rx.msg.width == 0 && image_rx.msg.height == 0);
    CHECK(image_rx.msg.payload_type == TAI_IMG_PAYLOAD_RAW && strcmp(image_rx.event, "event-a") == 0);
    CHECK(memcmp(image_rx.bytes, image_plain, sizeof(image_plain)) == 0);
    payload[2] = TAI_STREAM_END << 6;
    CHECK(image_rx_dispatch(&ctx, "1 1 640 480", "event-a", payload, len) == TAI_OK);
    CHECK(!ctx.rx_image_active && image_rx.calls == 3);
    CHECK(image_rx_dispatch(&ctx, NULL, NULL, payload, len) == TAI_PROTO_ERR_UNEXPECTED);
    CHECK(image_rx.calls == 3);

    for (int conflict = 0; conflict < 4; conflict++) {
        image_rx_init(&ctx, &pal);
        len = image_rx_payload(payload, TAI_STREAM_START, 0);
        CHECK(image_rx_dispatch(&ctx, "0 1 640 480", "event-a", payload, len) == TAI_OK);
        payload[2] = TAI_STREAM_MIDDLE << 6;
        if (conflict == 0) payload[1]++;
        CHECK(image_rx_dispatch(&ctx, conflict == 1 ? "1 1 640 480" :
                                      conflict == 3 ? "0 1 641 480" : NULL,
                                conflict == 2 ? "event-b" : NULL,
                                payload, len) == TAI_PROTO_ERR_UNEXPECTED);
        CHECK(image_rx.calls == 1 && !ctx.rx_image_active);
        CHECK(strcmp(ctx.rx_event_id, "event-a") == 0);
    }
    for (int reset = 0; reset < 5; reset++) {
        image_rx_init(&ctx, &pal);
        len = image_rx_payload(payload, TAI_STREAM_START, 0);
        CHECK(image_rx_dispatch(&ctx, "0 1 640 480", "event-a", payload, len) == TAI_OK);
        if (reset == 0) tai_proto_reset_image(&ctx);
        else if (reset == 1)
            CHECK(tai_proto_dispatch(&ctx, TAI_PKT_SESSION_CLOSE, NULL, 0, NULL, 0) == TAI_OK);
        else {
            evt[1] = reset == 2 ? TAI_EVT_END : reset == 3 ? TAI_EVT_START : TAI_EVT_CHAT_BREAK;
            tai_attr_t attr = tai_attr_strv(TAI_ATTR_EVENT_ID, "event-b");
            CHECK(tai_proto_dispatch(&ctx, TAI_PKT_EVENT, &attr, 1, evt, 2) == TAI_OK);
        }
        CHECK(!ctx.rx_image_active && memcmp(ctx.image_key, image_key, 16) == 0);
        payload[2] = TAI_STREAM_MIDDLE << 6;
        CHECK(image_rx_dispatch(&ctx, NULL, NULL, payload, len) == TAI_PROTO_ERR_UNEXPECTED);
        CHECK(image_rx.calls == 1);
    }
    /* EventStart supplies identity when all Image Packets omit event-id. */
    image_rx_init(&ctx, &pal);
    tai_attr_t event_attr = tai_attr_strv(TAI_ATTR_EVENT_ID, "event-inherited");
    evt[1] = TAI_EVT_START;
    CHECK(tai_proto_dispatch(&ctx, TAI_PKT_EVENT, &event_attr, 1, evt, 2) == TAI_OK);
    len = image_rx_payload(payload, TAI_STREAM_START, 0);
    CHECK(image_rx_dispatch(&ctx, "0 1 1 1", NULL, payload, len) == TAI_OK);
    CHECK(strcmp(image_rx.event, "event-inherited") == 0);
    payload[2] = TAI_STREAM_MIDDLE << 6;
    CHECK(image_rx_dispatch(&ctx, NULL, "event-inherited", payload, len) == TAI_OK);
    CHECK(image_rx.calls == 2);

    /* Consecutive streams must not inherit the previous encoding. */
    image_rx_init(&ctx, &pal);
    len = image_rx_payload(payload, TAI_STREAM_ONE_SHOT, 1);
    CHECK(image_rx_dispatch(&ctx, "1 1 1 1", NULL, payload, len) == TAI_OK);
    len = image_rx_payload(payload, TAI_STREAM_ONE_SHOT, 0);
    CHECK(image_rx_dispatch(&ctx, "0 1 1 1", NULL, payload, len) == TAI_OK);
    CHECK(image_rx.calls == 2 && memcmp(image_rx.bytes, image_plain, 37) == 0);
    PASS();
}

static void test_image_receive_bounds(void)
{
    TEST("image short headers/bodies, empty markers and tag-only");
    static struct tai_ctx ctx;
    pal_t pal;
    uint8_t payload[160];
    image_rx_init(&ctx, &pal);
    image_rx_payload(payload, TAI_STREAM_ONE_SHOT, 0);
    for (size_t n = 0; n < 24; n++)
        CHECK(image_rx_dispatch(&ctx, "0 1 1 1", NULL, payload, n) == TAI_PROTO_ERR_MEDIA_HDR);
    for (size_t n = 1; n < sizeof(image_body); n++) {
        CHECK(image_rx_dispatch(&ctx, "0 1 1 1", NULL, payload, 24 + n) ==
              (n < 16 ? TAI_PROTO_ERR_PKT_DECODE : TAI_PROTO_ERR_IMAGE_AUTH));
        CHECK(image_rx.calls == 0 && image_rx.allocs == image_rx.frees);
    }
    CHECK(image_rx_dispatch(&ctx, "0 1 1 1", NULL, payload,
                            AGENTIC_KIT_TAI_FRAG_BUF_SIZE + 1U) == TAI_PROTO_ERR_FRAG);
    static uint8_t bounded[AGENTIC_KIT_TAI_FRAG_BUF_SIZE];
    memset(bounded, 'x', sizeof(bounded));
    memcpy(bounded, payload, 24);
    ctx.on_image = NULL;
    /* 1 Packet byte + 4 block bytes + 4 Attribute bytes + 7 params bytes. */
    CHECK(image_rx_dispatch(&ctx, "2 1 1 1", NULL, bounded, sizeof(bounded) - 16) == TAI_OK);
    CHECK(image_rx_dispatch(&ctx, "2 1 1 1", NULL, bounded, sizeof(bounded) - 15) == TAI_PROTO_ERR_FRAG);
    bounded[2] = TAI_STREAM_START << 6;
    CHECK(image_rx_dispatch(&ctx, "2 1 1 1", NULL, bounded, 24) == TAI_OK);
    bounded[2] = TAI_STREAM_MIDDLE << 6;
    /* No attrs means only the one-byte Packet header consumes capacity. */
    for (size_t spare = 4; spare > 0; spare--)
        CHECK(image_rx_dispatch(&ctx, NULL, NULL, bounded, sizeof(bounded) - spare) == TAI_OK);
    CHECK(image_rx_dispatch(&ctx, NULL, NULL, bounded, sizeof(bounded)) == TAI_PROTO_ERR_FRAG);
    payload[24] ^= 1;
    CHECK(image_rx_dispatch(&ctx, "0 1 1 1", NULL, payload, 24 + sizeof(image_body)) == TAI_PROTO_ERR_IMAGE_AUTH);
    CHECK(image_rx.calls == 0 && image_rx.allocs == image_rx.frees);
    for (int b64 = 0; b64 <= 1; b64++) {
        image_rx_init(&ctx, &pal);
        image_rx_payload(payload, TAI_STREAM_START, b64);
        CHECK(image_rx_dispatch(&ctx, b64 ? "1 1 1 1" : "0 1 1 1", NULL, payload, 24) == TAI_OK);
        payload[2] = TAI_STREAM_END << 6;
        CHECK(image_rx_dispatch(&ctx, NULL, NULL, payload, 24) == TAI_OK);
        payload[2] = TAI_STREAM_ONE_SHOT << 6;
        CHECK(image_rx_dispatch(&ctx, b64 ? "1 1 1 1" : "0 1 1 1", NULL, payload, 24) == TAI_OK);
        CHECK(image_rx.calls == 3 && image_rx.msg.len == 0 && image_rx.allocs == 0);
        payload[2] = TAI_STREAM_START << 6;
        CHECK(image_rx_dispatch(&ctx, "0 1 1 1", NULL, payload, 24) == TAI_OK);
        payload[2] = TAI_STREAM_MIDDLE << 6;
        CHECK(image_rx_dispatch(&ctx, NULL, NULL, payload, 24) == TAI_PROTO_ERR_PKT_DECODE);
        CHECK(image_rx.calls == 4);
    }
    static const uint8_t empty_tag[16] = {
        0xe8,0x23,0xb7,0xf1,0xa1,0xd3,0xf1,0xa0,0x46,0x2e,0xbd,0xb2,0xca,0xe3,0xb3,0x50
    };
    for (int b64 = 0; b64 <= 1; b64++) {
        image_rx_init(&ctx, &pal);
        memset(ctx.image_key, 0, 16);
        image_rx_payload(payload, TAI_STREAM_ONE_SHOT, 0);
        memset(payload + 8, 0, 16);
        size_t n = 16;
        if (b64) CHECK(mbedtls_base64_encode(payload + 24, 128, &n, empty_tag, 16) == 0);
        else memcpy(payload + 24, empty_tag, 16);
        CHECK(image_rx_dispatch(&ctx, b64 ? "1 1 1 1" : "0 1 1 1", NULL, payload, 24 + n) == TAI_OK);
        CHECK(image_rx.calls == 1 && image_rx.msg.len == 0 && image_rx.allocs == image_rx.frees);
        payload[24] ^= 1;
        CHECK(image_rx_dispatch(&ctx, b64 ? "1 1 1 1" : "0 1 1 1", NULL, payload, 24 + n) == TAI_PROTO_ERR_IMAGE_AUTH);
        CHECK(image_rx.calls == 1 && image_rx.allocs == image_rx.frees);
    }
    PASS();
}

static void test_image_receive_malformed(void)
{
    TEST("image params, Event IDs and Base64 reject ambiguity");
    static struct tai_ctx ctx;
    pal_t pal;
    uint8_t payload[160];
    image_rx_init(&ctx, &pal);
    size_t len = image_rx_payload(payload, TAI_STREAM_ONE_SHOT, 0);
    const char *bad[] = {NULL, "", "0 1 1", "0 1 1 1 2", "-1 1 1 1", "+0 1 1 1",
        "3 1 1 1", "0 256 1 1", "0 1 65536 1", "0 1 1 65536", "0 1 4294967296 1",
        "0 1 18446744073709551616 1", "0 1 1 1x", "0 1 1\t1"};
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++)
        CHECK(image_rx_dispatch(&ctx, bad[i], NULL, payload, len) == TAI_PROTO_ERR_PKT_DECODE);
    tai_attr_t attrs[3] = {tai_attr_strv(TAI_ATTR_IMAGE_PARAMS, "0 1 1 1"),
                           tai_attr_strv(TAI_ATTR_IMAGE_PARAMS, "0 1 1 1")};
    CHECK(tai_proto_dispatch(&ctx, TAI_PKT_IMAGE, attrs, 2, payload, len) == TAI_PROTO_ERR_PKT_DECODE);
    const uint8_t embedded[] = {'0',' ','1',' ','1',' ','1',0,'2'};
    attrs[0] = tai_attr_bytesv(TAI_ATTR_IMAGE_PARAMS, embedded, sizeof(embedded));
    CHECK(tai_proto_dispatch(&ctx, TAI_PKT_IMAGE, attrs, 1, payload, len) == TAI_PROTO_ERR_PKT_DECODE);
    attrs[0] = tai_attr_strv(TAI_ATTR_IMAGE_PARAMS, "0 1 1 1");
    char long_id[65]; memset(long_id, 'a', 64); long_id[64] = 0;
    CHECK(image_rx_dispatch(&ctx, "0 1 1 1", long_id, payload, len) == TAI_PROTO_ERR_PKT_DECODE);
    CHECK(image_rx_dispatch(&ctx, "0 1 1 1", "", payload, len) == TAI_PROTO_ERR_PKT_DECODE);
    attrs[1] = tai_attr_strv(TAI_ATTR_EVENT_ID, "event-a");
    attrs[2] = attrs[1];
    CHECK(tai_proto_dispatch(&ctx, TAI_PKT_IMAGE, attrs, 3, payload, len) == TAI_PROTO_ERR_PKT_DECODE);
    attrs[1] = tai_attr_bytesv(TAI_ATTR_EVENT_ID, (const uint8_t *)"a\0b", 3);
    CHECK(tai_proto_dispatch(&ctx, TAI_PKT_IMAGE, attrs, 2, payload, len) == TAI_PROTO_ERR_PKT_DECODE);
    /* An overlong Event must not truncate into a later matching image identity. */
    uint8_t evt[2] = {0, TAI_EVT_START};
    attrs[0] = tai_attr_strv(TAI_ATTR_EVENT_ID, long_id);
    CHECK(tai_proto_dispatch(&ctx, TAI_PKT_EVENT, attrs, 1, evt, 2) == TAI_PROTO_ERR_PKT_DECODE);
    CHECK(ctx.rx_event_id[0] == 0);
    const char *bad64[] = {"A", "AAA", "====", "AA=A", "A===", "AA==AAAA", "AB==", "AAB=",
                           "AAAA\n", " AAA", "AA-A", "AA_A", "AA==", "AAAA"};
    for (size_t i = 0; i < sizeof(bad64) / sizeof(bad64[0]); i++) {
        size_t n = strlen(bad64[i]);
        memcpy(payload + 24, bad64[i], n);
        CHECK(image_rx_dispatch(&ctx, "1 1 1 1", NULL, payload, 24 + n) == TAI_PROTO_ERR_PKT_DECODE);
    }
    CHECK(image_rx.calls == 0 && image_rx.allocs == image_rx.frees);
    for (size_t n = 1; n < 16; n++) {
        size_t encoded = 0;
        CHECK(mbedtls_base64_encode(payload + 24, 128, &encoded, image_body, n) == 0);
        CHECK(image_rx_dispatch(&ctx, "1 1 1 1", NULL, payload, 24 + encoded) == TAI_PROTO_ERR_PKT_DECODE);
        CHECK(image_rx.calls == 0 && image_rx.allocs == 0);
    }
    /* Exactly bounded, non-NUL-terminated params, including maximum values. */
    const uint8_t valid[] = {'0',' ','2','5','5',' ','6','5','5','3','5',' ','6','5','5','3','5'};
    attrs[0] = tai_attr_bytesv(TAI_ATTR_IMAGE_PARAMS, valid, sizeof(valid));
    len = image_rx_payload(payload, TAI_STREAM_ONE_SHOT, 0);
    CHECK(tai_proto_dispatch(&ctx, TAI_PKT_IMAGE, attrs, 1, payload, len) == TAI_OK);
    CHECK(image_rx.calls == 1 && image_rx.msg.format == 255 && image_rx.msg.width == 65535 && image_rx.msg.height == 65535);
    PASS();
}

static void test_image_receive_failures(void)
{
    TEST("image tampering and allocation failure never deliver");
    static struct tai_ctx ctx;
    pal_t pal;
    uint8_t payload[160], corrupt[sizeof(image_body)];
    for (int b64 = 0; b64 <= 1; b64++) {
        for (size_t i = 0; i < 16 + sizeof(image_body); i++) {
            image_rx_init(&ctx, &pal);
            size_t len = image_rx_payload(payload, TAI_STREAM_ONE_SHOT, b64);
            if (i < 16) payload[8 + i] ^= 1;
            else {
                memcpy(corrupt, image_body, sizeof(corrupt));
                corrupt[i - 16] ^= 1;
                size_t n = sizeof(corrupt);
                if (b64) CHECK(mbedtls_base64_encode(payload + 24, 128, &n, corrupt, sizeof(corrupt)) == 0);
                else memcpy(payload + 24, corrupt, n);
                len = 24 + n;
            }
            CHECK(image_rx_dispatch(&ctx, b64 ? "1 1 1 1" : "0 1 1 1", NULL, payload, len) == TAI_PROTO_ERR_IMAGE_AUTH);
            CHECK(image_rx.calls == 0 && image_rx.allocs == image_rx.frees && !ctx.rx_image_active);
        }
        for (int fail = 1; fail <= b64 + 1; fail++) {
            image_rx_init(&ctx, &pal);
            size_t len = image_rx_payload(payload, TAI_STREAM_START, b64);
            image_rx.fail_at = fail;
            CHECK(image_rx_dispatch(&ctx, b64 ? "1 1 1 1" : "0 1 1 1", NULL, payload, len) == TAI_PROTO_ERR_IMAGE_PROCESSING);
            CHECK(image_rx.calls == 0 && image_rx.frees == fail - 1 && !ctx.rx_image_active);
            CHECK(!image_rx.alloc[0] && !image_rx.alloc[1]);
        }
    }
    PASS();
}

/* -------------------------------------------------------------------------
 * main
 * ------------------------------------------------------------------------- */
int main(void)
{
    printf("=== Tuya AI Foundation — unit tests ===\n");

    test_image_send_only_rng_failure();
    test_image_key_init_failure();
    CHECK(rng_init(&g_stub_pal) == 0); /* seed after constructor failure injection */

    test_varint();
    test_attrs_v21();
    test_packet_v21();
    test_transport();
    test_payloads();
    test_crypto();
    test_image_gcm();
    test_image_gcm_encrypt();
    test_image_gcm_bounds();
    test_image_key_ownership();
    test_image_connection_guards();
    test_image_receive_types();
    test_image_receive_streams();
    test_image_receive_bounds();
    test_image_receive_malformed();
    test_image_receive_failures();
    test_proto_client_hello();
    test_hmac_sg();
    test_compile_time_log_ceiling();

    printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
