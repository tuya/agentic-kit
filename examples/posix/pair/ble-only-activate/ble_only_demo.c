/**
 * @file ble_only_demo.c
 * @brief BLE-only provisioning with App-assisted activation, in one process.
 *
 * Flow (see ble_only_demo.h for the big picture):
 *  1. Cloud leg (token mode): activate the device on the cloud with its own
 *     uuid/authkey -- the registration a real App triggers -- and harvest
 *     devid / local_key / secret_key / schema_id.
 *  2. BLE leg, "App" side driving the device state machine over loopback:
 *     unbound device-info handshake (KEY_11) -> pair (KEY_12) -> 0x001B
 *     activation JSON delivery -> ACK 0x00 -> the device flips to the bound
 *     form -> bound handshake (KEY_14/KEY_15) -> one DP write.
 *  3. The device-side activation callback prints and persists what a real
 *     product must store across reboots (ble-only-device-state.json).
 */

#include "ble_only_demo.h"
#include "iot_client.h"
#include "tuya_ble_prov.h"

#include "mbedtls/aes.h"
#include "mbedtls/md5.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define TAG "ble_only"

/* Host entropy for the legacy void HAL (the checked random_fn above is what
 * the state actually uses; this satisfies the link and stays demo-grade). */
void tuya_ble_hal_random(uint8_t *buf, size_t len)
{
    FILE *f = fopen("/dev/urandom", "rb");
    if (f) {
        size_t got = fread(buf, 1, len, f);
        fclose(f);
        if (got == len) return;
    }
    for (size_t i = 0; i < len; i++) buf[i] = (uint8_t)rand();
}

#define FAIL(...) do { fprintf(stderr, "[%s] ", TAG); fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n"); return -1; } while (0)
#define STEP(...) do { printf("[%s] ", TAG); printf(__VA_ARGS__); printf("\n"); } while (0)

/* -- In-process loopback peer: the "App" side of the BLE link -------------- */

#define PACKET_CAP 8

typedef struct {
    uint8_t packets[PACKET_CAP][1024];
    size_t lengths[PACKET_CAP];
    size_t used, total;
    unsigned complete, next, entropy;
    /* Device-side captures. */
    char dev_id[32], schema[64], local_key[17], secret_key[17], json[768];
    unsigned activations, writes;
    uint8_t written_tlv[64];
    size_t written_len;
} demo_peer_t;

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v;
}
static uint32_t get32(const uint8_t *p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}
static uint16_t checksum(const uint8_t *p, size_t len)
{
    uint16_t v = 0xffff;
    while (len--) {
        v ^= *p++;
        for (unsigned j = 0; j < 8; j++) v = (v & 1) ? (v >> 1) ^ 0xa001 : v >> 1;
    }
    return v;
}
static size_t varint(uint8_t *p, uint32_t v)
{
    size_t n = 0;
    do { p[n++] = (v & 127) | (v > 127 ? 128 : 0); v >>= 7; } while (v);
    return n;
}
static int read_varint(const uint8_t *p, size_t n, size_t *offset, uint32_t *value)
{
    *value = 0;
    for (unsigned shift = 0; shift < 28 && *offset < n; shift += 7) {
        uint8_t byte = p[(*offset)++];
        *value |= (uint32_t)(byte & 127) << shift;
        if (!(byte & 128)) return 0;
    }
    return -1;
}

/* The "GATT write" sink: Trsmitr segments in, whole encrypted Packets out. */
static int capture(const uint8_t *p, uint16_t n, void *ctx)
{
    demo_peer_t *peer = ctx;
    size_t offset = 0;
    uint32_t part;
    if (peer->complete >= PACKET_CAP || read_varint(p, n, &offset, &part)) return -1;
    if (!part) {
        uint32_t total;
        if (peer->used || read_varint(p, n, &offset, &total) || offset >= n) return -1;
        if ((p[offset++] >> 4) != 4 || total > sizeof(peer->packets[0])) return -1;
        peer->total = total;
        peer->next = 0;
    }
    if (part != peer->next++ || offset >= n || peer->used + n - offset > peer->total) return -1;
    memcpy(peer->packets[peer->complete] + peer->used, p + offset, n - offset);
    peer->used += n - offset;
    if (peer->used == peer->total) {
        peer->lengths[peer->complete++] = peer->used;
        peer->used = 0;
    }
    return 0;
}

static int random_bytes(uint8_t *p, size_t len, void *ctx)
{
    demo_peer_t *peer = ctx;
    for (size_t i = 0; i < len; i++) p[i] = (uint8_t)(peer->entropy + i);
    peer->entropy += 17;
    return (int)len;
}

