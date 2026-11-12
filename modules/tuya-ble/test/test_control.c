/* Independent bound-device peer: public GATT input, locally built Frames and
 * independently decrypted notifications. All credentials are synthetic. */
#include "tuya_ble_prov.h"
#include "tuya_ble_dp.h"
#include "mbedtls/aes.h"
#include "mbedtls/md5.h"
#include "cJSON.h"
#include <stdio.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); return 1; } } while (0)
#define PACKETS 24
static const char product[] = "0123456789abcdef";
static const char identity[] = "abcdefghijklmnop";
static const char auth[] = "0123456789abcdef0123456789abcdef";
static const char local_key[] = "local_key_123456";
static const char secret_key[] = "secret_key_12345";
static const uint8_t phone_iv[16] = {19,18,17,16,15,14,13,12,11,10,9,8,7,6,5,4};
static unsigned credentials_called;

typedef struct {
    uint8_t packets[PACKETS][1024];
    size_t lengths[PACKETS], total, used, budget;
    unsigned complete, next, notifications, entropy;
    int busy, bad, write_rc, query_rc, entropy_fail;
    unsigned writes, queries;
    uint8_t written_tlv[512], queried_ids[256];
    size_t written_len, queried_count;
    /* Single-mode binding scenario state. */
    unsigned activations, unbound_called;
    int activation_rc;
    char activation_json[TUYA_BLE_RX_BUF_SIZE + 1];
    char activation_dev_id[32], activation_schema[64];
    char activation_local[17], activation_secret[17];
} control_peer_t;

