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

/* -------------------------------------------------------------------------
 * Minimal stub PAL.  TLS and crypto are no longer PAL responsibilities --
 * the SDK uses mbedTLS internally -- so the stub only needs time/memory/log.
 * ------------------------------------------------------------------------- */
static uint64_t stub_time(void) { return 1700000000000ULL; }
static void *stub_malloc(size_t s) { return malloc(s); }
static void  stub_free(void *p)    { free(p); }

static const pal_t g_stub_pal = {
    .tcp_connect      = NULL,
    .tcp_send         = NULL,
    .tcp_recv         = NULL,
    .tcp_close        = NULL,
    .tcp_poll         = NULL,
    .time_ms          = stub_time,
    .malloc           = stub_malloc,
    .free             = stub_free,
    .mutex_create     = NULL,
    .mutex_lock       = NULL,
    .mutex_unlock     = NULL,
    .mutex_destroy    = NULL,
    .thread_create    = NULL,
    .thread_join      = NULL,
};

/* -------------------------------------------------------------------------
 * Test framework
 * ------------------------------------------------------------------------- */
static int g_pass = 0, g_fail = 0;

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
 * 6. Transport fragmentation
 * ------------------------------------------------------------------------- */
typedef struct { uint8_t *buf; size_t total; int calls; } frag_capture_t;

static int capture_send(const uint8_t *buf, size_t len, void *arg)
{
    frag_capture_t *c = (frag_capture_t *)arg;
    memcpy(c->buf + c->total, buf, len);
    c->total += len;
    c->calls++;
    return TAI_OK;
}

static void test_fragmentation(void)
{
    printf("\n[fragmentation]\n");

    /* Build payload larger than TAI_MAX_FRAGMENT_PAYLOAD (32000 bytes) */
    size_t big = 70000;
    uint8_t *app = (uint8_t *)malloc(big);
    for (size_t i = 0; i < big; i++) app[i] = (uint8_t)(i & 0xFF);

    /* Scratch and capture buffers */
    uint8_t scratch[TAI_TX_FRAME_BUF_SIZE];
    uint8_t *captured = (uint8_t *)malloc(big + 4096);
    frag_capture_t cap = { captured, 0, 0 };

    uint8_t sign_key[32]; memset(sign_key, 0x55, 32);
    uint16_t seq = 0;

    TEST("fragment 70000-byte payload (sig_len=32)");
    int rc = tai_frame_fragment(app, big, &seq, sign_key, 32, &g_stub_pal,
                                 scratch, sizeof(scratch), capture_send, &cap);
    CHECK(rc == TAI_OK);
    /* 70000 / 32000 = 3 fragments (32000 + 32000 + 6000) */
    CHECK(cap.calls == 3);
    PASS();

    TEST("fragment flags: FIRST, MIDDLE, LAST");
    /* Inspect frag flags in the captured stream */
    /* Frame 1: offset 0, flags byte[0] */
    uint8_t f1 = (captured[0] >> 6) & 0x03;
    /* Frame 2 starts at 5 + 32000 + 32 = 32037 */
    uint8_t f2 = (captured[32037] >> 6) & 0x03;
    /* Frame 3 starts at 32037 + 5 + 32000 + 32 = 64074 */
    uint8_t f3 = (captured[64074] >> 6) & 0x03;
    CHECK(f1 == TAI_FRAG_FIRST);
    CHECK(f2 == TAI_FRAG_MIDDLE);
    CHECK(f3 == TAI_FRAG_LAST);
    PASS();

    free(app);
    free(captured);
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
    CHECK(nattrs == 4);
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
 * main
 * ------------------------------------------------------------------------- */
int main(void)
{
    printf("=== Tuya AI Foundation — unit tests ===\n");

    test_varint();
    test_attrs_v21();
    test_packet_v21();
    test_transport();
    test_fragmentation();
    test_payloads();
    test_crypto();
    test_proto_client_hello();

    printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