static int activation_capture(const tuya_ble_activation_info_t *info, void *ctx)
{
    demo_peer_t *peer = ctx;
    peer->activations++;
    snprintf(peer->dev_id, sizeof(peer->dev_id), "%s", info->dev_id ? info->dev_id : "");
    snprintf(peer->schema, sizeof(peer->schema), "%s", info->schema ? info->schema : "");
    snprintf(peer->json, sizeof(peer->json), "%s", info->json);
    memcpy(peer->local_key, info->local_key, 16); peer->local_key[16] = '\0';
    memcpy(peer->secret_key, info->secret_key, 16); peer->secret_key[16] = '\0';
    printf("[device] activation delivery received -- persist these:\n");
    printf("[device]   devID    : %s\n", peer->dev_id);
    printf("[device]   schema   : %s\n", peer->schema);
    printf("[device]   localKey : %s\n", peer->local_key);
    printf("[device]   secKey   : %s\n", peer->secret_key);
    FILE *f = fopen("ble-only-device-state.json", "w");
    if (f) {
        fprintf(f, "%s\n", info->json);
        fclose(f);
        printf("[device]   (written to ble-only-device-state.json)\n");
    }
    return 0;
}

static int write_dps(const uint8_t *tlv, size_t len, void *ctx)
{
    demo_peer_t *peer = ctx;
    if (len > sizeof(peer->written_tlv)) return -1;
    peer->writes++;
    memcpy(peer->written_tlv, tlv, len);
    peer->written_len = len;
    return 0;
}

/* AES-CBC; the IV the "phone" uses is also the key_11 input. */
static const uint8_t phone_iv[16] = {19,18,17,16,15,14,13,12,11,10,9,8,7,6,5,4};

static int crypt(int encrypt, const uint8_t key[16], const uint8_t iv[16],
                 const uint8_t *in, size_t len, uint8_t *out)
{
    mbedtls_aes_context aes;
    uint8_t copy[16];
    memcpy(copy, iv, sizeof(copy));
    mbedtls_aes_init(&aes);
    int rc = mbedtls_aes_setkey_enc(&aes, key, 128);
    if (rc == 0 && !encrypt) rc = mbedtls_aes_setkey_dec(&aes, key, 128);
    if (rc == 0) rc = mbedtls_aes_crypt_cbc(&aes, encrypt ? MBEDTLS_AES_ENCRYPT : MBEDTLS_AES_DECRYPT,
                                            len, copy, in, out);
    mbedtls_aes_free(&aes);
    return rc;
}

/* Frame: [sn 4][ack 4][cmd 2][len 2][data][crc16 2]; Packet: [mode][IV][CT]. */
static size_t make_packet(uint8_t *packet, uint8_t mode, const uint8_t key[16],
                          uint16_t command, uint32_t sn, const void *data, size_t len)
{
    uint8_t frame[512] = {0};
    if (len > sizeof(frame) - 30) return 0;
    put32(frame, sn);
    frame[8] = command >> 8; frame[9] = command;
    frame[10] = len >> 8; frame[11] = len;
    if (len) memcpy(frame + 12, data, len);
    uint16_t crc = checksum(frame, 12 + len);
    frame[12 + len] = crc >> 8; frame[13 + len] = crc;
    size_t exact = len + 14;
    packet[0] = mode;
    if (!key) { memcpy(packet + 1, frame, exact); return exact + 1; }
    size_t padded = (exact + 15) & ~(size_t)15;
    memset(frame + exact, padded - exact, padded - exact);
    memcpy(packet + 1, phone_iv, 16);
    if (crypt(1, key, phone_iv, frame, padded, packet + 17)) return 0;
    return padded + 17;
}

static int receive(tuya_ble_prov_state_t *state, uint8_t mode, const uint8_t key[16],
                   uint16_t command, uint32_t sn, const void *data, size_t len)
{
    uint8_t packet[512];
    size_t n = make_packet(packet, mode, key, command, sn, data, len);
    if (!n) FAIL("packet build failed (cmd=0x%04X)", command);
    /* Trsmitr framing: [subpkg varint][(first) total varint + version byte]
     * [chunk], one GATT-sized write per segment. */
    size_t sent = 0;
    for (uint32_t part = 0; sent < n; part++) {
        uint8_t segment[20];
        size_t offset = varint(segment, part);
        if (!part) {
            offset += varint(segment + offset, (uint32_t)n);
            segment[offset++] = 0x40; /* version 4 << 4 | seq 0 */
        }
        size_t count = n - sent;
        if (count > sizeof(segment) - offset) count = sizeof(segment) - offset;
        memcpy(segment + offset, packet + sent, count);
        if (tuya_ble_prov_on_data(state, segment, (uint16_t)(offset + count)) != 0)
            FAIL("segment %u rejected (cmd=0x%04X)", part, command);
        sent += count;
    }
    return 0;
}