static void put32(uint8_t *p, uint32_t value)
{
    p[0] = value >> 24; p[1] = value >> 16; p[2] = value >> 8; p[3] = value;
}
static uint32_t get32(const uint8_t *p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}
static uint16_t checksum(const uint8_t *p, size_t len)
{
    uint16_t value = 0xffff;
    while (len--) {
        value ^= *p++;
        for (unsigned j = 0; j < 8; j++) value = (value & 1) ? (value >> 1) ^ 0xa001 : value >> 1;
    }
    return value;
}
static size_t varint(uint8_t *p, uint32_t value)
{
    size_t n = 0;
    do { p[n++] = (value & 127) | (value > 127 ? 128 : 0); value >>= 7; } while (value);
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
static int capture(const uint8_t *p, uint16_t n, void *ctx)
{
    control_peer_t *peer = ctx;
    if (peer->busy) return TUYA_BLE_SEND_BUSY;
    size_t offset = 0;
    uint32_t part;
    if (n > peer->budget || peer->complete >= PACKETS || read_varint(p, n, &offset, &part)) goto bad;
    if (!part) {
        uint32_t total;
        if (peer->used || read_varint(p, n, &offset, &total) || offset >= n) goto bad;
        if ((p[offset++] >> 4) != 4 || total > sizeof(peer->packets[0])) goto bad;
        peer->total = total;
        peer->next = 0;
    }
    if (part != peer->next++ || offset >= n || peer->used + n - offset > peer->total) goto bad;
    memcpy(peer->packets[peer->complete] + peer->used, p + offset, n - offset);
    peer->used += n - offset;
    peer->notifications++;
    if (peer->used == peer->total) {
        peer->lengths[peer->complete++] = peer->used;
        peer->used = 0;
    }
    return 0;
bad:
    peer->bad = 1;
    return -1;
}
static int random_bytes(uint8_t *p, size_t len, void *ctx)
{
    control_peer_t *peer = ctx;
    if (peer->entropy_fail) return -1;
    for (size_t i = 0; i < len; i++) p[i] = (uint8_t)(peer->entropy + i);
    peer->entropy += 17;
    return (int)len;
}
static void credentials(const tuya_ble_wifi_creds_t *creds)
{
    (void)creds;
    credentials_called++;
}
static int write_dps(const uint8_t *tlv, size_t len, void *ctx)
{
    control_peer_t *peer = ctx;
    if (len > sizeof(peer->written_tlv)) return -1;
    peer->writes++;
    memcpy(peer->written_tlv, tlv, len);
    peer->written_len = len;
    return peer->write_rc;
}
static int query_dps(const uint8_t *ids, size_t count, void *ctx)
{
    control_peer_t *peer = ctx;
    if (count > sizeof(peer->queried_ids)) return -1;
    peer->queries++;
    if (count) memcpy(peer->queried_ids, ids, count);
    peer->queried_count = count;
    return peer->query_rc;
}
static int activation_reply(const tuya_ble_activation_info_t *info, void *ctx)
{
    control_peer_t *peer = ctx;
    peer->activations++;
    if (peer->activation_rc) return peer->activation_rc;
    snprintf(peer->activation_json, sizeof(peer->activation_json), "%s", info->json);
    snprintf(peer->activation_dev_id, sizeof(peer->activation_dev_id), "%s",
             info->dev_id ? info->dev_id : "");
    snprintf(peer->activation_schema, sizeof(peer->activation_schema), "%s",
             info->schema ? info->schema : "");
    memcpy(peer->activation_local, info->local_key, 16);
    peer->activation_local[16] = '\0';
    memcpy(peer->activation_secret, info->secret_key, 16);
    peer->activation_secret[16] = '\0';
    return 0;
}
static void unbound_notification(void *ctx)
{
    ((control_peer_t *)ctx)->unbound_called++;
}
static int crypt(int encrypt, const uint8_t key[16], const uint8_t iv[16],
                 const uint8_t *input, size_t len, uint8_t *output)
{
    mbedtls_aes_context aes;
    uint8_t copy[16];
    memcpy(copy, iv, sizeof(copy));
    mbedtls_aes_init(&aes);
    int rc = encrypt ? mbedtls_aes_setkey_enc(&aes, key, 128) : mbedtls_aes_setkey_dec(&aes, key, 128);
    if (!rc) rc = mbedtls_aes_crypt_cbc(&aes, encrypt ? MBEDTLS_AES_ENCRYPT : MBEDTLS_AES_DECRYPT,
                                       len, copy, input, output);
    mbedtls_aes_free(&aes);
    return rc;
}
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
static int deliver(tuya_ble_prov_state_t *state, const uint8_t *packet, size_t len)
{
    CHECK(len > 0);
    size_t sent = 0;
    for (uint32_t part = 0; sent < len; part++) {
        uint8_t segment[20];
        size_t offset = varint(segment, part);
        if (!part) { offset += varint(segment + offset, len); segment[offset++] = 0x40; }
        size_t count = len - sent;
        if (count > sizeof(segment) - offset) count = sizeof(segment) - offset;
        memcpy(segment + offset, packet + sent, count);
        CHECK(tuya_ble_prov_on_data(state, segment, offset + count) == 0);
        sent += count;
    }
    return 0;
}
static int receive(tuya_ble_prov_state_t *state, uint8_t mode, const uint8_t key[16],
                   uint16_t command, uint32_t sn, const void *data, size_t len)
{
    uint8_t packet[512];
    return deliver(state, packet, make_packet(packet, mode, key, command, sn, data, len));
}
static int response(control_peer_t *peer, unsigned index, const uint8_t key[16], uint8_t mode,
                    uint16_t command, uint32_t ack, uint8_t *data, size_t *len)
{
    uint8_t frame[1024];
    CHECK(!peer->bad && index < peer->complete && peer->lengths[index] >= 33);
    const uint8_t *packet = peer->packets[index];
    size_t n = peer->lengths[index] - 17;
    CHECK(packet[0] == mode && !(n % 16));
    CHECK(crypt(0, key, packet + 1, packet + 17, n, frame) == 0);
    CHECK(get32(frame) != 0 && get32(frame + 4) == ack);
    CHECK(((unsigned)frame[8] << 8 | frame[9]) == command);
    size_t size = (size_t)frame[10] << 8 | frame[11];
    CHECK(size + 14 <= n && checksum(frame, size + 12) == ((unsigned)frame[size + 12] << 8 | frame[size + 13]));
    if (data) memcpy(data, frame + 12, size);
    if (len) *len = size;
    return 0;
}
static int setup(tuya_ble_prov_state_t *state, control_peer_t *peer, int bound)
{
    memset(peer, 0, sizeof(*peer));
    peer->budget = 20;
    peer->entropy = 31;
    credentials_called = 0;
    tuya_ble_prov_cfg_ext_t cfg = {
        .device_name = "test", .product_key = product, .uuid = identity, .auth_key = auth,
        .send_fn = capture, .send_ctx = peer, .random_fn = random_bytes, .random_ctx = peer,
        .cb = credentials, .local_key = bound ? local_key : NULL, .secret_key = bound ? secret_key : NULL,
        .dp_write = write_dps, .dp_query = query_dps, .dp_ctx = peer,
        .activation_info = activation_reply, .activation_ctx = peer,
        .unbound = unbound_notification, .unbound_ctx = peer
    };
    CHECK(tuya_ble_prov_init(state, &cfg) == 0);
    return 0;
}
static void clear_packets(control_peer_t *peer)
{
    peer->complete = peer->next = peer->notifications = 0;
    peer->used = peer->total = 0;
}
static int key14(uint8_t key[16])
{
    uint8_t material[32];
    memcpy(material, local_key, 16); memcpy(material + 16, secret_key, 16);
    return mbedtls_md5(material, sizeof(material), key);
}
static int key11(uint8_t key[16])
{
    uint8_t material[64];
    memcpy(material, auth, 32); memcpy(material + 32, identity, 16); memcpy(material + 48, phone_iv, 16);
    return mbedtls_md5(material, sizeof(material), key);
}
static int handshake(tuya_ble_prov_state_t *state, control_peer_t *peer, uint8_t key[16])
{
    uint8_t first[16], data[512], material[38], request[] = {0, 244};
    size_t len;
    CHECK(setup(state, peer, 1) == 0 && key14(first) == 0);
    CHECK(receive(state, 14, first, 0, 1, request, sizeof(request)) == 0);
    CHECK(peer->complete == 1 && state->handshake_ready && !state->authenticated);
    CHECK(response(peer, 0, first, 14, 0, 1, data, &len) == 0);
    CHECK(len >= 12 && data[2] == 4 && data[3] == 4 && data[5] == 1);
    memcpy(material, local_key, 16); memcpy(material + 16, secret_key, 16); memcpy(material + 32, data + 6, 6);
    CHECK(mbedtls_md5(material, sizeof(material), key) == 0);
    CHECK(receive(state, 15, key, 1, 2, identity, 16) == 0);
    CHECK(state->authenticated && state->paired && peer->complete >= 2);
    CHECK(response(peer, 1, key, 15, 1, 2, data, &len) == 0 && len == 1 && data[0] == 2);
    CHECK(peer->notifications > 10); /* Every direction uses the default low MTU. */
    clear_packets(peer);
    return 0;
}

/* Unbound (provisioning) form: KEY_11 device-info handshake plus KEY_12
 * pair, ending in an authenticated unbound session. pair_rand_out (may be
 * NULL) receives the handshake's pair random. */
static int unbound_handshake(tuya_ble_prov_state_t *state, control_peer_t *peer,
                             uint8_t key[16], uint8_t pair_rand_out[6])
{
    uint8_t first[16], data[512], material[22], request[] = {0, 244};
    size_t len;
    CHECK(setup(state, peer, 0) == 0 && key11(first) == 0);
    CHECK(receive(state, 11, first, 0, 1, request, sizeof(request)) == 0);
    CHECK(peer->complete == 1 && state->handshake_ready && !state->authenticated);
    CHECK(response(peer, 0, first, 11, 0, 1, data, &len) == 0);
    CHECK(len >= 12 && data[5] == 0);
    memcpy(material, first, 16); memcpy(material + 16, data + 6, 6);
    if (pair_rand_out) memcpy(pair_rand_out, data + 6, 6);
    CHECK(mbedtls_md5(material, sizeof(material), key) == 0);
    CHECK(receive(state, 12, key, 1, 2, identity, 16) == 0);
    CHECK(state->authenticated && state->paired && peer->complete >= 2);
    CHECK(response(peer, 1, key, 12, 1, 2, data, &len) == 0 && len == 1 && data[0] == 0);
    clear_packets(peer);
    return 0;
}

static const uint8_t all_types[] = {
    1, 0, 0, 3, 0, 1, 255,
    2, 1, 0, 1, 1,
    3, 2, 0, 4, 255, 255, 255, 214,
    4, 3, 0, 4, 'h', 'i', 0xc3, 0xa9,
    5, 4, 0, 2, 1, 2,
    6, 5, 0, 4, 0x80, 0, 0, 1
};
static size_t dp_command(uint8_t *out, const uint8_t *tlv, size_t len)
{
    static const uint8_t prefix[] = {0, 0x12, 0x34, 0x56, 0x78};
    memcpy(out, prefix, sizeof(prefix)); memcpy(out + sizeof(prefix), tlv, len);
    return len + sizeof(prefix);
}
static int bound_advertisement(void)
{
    tuya_ble_prov_state_t state;
    control_peer_t peer;
    uint8_t digest[16], text[16], encrypted[16], decrypted[16], rspkey[16];
    static const char hex[] = "0123456789ABCDEF";
    CHECK(setup(&state, &peer, 1) == 0);
    CHECK(state.adv_len == 31 && state.rsp_len <= 31);
    CHECK((state.adv_data[11] & 8) && (state.rsp_data[7] & 128));
    CHECK(memcmp(state.adv_data + 15, product, 16) != 0);
    for (unsigned i = 0; i < 8; i++) { text[i * 2] = hex[(uint8_t)identity[i] >> 4]; text[i * 2 + 1] = hex[(uint8_t)identity[i] & 15]; }
    CHECK(mbedtls_md5((const uint8_t *)secret_key, 16, digest) == 0);
    mbedtls_aes_context aes;
    mbedtls_aes_init(&aes);
    CHECK(mbedtls_aes_setkey_enc(&aes, digest, 128) == 0);
    CHECK(mbedtls_aes_crypt_ecb(&aes, MBEDTLS_AES_ENCRYPT, text, encrypted) == 0);
    mbedtls_aes_free(&aes);
    CHECK(memcmp(encrypted, state.adv_data + 15, 16) == 0);
    CHECK(mbedtls_md5(state.adv_data + 11, 20, rspkey) == 0);
    CHECK(crypt(0, rspkey, rspkey, state.rsp_data + 8, 16, decrypted) == 0);
    CHECK(memcmp(decrypted, identity, 16) == 0);
    CHECK(setup(&state, &peer, 0) == 0);
    CHECK(!(state.adv_data[11] & 8) && !(state.rsp_data[7] & 128));
    CHECK(memcmp(state.adv_data + 15, product, 16) == 0);
    return 0;
}
static int bound_control_and_query(void)
{
    tuya_ble_prov_state_t state;
    control_peer_t peer;
    uint8_t key[16], command[512], reply[512], ids[] = {1, 3};
    size_t len;
    CHECK(handshake(&state, &peer, key) == 0);
    size_t n = dp_command(command, all_types, sizeof(all_types));
    CHECK(receive(&state, 15, key, 0x27, 3, command, n) == 0);
    CHECK(peer.writes == 1 && peer.written_len == sizeof(all_types));
    CHECK(memcmp(peer.written_tlv, all_types, sizeof(all_types)) == 0 && peer.complete == 1);
    CHECK(response(&peer, 0, key, 15, 0x27, 3, reply, &len) == 0);
    CHECK(len == 6 && memcmp(reply, command, 5) == 0 && reply[5] == 0);
    CHECK(receive(&state, 15, key, 0x27, 3, command, n) == 0); /* Replay. */
    CHECK(peer.writes == 1 && peer.complete == 1);
    CHECK(receive(&state, 15, key, 3, 4, ids, sizeof(ids)) == 0);
    CHECK(peer.queries == 1 && peer.queried_count == sizeof(ids) && !memcmp(peer.queried_ids, ids, sizeof(ids)));
    CHECK(peer.complete == 2 && response(&peer, 1, key, 15, 3, 4, reply, &len) == 0 && len == 1 && reply[0] == 0);
    CHECK(receive(&state, 15, key, 3, 5, NULL, 0) == 0);
    CHECK(peer.queries == 2 && peer.queried_count == 0 && peer.complete == 3);
    CHECK(response(&peer, 2, key, 15, 3, 5, reply, &len) == 0 && len == 1 && reply[0] == 0);
    const uint8_t zero_id[] = {0}, duplicate_ids[] = {1, 1};
    CHECK(receive(&state, 15, key, 3, 6, zero_id, sizeof(zero_id)) == 0);
    CHECK(peer.queries == 2 && peer.complete == 4);
    CHECK(response(&peer, 3, key, 15, 3, 6, reply, &len) == 0 && len == 1 && reply[0] != 0);
    CHECK(receive(&state, 15, key, 3, 7, duplicate_ids, sizeof(duplicate_ids)) == 0);
    CHECK(peer.queries == 2 && peer.complete == 5);
    CHECK(response(&peer, 4, key, 15, 3, 7, reply, &len) == 0 && len == 1 && reply[0] != 0);
    return 0;
}
static int codec_all_types(void)
{
    char json[512]; size_t written = 99;
    CHECK(tuya_ble_dp_validate(all_types, sizeof(all_types)) == 0);
    CHECK(tuya_ble_dp_to_json(all_types, sizeof(all_types), json, sizeof(json), &written) == 0);
    CHECK(written == strlen(json));
    cJSON *root = cJSON_Parse(json);
    CHECK(root != NULL && cJSON_IsObject(root) && cJSON_GetArraySize(root) == 6);
    CHECK(cJSON_IsString(cJSON_GetObjectItem(root, "1")) && !strcmp(cJSON_GetObjectItem(root, "1")->valuestring, "AAH/"));
    CHECK(cJSON_IsTrue(cJSON_GetObjectItem(root, "2")));
    CHECK(cJSON_GetObjectItem(root, "3")->valuedouble == -42);
    CHECK(cJSON_IsString(cJSON_GetObjectItem(root, "4")) && !strcmp(cJSON_GetObjectItem(root, "4")->valuestring, "hi\xc3\xa9"));
    CHECK(cJSON_GetObjectItem(root, "5")->valuedouble == 258);
    CHECK(cJSON_GetObjectItem(root, "6")->valuedouble == 2147483649.0);
    cJSON_Delete(root);
    CHECK(tuya_ble_dp_to_json(all_types, sizeof(all_types), json, 2, &written) != 0 && written == 0 && json[0] == 0);
    const uint8_t empty[] = {1, 0, 0, 0, 2, 3, 0, 0};
    CHECK(tuya_ble_dp_to_json(empty, sizeof(empty), json, sizeof(json), &written) == 0);
    CHECK(!strcmp(json, "{\"1\":\"\",\"2\":\"\"}"));
    return 0;
}
static int invalid_batches_are_atomic(void)
{
    static const uint8_t invalid[][12] = {
        {1,1,0,1,2},                 /* Invalid Boolean. */
        {1,2,0,3,1,2,3},             /* Wrong signed-value width. */
        {1,4,0,3,1,2,3},             /* Wrong enum width. */
        {1,5,0,3,1,2,3},             /* Wrong bitmap width. */
        {1,3,0,1,0},                 /* Embedded NUL. */
        {1,3,0,2,0xc0,0x80},         /* Noncanonical UTF-8. */
        {1,6,0,1,0},                 /* Unknown TLV type. */
        {1,0,0,2,0},                 /* Truncated raw. */
        {0,1,0,1,1},                 /* Invalid ID. */
        {1,1,0,1,1,1,1,0,1,0}       /* Duplicate ID. */
    };
    static const size_t sizes[] = {5,7,7,7,5,6,5,5,5,10};
    const uint8_t first[] = {9, 1, 0, 1, 1};
    tuya_ble_prov_state_t state;
    control_peer_t peer;
    uint8_t key[16], tlv[40], command[64], reply[64];
    size_t len;
    CHECK(handshake(&state, &peer, key) == 0);
    for (unsigned i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
        char json[100] = "unchanged"; size_t written = 99;
        memcpy(tlv, first, sizeof(first)); memcpy(tlv + sizeof(first), invalid[i], sizes[i]);
        size_t n = sizeof(first) + sizes[i];
        CHECK(tuya_ble_dp_validate(tlv, n) != 0);
        CHECK(tuya_ble_dp_to_json(tlv, n, json, sizeof(json), &written) != 0 && written == 0 && json[0] == 0);
        CHECK(receive(&state, 15, key, 0x27, i + 3, command, dp_command(command, tlv, n)) == 0);
        CHECK(peer.writes == 0 && peer.complete == i + 1 && state.authenticated);
        CHECK(response(&peer, i, key, 15, 0x27, i + 3, reply, &len) == 0);
        CHECK(len == 6 && !memcmp(reply, command, 5) && reply[5] != 0);
    }
    CHECK(tuya_ble_dp_validate(NULL, 0) != 0);
    CHECK(receive(&state, 15, key, 0x27, 30, command, 4) == 0 && peer.writes == 0);
    return 0;
}
static int unauthorized_modes_and_credentials(void)
{
    tuya_ble_prov_state_t state;
    control_peer_t peer;
    uint8_t key[16], initial[16], command[512], request[] = {0, 244};
    size_t len = dp_command(command, all_types, sizeof(all_types));
    CHECK(setup(&state, &peer, 1) == 0 && key14(initial) == 0);
    CHECK(receive(&state, 14, initial, 0x27, 1, command, len) == 0);
    CHECK(!peer.writes && !peer.complete && !state.authenticated && !state.last_rx_sn);
    CHECK(receive(&state, 0, NULL, 0x27, 2, command, len) == 0);
    CHECK(!peer.writes && !peer.complete && !state.last_rx_sn);
    CHECK(receive(&state, 14, initial, 0, 3, request, sizeof(request)) == 0);
    CHECK(peer.complete == 1 && state.handshake_ready && !state.authenticated);
    uint8_t material[38];
    memcpy(material, local_key, 16); memcpy(material + 16, secret_key, 16); memcpy(material + 32, state.pair_rand, 6);
    CHECK(mbedtls_md5(material, sizeof(material), key) == 0);
    CHECK(receive(&state, 15, key, 0x27, 4, command, len) == 0 && !peer.writes && peer.complete == 1);
    CHECK(handshake(&state, &peer, key) == 0);
    CHECK(receive(&state, 14, initial, 0x27, 3, command, len) == 0 && !peer.writes && !peer.complete);
    CHECK(receive(&state, 12, key, 0x27, 4, command, len) == 0 && !peer.writes && !peer.complete);
    CHECK(receive(&state, 0, NULL, 0x27, 5, command, len) == 0 && !peer.writes && !peer.complete);
    const char creds[] = "\0\0\0\1{\"ssid\":\"router\",\"pwd\":\"password\",\"token\":\"1234567890123456\"}";
    CHECK(receive(&state, 15, key, 0x801b, 6, creds, sizeof(creds) - 1) == 0 && !credentials_called && !peer.complete);
    CHECK(receive(&state, 15, key, 0x801e, 7, creds, sizeof(creds) - 1) == 0 && !credentials_called && !peer.complete);
    CHECK(tuya_ble_dp_report(&state, all_types, sizeof(all_types), false) == 0);
    CHECK(peer.complete == 1); /* Invalid modes do not destroy the authenticated session. */
    CHECK(setup(&state, &peer, 0) == 0);
    CHECK(receive(&state, 14, initial, 0, 1, request, sizeof(request)) == 0 && !peer.complete && !state.handshake_ready);
    CHECK(tuya_ble_dp_report(&state, all_types, sizeof(all_types), false) != 0 && !peer.complete);
    /* Completing provisioning Pairing still cannot authorize activated control. */
    uint8_t provisioning_material[64], response_data[512];
    size_t response_len;
    memcpy(provisioning_material, auth, 32);
    memcpy(provisioning_material + 32, identity, 16);
    memcpy(provisioning_material + 48, phone_iv, 16);
    CHECK(mbedtls_md5(provisioning_material, 64, initial) == 0);
    CHECK(receive(&state, 11, initial, 0, 2, request, sizeof(request)) == 0 && peer.complete == 1);
    CHECK(response(&peer, 0, initial, 11, 0, 2, response_data, &response_len) == 0 && response_data[5] == 0);
    memcpy(provisioning_material, initial, 16);
    memcpy(provisioning_material + 16, response_data + 6, 6);
    CHECK(mbedtls_md5(provisioning_material, 22, key) == 0);
    CHECK(receive(&state, 12, key, 1, 3, identity, 16) == 0 && state.authenticated && state.paired);
    CHECK(response(&peer, 1, key, 12, 1, 3, response_data, &response_len) == 0 && response_len == 1 && response_data[0] == 0);
    clear_packets(&peer);
    CHECK(receive(&state, 12, key, 0x27, 4, command, len) == 0 && !peer.writes && !peer.complete);
    CHECK(receive(&state, 12, key, 3, 5, NULL, 0) == 0 && !peer.queries && !peer.complete);
    CHECK(tuya_ble_dp_report(&state, all_types, sizeof(all_types), false) != 0 && !peer.complete);
    return 0;
}
static int explicit_reports_and_acks(void)
{
    tuya_ble_prov_state_t state;
    control_peer_t peer;
    uint8_t key[16], reply[512], ack[] = {0,0,0,0,1,0};
    size_t len;
    CHECK(handshake(&state, &peer, key) == 0);
    CHECK(tuya_ble_dp_report(&state, all_types, sizeof(all_types), false) == 0 && peer.complete == 1);
    CHECK(response(&peer, 0, key, 15, 0x8006, 0, reply, &len) == 0);
    CHECK(len == sizeof(all_types) + 7 && reply[0] == 0 && get32(reply + 1) != 0 && reply[5] == 0 && reply[6] == 0);
    uint32_t first_sn = get32(reply + 1);
    CHECK(!memcmp(reply + 7, all_types, sizeof(all_types)));
    CHECK(tuya_ble_dp_report_with_time(&state, all_types, sizeof(all_types), 1700000000, true) == 0 && peer.complete == 2);
    CHECK(response(&peer, 1, key, 15, 0x8007, 0, reply, &len) == 0);
    CHECK(len == sizeof(all_types) + 12 && reply[0] == 0 && get32(reply + 1) == first_sn + 1);
    CHECK(reply[5] == 1 && reply[6] == 0 && reply[7] == 1 && get32(reply + 8) == 1700000000);
    CHECK(!memcmp(reply + 12, all_types, sizeof(all_types)));
    CHECK(receive(&state, 15, key, 0x8006, 3, ack, sizeof(ack)) == 0 && peer.complete == 2);
    CHECK(receive(&state, 15, key, 0x8007, 4, ack, sizeof(ack)) == 0 && peer.complete == 2);
    const uint8_t bad[] = {1, 1, 0, 1, 2};
    CHECK(tuya_ble_dp_report(&state, bad, sizeof(bad), false) != 0 && peer.complete == 2);
    return 0;
}
static int callback_failure_and_backpressure(void)
{
    tuya_ble_prov_state_t state;
    control_peer_t peer;
    uint8_t key[16], command[512], reply[512];
    size_t len;
    CHECK(handshake(&state, &peer, key) == 0);
    peer.write_rc = -1;
    CHECK(receive(&state, 15, key, 0x27, 3, command, dp_command(command, all_types, sizeof(all_types))) == 0);
    CHECK(peer.writes == 1 && peer.complete == 1 && response(&peer, 0, key, 15, 0x27, 3, reply, &len) == 0);
    CHECK(len == 6 && reply[5] != 0);
    peer.query_rc = -1;
    CHECK(receive(&state, 15, key, 3, 4, NULL, 0) == 0 && peer.queries == 1 && peer.complete == 2);
    CHECK(response(&peer, 1, key, 15, 3, 4, reply, &len) == 0 && len == 1 && reply[0] != 0);
    clear_packets(&peer);
    peer.busy = 1;
    CHECK(tuya_ble_dp_report(&state, all_types, sizeof(all_types), false) == 0 && !peer.complete && state.tx_count == 1);
    peer.busy = 0;
    CHECK(tuya_ble_prov_tx_ready(&state) == 0 && peer.complete == 1 && !state.tx_count);
    CHECK(response(&peer, 0, key, 15, 0x8006, 0, reply, &len) == 0 && len == sizeof(all_types) + 7);
    CHECK(!memcmp(reply + 7, all_types, sizeof(all_types)));
    clear_packets(&peer);
    uint32_t saved_sn = state.sn;
    state.sn = UINT32_MAX;
    CHECK(receive(&state, 15, key, 0x27, 5, command, dp_command(command, all_types, sizeof(all_types))) == 0);
    CHECK(receive(&state, 15, key, 3, 6, NULL, 0) == 0);
    CHECK(peer.writes == 1 && peer.queries == 1 && !peer.complete);
    state.sn = saved_sn;
    state.cfg.send_fn = NULL;
    CHECK(receive(&state, 15, key, 0x27, 7, command, dp_command(command, all_types, sizeof(all_types))) == 0);
    CHECK(receive(&state, 15, key, 3, 8, NULL, 0) == 0);
    CHECK(peer.writes == 1 && peer.queries == 1 && !peer.complete);
    state.cfg.send_fn = capture;
    return 0;
}
static int close_and_device_info_require_fresh_pairing(void)
{
    tuya_ble_prov_state_t state;
    control_peer_t peer;
    uint8_t key[16], initial[16], command[512], request[] = {0,244}, reply[512], material[38];
    size_t len;
    CHECK(handshake(&state, &peer, key) == 0 && key14(initial) == 0);
    size_t n = dp_command(command, all_types, sizeof(all_types));
    uint32_t generation = state.connection_generation;
    tuya_ble_prov_close(&state);
    CHECK(!state.authenticated && !state.paired && !state.handshake_ready);
    CHECK(state.connection_generation != generation);
    generation = state.connection_generation;
    CHECK(receive(&state, 15, key, 0x27, 3, command, n) == 0 && !peer.writes && !peer.complete);
    CHECK(tuya_ble_dp_report(&state, all_types, sizeof(all_types), false) != 0 && !peer.complete);
    CHECK(receive(&state, 14, initial, 0, 4, request, sizeof(request)) == 0 && peer.complete == 1);
    CHECK(state.connection_generation != generation);
    CHECK(response(&peer, 0, initial, 14, 0, 4, reply, &len) == 0 && reply[5] == 1);
    memcpy(material, local_key, 16); memcpy(material + 16, secret_key, 16); memcpy(material + 32, reply + 6, 6);
    uint8_t fresh[16];
    CHECK(mbedtls_md5(material, sizeof(material), fresh) == 0 && memcmp(fresh, key, 16));
    CHECK(receive(&state, 15, fresh, 1, 5, identity, 16) == 0 && state.authenticated);
    clear_packets(&peer);
    generation = state.connection_generation;
    CHECK(receive(&state, 14, initial, 0, 6, request, sizeof(request)) == 0 && peer.complete == 1);
    CHECK(state.connection_generation != generation);
    CHECK(state.handshake_ready && !state.authenticated && !state.paired);
    CHECK(tuya_ble_dp_report(&state, all_types, sizeof(all_types), false) != 0 && peer.complete == 1);
    CHECK(receive(&state, 15, fresh, 0x27, 7, command, n) == 0 && !peer.writes && peer.complete == 1);
    return 0;
}

static int credential_configuration(void)
{
    tuya_ble_prov_state_t state;
    control_peer_t peer;
    CHECK(setup(&state, &peer, 1) == 0);
    tuya_ble_prov_cfg_ext_t good = state.cfg, bad = good;
    bad.local_key = NULL;
    CHECK(tuya_ble_prov_init(&state, &bad) != 0);
    bad = good; bad.secret_key = NULL;
    CHECK(tuya_ble_prov_init(&state, &bad) != 0);
    bad = good; bad.local_key = "123456789012345";
    CHECK(tuya_ble_prov_init(&state, &bad) != 0);
    bad = good; bad.local_key = "12345678901234567";
    CHECK(tuya_ble_prov_init(&state, &bad) != 0);
    bad = good; bad.secret_key = "123456789012345";
    CHECK(tuya_ble_prov_init(&state, &bad) != 0);
    bad = good; bad.secret_key = "12345678901234567";
    CHECK(tuya_ble_prov_init(&state, &bad) != 0);
    CHECK(tuya_ble_prov_init(&state, &good) == 0);
    return 0;
}

static int bound_entropy_failure(void)
{
    tuya_ble_prov_state_t state;
    control_peer_t peer;
    uint8_t initial[16], key[16], request[] = {0, 244};
    CHECK(setup(&state, &peer, 1) == 0 && key14(initial) == 0);
    peer.entropy_fail = 1;
    CHECK(receive(&state, 14, initial, 0, 1, request, sizeof(request)) == 0);
    CHECK(!state.handshake_ready && !state.authenticated && !peer.complete);
    uint8_t zero[6] = {0};
    CHECK(!memcmp(state.pair_rand, zero, sizeof(zero)));
    CHECK(handshake(&state, &peer, key) == 0);
    uint32_t report_sn = state.dp_report_sn;
    peer.entropy_fail = 1;
    CHECK(tuya_ble_dp_report(&state, all_types, sizeof(all_types), false) != 0);
    CHECK(!peer.complete && !state.tx_count && state.dp_report_sn == report_sn);
    uint32_t generation = state.connection_generation;
    CHECK(receive(&state, 14, initial, 0, 4, request, sizeof(request)) == 0);
    CHECK(!state.handshake_ready && !state.authenticated && !state.paired && !peer.complete);
    CHECK(state.connection_generation != generation);
    return 0;
}

static int report_capacity_and_size_boundaries(void)
{
    tuya_ble_prov_state_t state;
    control_peer_t peer;
    uint8_t key[16], reply[1024], large[972], command[512];
    size_t len;
    CHECK(handshake(&state, &peer, key) == 0);
    peer.busy = 1;
    for (unsigned i = 0; i < TUYA_BLE_TX_QUEUE_DEPTH; i++)
        CHECK(tuya_ble_dp_report(&state, all_types, sizeof(all_types), false) == 0);
    CHECK(state.tx_count == TUYA_BLE_TX_QUEUE_DEPTH && !peer.complete);
    uint32_t sequence = state.dp_report_sn;
    CHECK(tuya_ble_dp_report(&state, all_types, sizeof(all_types), false) != 0);
    CHECK(state.tx_count == TUYA_BLE_TX_QUEUE_DEPTH && state.dp_report_sn == sequence);
    CHECK(receive(&state, 15, key, 0x27, 3, command, dp_command(command, all_types, sizeof(all_types))) == 0);
    CHECK(receive(&state, 15, key, 3, 4, NULL, 0) == 0);
    CHECK(!peer.writes && !peer.queries && state.tx_count == TUYA_BLE_TX_QUEUE_DEPTH);
    peer.busy = 0;
    CHECK(tuya_ble_prov_tx_ready(&state) == 0 && peer.complete == TUYA_BLE_TX_QUEUE_DEPTH);
    for (unsigned i = 0; i < TUYA_BLE_TX_QUEUE_DEPTH; i++) {
        CHECK(response(&peer, i, key, 15, 0x8006, 0, reply, &len) == 0);
        CHECK(get32(reply + 1) == i + 1 && len == sizeof(all_types) + 7);
    }
    clear_packets(&peer);
    memset(large, 0x5a, sizeof(large));
    large[0] = 1; large[1] = 0; large[2] = 967 >> 8; large[3] = 967 & 255;
    CHECK(tuya_ble_dp_report(&state, large, 971, false) == 0 && peer.complete == 1);
    CHECK(response(&peer, 0, key, 15, 0x8006, 0, reply, &len) == 0 && len == 978);
    CHECK(!memcmp(reply + 7, large, 971));
    sequence = state.dp_report_sn;
    large[3]++;
    CHECK(tuya_ble_dp_report(&state, large, 972, false) != 0);
    CHECK(state.dp_report_sn == sequence && peer.complete == 1);
    large[2] = 962 >> 8; large[3] = 962 & 255;
    CHECK(tuya_ble_dp_report_with_time(&state, large, 966, 1700000000, false) == 0 && peer.complete == 2);
    CHECK(response(&peer, 1, key, 15, 0x8007, 0, reply, &len) == 0 && len == 978);
    sequence = state.dp_report_sn;
    large[3]++;
    CHECK(tuya_ble_dp_report_with_time(&state, large, 967, 1700000000, false) != 0);
    CHECK(state.dp_report_sn == sequence && peer.complete == 2);
    return 0;
}

/* 发送设备激活信息 (0x001B): the wire payload is a JSON object with devID,
 * secKey, localKey and schema fields. */
static const char activation_payload[] =
    "{\"devID\":\"dev1234567890abcdefq\",\"secKey\":\"secret_key_12345\","
    "\"localKey\":\"local_key_123456\",\"schema\":\"schema001\"}";
/* Gate probes only need bytes the parser will refuse before any callback. */
static const uint8_t activation_garbage[] = "synthetic-activation-blob";
static const char activation_no_secret[] =
    "{\"devID\":\"dev1234567890abcdefq\",\"localKey\":\"local_key_123456\"}";
static const char activation_short_local[] =
    "{\"secKey\":\"secret_key_12345\",\"localKey\":\"short\"}";

/* Single-mode binding: 0x001B in the unbound KEY_12 session parses the
 * activation JSON, hands the typed fields to the app, the KEY_12 ACK reports
 * success and only then does the SDK flip to the bound form -- after which a
 * full bound session (device-info, pair, DP write) completes on the same
 * state. */
static int activation_delivery_binds_device(void)
{
    tuya_ble_prov_state_t state;
    control_peer_t peer;
    uint8_t key12[16], key15[16], first[16], data[512], material[38];
    uint8_t command[512], reply[512], request[] = {0, 244}, stale[16], pair_rand[6];
    size_t len, n;
    CHECK(unbound_handshake(&state, &peer, key12, pair_rand) == 0);
    CHECK(receive(&state, 12, key12, 0x1B, 3,
                  (const uint8_t *)activation_payload, strlen(activation_payload)) == 0);
    CHECK(peer.activations == 1 && peer.complete == 1);
    CHECK(strcmp(peer.activation_local, local_key) == 0);
    CHECK(strcmp(peer.activation_secret, secret_key) == 0);
    CHECK(strcmp(peer.activation_dev_id, "dev1234567890abcdefq") == 0);
    CHECK(strcmp(peer.activation_schema, "schema001") == 0);
    CHECK(strstr(peer.activation_json, "\"schema\":\"schema001\"") != NULL);
    CHECK(response(&peer, 0, key12, 12, 0x1B, 3, reply, &len) == 0);
    CHECK(len == 1 && reply[0] == 0);
    CHECK(state.cfg.local_key == state.bound_local_key &&
          !memcmp(state.bound_local_key, local_key, 17) &&
          !memcmp(state.bound_secret_key, secret_key, 17));
    /* Unbound session key material lives on for its own ACK, but no bound
     * traffic is authorized before a bound-form re-handshake. */
    CHECK(state.handshake_ready && !state.handshake_bound);
    CHECK(!state.paired && !state.authenticated);
    CHECK((state.rsp_data[7] & 128) && memcmp(state.adv_data + 15, product, 16) != 0);
    memcpy(material, local_key, 16); memcpy(material + 16, secret_key, 16); memcpy(material + 32, pair_rand, 6);
    CHECK(mbedtls_md5(material, sizeof(material), stale) == 0);
    clear_packets(&peer);
    n = dp_command(command, all_types, sizeof(all_types));
    CHECK(receive(&state, 15, stale, 0x27, 4, command, n) == 0);
    CHECK(peer.writes == 0 && peer.complete == 0);
    /* The app reconnects: bound handshake in the same state, then DP control. */
    CHECK(key14(first) == 0);
    CHECK(receive(&state, 14, first, 0, 5, request, sizeof(request)) == 0);
    CHECK(state.handshake_ready && state.handshake_bound && peer.complete == 1);
    CHECK(response(&peer, 0, first, 14, 0, 5, data, &len) == 0);
    CHECK(len >= 12 && data[5] == 1);
    memcpy(material, local_key, 16); memcpy(material + 16, secret_key, 16); memcpy(material + 32, data + 6, 6);
    CHECK(mbedtls_md5(material, sizeof(material), key15) == 0);
    CHECK(receive(&state, 15, key15, 1, 6, identity, 16) == 0);
    CHECK(state.authenticated && state.paired);
    clear_packets(&peer);
    CHECK(receive(&state, 15, key15, 0x27, 7, command, n) == 0);
    CHECK(peer.writes == 1 && peer.complete == 1);
    CHECK(response(&peer, 0, key15, 15, 0x27, 7, reply, &len) == 0);
    CHECK(len == 6 && reply[5] == 0);
    /* close() carries the in-session credentials across its wipe: the form
     * and the rebuilt bound advertisement survive. */
    tuya_ble_prov_close(&state);
    CHECK(state.cfg.local_key == state.bound_local_key &&
          !memcmp(state.bound_local_key, local_key, 17) && (state.rsp_data[7] & 128));
    return 0;
}

/* A rejected delivery ACKs failure and leaves the unbound session usable. */
static int activation_delivery_rejected_stays_unbound(void)
{
    tuya_ble_prov_state_t state;
    control_peer_t peer;
    uint8_t key12[16], reply[512];
    size_t len;
    CHECK(unbound_handshake(&state, &peer, key12, NULL) == 0);
    peer.activation_rc = 7;
    CHECK(receive(&state, 12, key12, 0x1B, 3,
                  (const uint8_t *)activation_payload, strlen(activation_payload)) == 0);
    CHECK(peer.activations == 1 && peer.complete == 1);
    CHECK(response(&peer, 0, key12, 12, 0x1B, 3, reply, &len) == 0);
    CHECK(len == 1 && reply[0] == 1);
    CHECK(state.cfg.local_key == NULL && state.cfg.secret_key == NULL);
    CHECK(state.handshake_ready && state.paired && state.authenticated);
    CHECK(!(state.rsp_data[7] & 128) && memcmp(state.adv_data + 15, product, 16) == 0);
    return 0;
}

/* Malformed activation payloads never reach the application: a non-JSON
 * body, a missing secKey and a short localKey each ACK failure without a
 * callback and leave the unbound session usable for a corrected retry. */
static int activation_delivery_rejects_malformed_payloads(void)
{
    tuya_ble_prov_state_t state;
    control_peer_t peer;
    uint8_t key12[16], reply[512];
    size_t len;
    CHECK(unbound_handshake(&state, &peer, key12, NULL) == 0);
    CHECK(receive(&state, 12, key12, 0x1B, 3,
                  activation_garbage, sizeof(activation_garbage) - 1) == 0);
    CHECK(peer.activations == 0 && peer.complete == 1);
    CHECK(response(&peer, 0, key12, 12, 0x1B, 3, reply, &len) == 0);
    CHECK(len == 1 && reply[0] == 1);
    CHECK(receive(&state, 12, key12, 0x1B, 4,
                  (const uint8_t *)activation_no_secret, strlen(activation_no_secret)) == 0);
    CHECK(peer.activations == 0 && peer.complete == 2);
    CHECK(response(&peer, 1, key12, 12, 0x1B, 4, reply, &len) == 0);
    CHECK(len == 1 && reply[0] == 1);
    CHECK(receive(&state, 12, key12, 0x1B, 5,
                  (const uint8_t *)activation_short_local, strlen(activation_short_local)) == 0);
    CHECK(peer.activations == 0 && peer.complete == 3);
    CHECK(response(&peer, 2, key12, 12, 0x1B, 5, reply, &len) == 0);
    CHECK(len == 1 && reply[0] == 1);
    CHECK(state.cfg.local_key == NULL && state.cfg.secret_key == NULL);
    CHECK(state.handshake_ready && state.paired && state.authenticated);
    return 0;
}

/* Activation only rides the unbound KEY_12 session: no pairing, wrong
 * mode, or a bound device all drop it without a callback or an ACK. */
static int activation_delivery_is_gated(void)
{
    tuya_ble_prov_state_t state;
    control_peer_t peer;
    uint8_t key11v[16], key15[16], key12[16] = {0};
    CHECK(setup(&state, &peer, 0) == 0);
    CHECK(key11(key11v) == 0);
    CHECK(receive(&state, 11, key11v, 0x1B, 1,
                  activation_garbage, sizeof(activation_garbage) - 1) == 0);
    CHECK(peer.activations == 0 && peer.complete == 0);
    CHECK(receive(&state, 12, key12, 0x1B, 2,
                  activation_garbage, sizeof(activation_garbage) - 1) == 0);
    CHECK(peer.activations == 0 && peer.complete == 0);
    /* Bound device: a KEY_15 activation frame decrypts but is refused. */
    CHECK(handshake(&state, &peer, key15) == 0);
    CHECK(receive(&state, 15, key15, 0x1B, 3,
                  activation_garbage, sizeof(activation_garbage) - 1) == 0);
    CHECK(peer.activations == 0 && peer.complete == 0);
    return 0;
}

/* Unbind (0x0005) and device reset (0x0006) ACK 0x01, drop the bound form,
 * notify the app and restore the unbound advertisement, after which the
 * unbound handshake completes again on the same state. */
static int unbind_restores_unbound_form(void)
{
    tuya_ble_prov_state_t state;
    control_peer_t peer;
    uint8_t key15[16], key11v[16], key12[16], data[512], material[22], request[] = {0, 244};
    size_t len;
    CHECK(handshake(&state, &peer, key15) == 0);
    CHECK(receive(&state, 15, key15, 5, 3, NULL, 0) == 0);
    CHECK(peer.complete == 1);
    CHECK(response(&peer, 0, key15, 15, 5, 3, data, &len) == 0);
    CHECK(len == 1 && data[0] == 1);
    CHECK(peer.unbound_called == 1);
    CHECK(state.cfg.local_key == NULL && state.cfg.secret_key == NULL);
    CHECK(!state.handshake_ready && !state.paired && !state.authenticated);
    CHECK(!(state.rsp_data[7] & 128) && memcmp(state.adv_data + 15, product, 16) == 0);
    /* The unbound handshake takes over on the same state. */
    clear_packets(&peer);
    CHECK(key11(key11v) == 0);
    CHECK(receive(&state, 11, key11v, 0, 4, request, sizeof(request)) == 0);
    CHECK(state.handshake_ready && peer.complete == 1);
    CHECK(response(&peer, 0, key11v, 11, 0, 4, data, &len) == 0 && data[5] == 0);
    memcpy(material, key11v, 16); memcpy(material + 16, data + 6, 6);
    CHECK(mbedtls_md5(material, sizeof(material), key12) == 0);
    CHECK(receive(&state, 12, key12, 1, 5, identity, 16) == 0);
    CHECK(state.authenticated && state.paired);
    /* Device reset acknowledges identically. */
    CHECK(handshake(&state, &peer, key15) == 0);
    CHECK(receive(&state, 15, key15, 6, 6, NULL, 0) == 0);
    CHECK(peer.complete == 1);
    CHECK(response(&peer, 0, key15, 15, 6, 6, data, &len) == 0);
    CHECK(len == 1 && data[0] == 1 && peer.unbound_called == 1);
    return 0;
}

/* Real single-mode apps bind in the pair request rather than sending 0x001B.
 * Without a beacon key the v4.4 payload is 82 bytes; with one it is 98. */
static int single_mode_device_info(tuya_ble_prov_state_t *state, control_peer_t *peer,
                                   uint8_t key12[16])
{
    uint8_t first[16], reply[512], material[22], request[] = {0, 244};
    size_t len;
    CHECK(setup(state, peer, 0) == 0);
    state->cfg.comm_ability = TUYA_BLE_COMM_ABILITY_BLE;
    tuya_ble_prov_close(state);
    CHECK(key11(first) == 0);
    CHECK(receive(state, 11, first, 0, 1, request, sizeof(request)) == 0);
    CHECK(response(peer, 0, first, 11, 0, 1, reply, &len) == 0 && reply[5] == 0);
    memcpy(material, first, 16); memcpy(material + 16, reply + 6, 6);
    CHECK(mbedtls_md5(material, sizeof(material), key12) == 0);
    clear_packets(peer);
    return 0;
}

static int make_pair_binding(uint8_t data[98], unsigned beacon_len)
{
    uint8_t material[64], digest[16];
    memset(data, 0, 98);
    memcpy(data, identity, 16);
    memcpy(data + 16, "legacy", 6);
    memcpy(data + 22, "dev1234567890abcdef012", 22);
    data[44] = beacon_len;
    memset(data + 45, 0xa5, beacon_len);
    data[45 + beacon_len] = 0; /* App source. */
    memcpy(data + 46 + beacon_len, local_key, 16);
    memcpy(data + 62 + beacon_len, secret_key, 16);
    memcpy(material, local_key, 16); memcpy(material + 16, secret_key, 16);
    memcpy(material + 32, auth, 32);
    CHECK(mbedtls_md5(material, sizeof(material), digest) == 0);
    memcpy(data + 78 + beacon_len, digest, 4);
    return 0;
}

static int single_mode_pair_binds_and_reconnects(void)
{
    for (unsigned beacon_len = 0; beacon_len <= 16; beacon_len += 16) {
        tuya_ble_prov_state_t state;
        control_peer_t peer;
        uint8_t key12[16], key15[16], first[16], binding[98], reply[512];
        uint8_t wire[512], frame[512], material[38], request[] = {0, 244}, command[512];
        size_t len, binding_len = 82 + beacon_len;
        CHECK(single_mode_device_info(&state, &peer, key12) == 0);
        CHECK(make_pair_binding(binding, beacon_len) == 0);
        size_t n = make_packet(wire, 12, key12, 1, 2, binding, binding_len);
        /* Match the phone trace: 96-byte Frame plus a full CBC padding block. */
        CHECK(crypt(0, key12, wire + 1, wire + 17, n - 17, frame) == 0);
        memset(frame + n - 17, 16, 16);
        CHECK(crypt(1, key12, wire + 1, frame, n - 1, wire + 17) == 0);
        peer.busy = beacon_len != 0;
        CHECK(deliver(&state, wire, n + 16) == 0);
        CHECK(peer.activations == 1);
        CHECK(!strcmp(peer.activation_dev_id, "dev1234567890abcdef012"));
        CHECK(!strcmp(peer.activation_local, local_key) && !strcmp(peer.activation_secret, secret_key));
        CHECK(state.cfg.local_key == state.bound_local_key && (state.rsp_data[7] & 128));
        if (peer.busy) {
            CHECK(peer.complete == 0 && state.tx_count == 1);
            peer.busy = 0;
            CHECK(tuya_ble_prov_tx_ready(&state) == 0);
        }
        CHECK(peer.complete == 1); /* No WiFi network-status frame on single-mode BLE. */
        CHECK(response(&peer, 0, key12, 12, 1, 2, reply, &len) == 0 && len == 1 && reply[0] == 0);
        tuya_ble_prov_close(&state);
        CHECK((state.rsp_data[7] & 128) && !strcmp(state.cfg.local_key, local_key));
        clear_packets(&peer);
        CHECK(key14(first) == 0);
        CHECK(receive(&state, 14, first, 0, 1, request, sizeof(request)) == 0);
        CHECK(response(&peer, 0, first, 14, 0, 1, reply, &len) == 0 && reply[5] == 1);
        memcpy(material, local_key, 16); memcpy(material + 16, secret_key, 16);
        memcpy(material + 32, reply + 6, 6);
        CHECK(mbedtls_md5(material, sizeof(material), key15) == 0);
        if (beacon_len) {
            /* Bound reauthentication must never replace saved credentials
             * using fields carried in the new pair request. */
            binding[46 + beacon_len] ^= 1;
            CHECK(receive(&state, 15, key15, 1, 2, binding, binding_len) == 0);
        } else {
            CHECK(receive(&state, 15, key15, 1, 2, identity, 16) == 0);
        }
        CHECK(!strcmp(state.cfg.local_key, local_key));
        CHECK(response(&peer, 1, key15, 15, 1, 2, reply, &len) == 0 && reply[0] == 2);
        CHECK(peer.complete == 2 && peer.activations == 1);
        clear_packets(&peer);
        n = dp_command(command, all_types, sizeof(all_types));
        CHECK(receive(&state, 15, key15, 0x27, 3, command, n) == 0);
        CHECK(peer.writes == 1);
        CHECK(response(&peer, 0, key15, 15, 0x27, 3, reply, &len) == 0 && reply[5] == 0);
        CHECK(tuya_ble_dp_report(&state, all_types, sizeof(all_types), false) == 0);
    }
    return 0;
}

static int single_mode_pair_accepts_padded_virtual_id(void)
{
    tuya_ble_prov_state_t state;
    control_peer_t peer;
    uint8_t key12[16], binding[98], reply[512];
    size_t len;
    CHECK(single_mode_device_info(&state, &peer, key12) == 0);
    CHECK(make_pair_binding(binding, 0) == 0);
    /* Real App trace: a 16-byte virtual ID in the fixed 22-byte field,
     * followed by six zero-padding bytes. */
    memset(binding + 22, 0, 22);
    memcpy(binding + 22, identity, 16);
    CHECK(receive(&state, 12, key12, 1, 2, binding, 82) == 0);
    CHECK(peer.activations == 1 && !strcmp(peer.activation_dev_id, identity));
    CHECK(response(&peer, 0, key12, 12, 1, 2, reply, &len) == 0 && reply[0] == 0);
    CHECK(state.cfg.local_key == state.bound_local_key && (state.rsp_data[7] & 128));
    return 0;
}

static int single_mode_pair_allows_immediate_bound_control(void)
{
    tuya_ble_prov_state_t state;
    control_peer_t peer;
    uint8_t key12[16], key15[16], binding[98], material[38], command[512], reply[512];
    size_t len;
    CHECK(single_mode_device_info(&state, &peer, key12) == 0);
    CHECK(make_pair_binding(binding, 0) == 0);
    memset(binding + 22, 0, 22);
    memcpy(binding + 22, identity, 16);
    CHECK(receive(&state, 12, key12, 1, 2, binding, 82) == 0);
    CHECK(peer.activations == 1);
    CHECK(response(&peer, 0, key12, 12, 1, 2, reply, &len) == 0 && reply[0] == 0);
    memcpy(material, local_key, 16);
    memcpy(material + 16, secret_key, 16);
    memcpy(material + 32, state.pair_rand, 6);
    CHECK(mbedtls_md5(material, sizeof(material), key15) == 0);
    /* The real App keeps the original GATT connection and sends KEY_15
     * directly after the first pair ACK, without a KEY_14 device-info exchange. */
    CHECK(receive(&state, 15, key15, 3, 3, NULL, 0) == 0);
    CHECK(peer.queries == 1 && peer.complete == 2);
    CHECK(response(&peer, 1, key15, 15, 3, 3, reply, &len) == 0 && reply[0] == 0);
    size_t n = dp_command(command, all_types, sizeof(all_types));
    CHECK(receive(&state, 15, key15, 0x27, 4, command, n) == 0);
    CHECK(peer.writes == 1 && peer.complete == 3);
    CHECK(tuya_ble_dp_report(&state, all_types, sizeof(all_types), false) == 0);
    return 0;
}

static int single_mode_pair_rejects_bad_binding(void)
{
    for (unsigned bad = 0; bad < 10; bad++) {
        tuya_ble_prov_state_t state;
        control_peer_t peer;
        uint8_t key12[16], binding[98], reply[512];
        size_t len, binding_len = 82;
        CHECK(single_mode_device_info(&state, &peer, key12) == 0);
        CHECK(make_pair_binding(binding, 0) == 0);
        switch (bad) {
        case 0: binding[81] ^= 1; break; /* Invalid cloud proof. */
        case 1: binding_len--; break;
        case 2: binding[44] = 1; break; /* Invalid beacon length. */
        case 3: binding[46] = 0; break;
        case 4: binding[62] = 0; break;
        case 5: binding[22] = 0; break;
        case 6: state.cfg.activation_info = NULL; break;
        case 7: peer.activation_rc = 7; break;
        case 8: binding[0] ^= 1; break;
        case 9: binding[38] = 0; break; /* Nonzero garbage after a padded ID. */
        }
        CHECK(receive(&state, 12, key12, 1, 2, binding, binding_len) == 0);
        CHECK(peer.activations == (bad == 7 ? 1u : 0u) && peer.complete == 1);
        CHECK(response(&peer, 0, key12, 12, 1, 2, reply, &len) == 0 && len == 1 && reply[0] == 1);
        CHECK(!state.cfg.local_key && !(state.rsp_data[7] & 128) && !state.authenticated);
    }
    return 0;
}

static int single_mode_uuid_pair_keeps_activation_delivery(void)
{
    tuya_ble_prov_state_t state;
    control_peer_t peer;
    uint8_t key12[16], reply[512];
    size_t len;
    CHECK(single_mode_device_info(&state, &peer, key12) == 0);
    CHECK(receive(&state, 12, key12, 1, 2, identity, 16) == 0);
    CHECK(state.authenticated && !state.cfg.local_key && peer.complete == 1 && !peer.activations);
    CHECK(response(&peer, 0, key12, 12, 1, 2, reply, &len) == 0 && reply[0] == 0);
    CHECK(receive(&state, 12, key12, 0x1b, 3, activation_payload, strlen(activation_payload)) == 0);
    CHECK(peer.activations == 1 && peer.complete == 2 && (state.rsp_data[7] & 128));
    CHECK(response(&peer, 1, key12, 12, 0x1b, 3, reply, &len) == 0 && reply[0] == 0);
    return 0;
}

static int wifi_pair_does_not_install_ble_binding(void)
{
    tuya_ble_prov_state_t state;
    control_peer_t peer;
    uint8_t first[16], key12[16], reply[512], material[22], binding[98], request[] = {0, 244};
    size_t len;
    CHECK(setup(&state, &peer, 0) == 0 && key11(first) == 0);
    CHECK(receive(&state, 11, first, 0, 1, request, sizeof(request)) == 0);
    CHECK(response(&peer, 0, first, 11, 0, 1, reply, &len) == 0);
    memcpy(material, first, 16); memcpy(material + 16, reply + 6, 6);
    CHECK(mbedtls_md5(material, sizeof(material), key12) == 0);
    CHECK(make_pair_binding(binding, 0) == 0);
    clear_packets(&peer);
    CHECK(receive(&state, 12, key12, 1, 2, binding, 82) == 0);
    CHECK(state.authenticated && !state.cfg.local_key && !peer.activations && peer.complete == 2);
    return 0;
}

static int bind_api_validation(void)
{
    tuya_ble_prov_state_t state;
    control_peer_t peer;
    CHECK(setup(&state, &peer, 0) == 0);
    CHECK(tuya_ble_prov_bind(NULL, local_key, secret_key) == -1);
    CHECK(tuya_ble_prov_bind(&state, NULL, secret_key) == -1);
    CHECK(tuya_ble_prov_bind(&state, local_key, NULL) == -1);
    CHECK(tuya_ble_prov_bind(&state, "short", secret_key) == -1);
    CHECK(tuya_ble_prov_bind(&state, local_key, "0123456789abcdef0") == -1);
    CHECK(state.cfg.local_key == NULL && !(state.rsp_data[7] & 128));
    CHECK(tuya_ble_prov_bind(&state, local_key, secret_key) == 0);
    CHECK(tuya_ble_prov_bind(&state, local_key, secret_key) == -1);
    CHECK(tuya_ble_prov_unbind(NULL) == -1);
    CHECK(tuya_ble_prov_unbind(&state) == 0);
    CHECK(state.cfg.local_key == NULL && peer.unbound_called == 1);
    CHECK(memcmp(state.adv_data + 15, product, 16) == 0);
    return 0;
}

int test_control(void)
{
    unsigned passed = 0;
#define RUN(fn) do { CHECK(fn() == 0); passed++; } while (0)
    RUN(bound_advertisement);
    RUN(bound_control_and_query);
    RUN(codec_all_types);
    RUN(invalid_batches_are_atomic);
    RUN(unauthorized_modes_and_credentials);
    RUN(explicit_reports_and_acks);
    RUN(callback_failure_and_backpressure);
    RUN(close_and_device_info_require_fresh_pairing);
    RUN(credential_configuration);
    RUN(bound_entropy_failure);
    RUN(report_capacity_and_size_boundaries);
    RUN(activation_delivery_binds_device);
    RUN(activation_delivery_rejected_stays_unbound);
    RUN(activation_delivery_rejects_malformed_payloads);
    RUN(activation_delivery_is_gated);
    RUN(single_mode_pair_binds_and_reconnects);
    RUN(single_mode_pair_accepts_padded_virtual_id);
    RUN(single_mode_pair_allows_immediate_bound_control);
    RUN(single_mode_pair_rejects_bad_binding);
    RUN(single_mode_uuid_pair_keeps_activation_delivery);
    RUN(wifi_pair_does_not_install_ble_binding);
    RUN(unbind_restores_unbound_form);
    RUN(bind_api_validation);
#undef RUN
    printf("PASS %u BLE bound-control wire scenarios\n", passed);
    return 0;
}