static int response(demo_peer_t *peer, unsigned index, const uint8_t key[16],
                    uint8_t mode, uint16_t command, uint32_t ack, uint8_t *data, size_t *len)
{
    uint8_t frame[1024];
    if (index >= peer->complete || peer->lengths[index] < 33) FAIL("no response %u", index);
    const uint8_t *packet = peer->packets[index];
    size_t n = peer->lengths[index] - 17;
    if (packet[0] != mode || n % 16) FAIL("response %u not mode %u", index, mode);
    if (crypt(0, key, packet + 1, packet + 17, n, frame)) FAIL("decrypt failed");
    if (get32(frame + 4) != ack || ((unsigned)frame[8] << 8 | frame[9]) != command)
        FAIL("response %u header mismatch", index);
    size_t size = (size_t)frame[10] << 8 | frame[11];
    if (checksum(frame, size + 12) != ((unsigned)frame[size + 12] << 8 | frame[size + 13]))
        FAIL("response %u CRC mismatch", index);
    if (data) memcpy(data, frame + 12, size);
    if (len) *len = size;
    return 0;
}

static void clear_packets(demo_peer_t *peer)
{
    peer->complete = 0;
    peer->used = 0;
}

/* -- The BLE leg ------------------------------------------------------------ */

static const uint8_t dev_info_req[] = {0, 244}; /* PacketMaxSize we offer. */

/* KEY_11 = MD5(auth_key || BLE ID || phone IV). */
static int key11_of(const tuya_ble_prov_state_t *state, uint8_t key[16])
{
    uint8_t material[64];
    memcpy(material, state->cfg.auth_key, 32);
    memcpy(material + 32, state->ble_id, 16);
    memcpy(material + 48, phone_iv, 16);
    return mbedtls_md5(material, sizeof(material), key);
}

static int unbound_handshake(tuya_ble_prov_state_t *state, demo_peer_t *peer, uint8_t key12[16])
{
    uint8_t first[16], data[512], material[22];
    size_t len;
    if (key11_of(state, first) != 0) FAIL("key11 derivation failed");
    if (receive(state, 11, first, 0, 1, dev_info_req, sizeof(dev_info_req)) != 0)
        FAIL("device-info request rejected");
    if (response(peer, 0, first, 11, 0, 1, data, &len) != 0) FAIL("no device-info response");
    printf("[app] device-info: protocol %u.%u, bound=%u\n", data[2], data[3], data[5]);
    memcpy(material, first, 16);
    memcpy(material + 16, data + 6, 6); /* pair_rand */
    if (mbedtls_md5(material, sizeof(material), key12) != 0) FAIL("key12 derivation failed");
    if (receive(state, 12, key12, 1, 2, state->ble_id, 16) != 0) FAIL("pair request rejected");
    if (response(peer, 1, key12, 12, 1, 2, data, &len) != 0 || len != 1 || data[0] != 0)
        FAIL("pair failed (result=%u)", len == 1 ? data[0] : 255u);
    STEP("paired (KEY_12 session, result=0)");
    clear_packets(peer);
    return 0;
}

static int bound_control_round(tuya_ble_prov_state_t *state, demo_peer_t *peer)
{
    uint8_t key14[16], key15[16], data[512], material[38];
    size_t len;
    /* Bound device-info: KEY_14 = MD5(localKey || secKey). */
    {
        uint8_t m[32];
        memcpy(m, peer->local_key, 16);
        memcpy(m + 16, peer->secret_key, 16);
        if (mbedtls_md5(m, sizeof(m), key14) != 0) FAIL("key14 derivation failed");
    }
    if (receive(state, 14, key14, 0, 4, dev_info_req, sizeof(dev_info_req)) != 0)
        FAIL("bound device-info rejected");
    if (response(peer, 0, key14, 14, 0, 4, data, &len) != 0) FAIL("no bound device-info response");
    /* KEY_15 = MD5(localKey || secKey || pair_rand). */
    memcpy(material, peer->local_key, 16);
    memcpy(material + 16, peer->secret_key, 16);
    memcpy(material + 32, data + 6, 6);
    if (mbedtls_md5(material, sizeof(material), key15) != 0) FAIL("key15 derivation failed");
    if (receive(state, 15, key15, 1, 5, state->ble_id, 16) != 0) FAIL("bound pair rejected");
    if (response(peer, 1, key15, 15, 1, 5, data, &len) != 0 || len != 1 || data[0] != 2)
        FAIL("bound pair failed (result=%u)", len == 1 ? data[0] : 255u);
    STEP("bound pairing complete (result=2, KEY_15 session)");
    clear_packets(peer);
    /* One DP write: dp 1, bool true. Payload = 5-byte prefix + DP TLV. */
    uint8_t tlv[] = {1, 1, 0, 1, 1};
    uint8_t command[16];
    memcpy(command, (uint8_t[]){0, 0x12, 0x34, 0x56, 0x78}, 5);
    memcpy(command + 5, tlv, sizeof(tlv));
    if (receive(state, 15, key15, 0x27, 6, command, sizeof(tlv) + 5) != 0)
        FAIL("DP write rejected");
    if (peer->writes != 1 || peer->written_len != sizeof(tlv) || peer->written_tlv[4] != 1)
        FAIL("DP write did not reach the application");
    if (response(peer, 0, key15, 15, 0x27, 6, data, &len) != 0 || len != 6 || data[5] != 0)
        FAIL("DP write ACK missing");
    STEP("DP write dp1=true delivered and ACKed (0x0027, KEY_15)");
    return 0;
}

/* -- Entry point ------------------------------------------------------------ */

int demo_ble_only_run(const char *token,
                      const char *uuid, const char *authkey,
                      const char *product_key)
{
    char devid[64] = "dev1234567890abcdefq";
    char local_key[17] = "local_key_123456";
    char secret_key[17] = "secret_key_12345";
    char schema[64] = "schema001";

    if (iot_init_default() != OPRT_OK) FAIL("iot_init_default failed");

    if (token) {
        STEP("cloud leg: activating on the cloud (App-assisted, uuid=%s)", uuid);
        iot_on_boarding_config_t cfg = {
            .timeout_ms = 10000, .env = PROD, .mqtt_disable_tls = false,
        };
        snprintf(cfg.uuid, sizeof(cfg.uuid), "%s", uuid);
        snprintf(cfg.authkey, sizeof(cfg.authkey), "%s", authkey);
        snprintf(cfg.product_key, sizeof(cfg.product_key), "%s", product_key);
        iot_client_t *client = iot_client_init_on_boarding_with_token(&cfg, token);
        if (!client) FAIL("cloud activation failed (token=%s)", token);
        snprintf(devid, sizeof(devid), "%s", client->devid);
        snprintf(local_key, sizeof(local_key), "%s", client->local_key);
        snprintf(secret_key, sizeof(secret_key), "%s", client->secret_key);
        snprintf(schema, sizeof(schema), "%s", client->schema_id);
        STEP("cloud leg: devid=%s schema=%s", devid, schema);
        iot_client_deinit(client); /* The device never held this client. */
    } else {
        STEP("offline dry run: synthetic activation payload (pass a token for the cloud leg)");
    }

    /* The 0x001B payload per protocol 8.32 发送设备激活信息. */
    char payload[512];
    int n = snprintf(payload, sizeof(payload),
                     "{\"devID\":\"%s\",\"secKey\":\"%s\",\"localKey\":\"%s\","
                     "\"schema\":\"%s\"}",
                     devid, secret_key, local_key, schema);
    if (n <= 0 || (size_t)n >= sizeof(payload)) FAIL("activation JSON too large");

    tuya_ble_prov_state_t state;
    demo_peer_t peer;
    memset(&peer, 0, sizeof(peer));
    tuya_ble_prov_cfg_ext_t cfg = {
        .device_name = "ble-only-demo",
        .product_key = product_key,
        .uuid = uuid,
        .auth_key = authkey,
        .send_fn = capture,
        .send_ctx = &peer,
        .random_fn = random_bytes,
        .random_ctx = &peer,
        .activation_info = activation_capture,
        .activation_ctx = &peer,
        .dp_write = write_dps,
        .dp_ctx = &peer,
    };
    if (tuya_ble_prov_init(&state, &cfg) != 0) FAIL("device init failed");
    STEP("device advertising: adv=%u bytes, rsp=%u bytes", state.adv_len, state.rsp_len);

    uint8_t key12[16];
    if (unbound_handshake(&state, &peer, key12) != 0) return -1;

    if (receive(&state, 12, key12, 0x1B, 3, payload, (size_t)n) != 0)
        FAIL("activation delivery rejected");
    if (peer.activations != 1) FAIL("device did not accept the activation JSON");
    {
        uint8_t reply[512];
        size_t len;
        if (response(&peer, 0, key12, 12, 0x1B, 3, reply, &len) != 0 || len != 1 || reply[0] != 0)
            FAIL("activation ACK missing or negative");
        STEP("activation delivery ACKed 0x00 (0x001B, KEY_12)");
    }
    if (state.cfg.local_key != state.bound_local_key)
        FAIL("device did not flip to the bound form");
    clear_packets(&peer);

    if (bound_control_round(&state, &peer) != 0) return -1;

    STEP("BLE-only provisioning complete: bound local control works with no WiFi radio");
    return 0;
}
