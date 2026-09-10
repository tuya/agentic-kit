#include "tuya_ble_internal.h"
#include "tuya_ble_bigdata.h"

#include "cJSON.h"
#include "log.h"

#undef TUYA_BLE_HAL_LOGI
#undef TUYA_BLE_HAL_LOGW
#undef TUYA_BLE_HAL_LOGE
#undef TUYA_BLE_HAL_HEXDUMP
#define TUYA_BLE_HAL_LOGI(fmt, ...) log_emit(LOG_DEBUG, "[ble] " fmt, ##__VA_ARGS__)
#define TUYA_BLE_HAL_LOGW(fmt, ...) log_emit(LOG_WARN, "[ble] " fmt, ##__VA_ARGS__)
#define TUYA_BLE_HAL_LOGE(fmt, ...) log_emit(LOG_ERROR, "[ble] " fmt, ##__VA_ARGS__)
#define TUYA_BLE_HAL_HEXDUMP(buf, len)                                         \
    do {                                                                       \
        const uint8_t *p_ = (const uint8_t *)(buf);                            \
        size_t n_ = (len);                                                     \
        char hex_[193];                                                        \
        size_t o_ = 0;                                                         \
        for (size_t i_ = 0; i_ < n_ && o_ + 3 < sizeof(hex_); i_++) {           \
            o_ += (size_t)snprintf(hex_ + o_, sizeof(hex_) - o_, "%02X ",       \
                                   p_[i_]);                                    \
        }                                                                      \
        if (o_) hex_[o_ - 1] = '\0';                                           \
        log_emit(LOG_DEBUG, "[ble] HEX(%u): %s", (unsigned)n_, hex_);          \
    } while (0)
#include "mbedtls/aes.h"
#include "mbedtls/md5.h"

#include <stdio.h>
#include <string.h>

#define FRM_QRY_DEV_INFO_REQ         0x0000
#define FRM_PAIR_REQ                 0x0001
#define FRM_RPT_NET_STAT_REQ         0x001E
#define FRM_DOWNLINK_TRANSPARENT_REQ 0x801B
#define FRM_UPLINK_TRANSPARENT_REQ   0x801C

#define ENCRYPTION_MODE_NONE        TUYA_BLE_ENCRYPTION_MODE_NONE
#define ENCRYPTION_MODE_KEY_11      TUYA_BLE_ENCRYPTION_MODE_KEY_11
#define ENCRYPTION_MODE_KEY_12      TUYA_BLE_ENCRYPTION_MODE_KEY_12

#define TUYA_BLE_PROTOCOL_VER_HI    0x04
#define TUYA_BLE_PROTOCOL_VER_LO    0x04

#define BLE_FRAME_HEADER_LEN        12
#define BLE_FRAME_CRC_LEN           2
#define BLE_FRAME_MIN_LEN           (BLE_FRAME_HEADER_LEN + BLE_FRAME_CRC_LEN)

#define AUTH_KEY_LEN         32
#define TUYA_SVC_UUID_LO    0x50
#define TUYA_SVC_UUID_HI    0xFD
#define TUYA_COMPANY_ID_LO  0xD0
#define TUYA_COMPANY_ID_HI  0x07
#define TUYA_ENCRY_MODE     0x00
#define ADV_FLAG_WIFI_LIST  (1U << 5)
#define COMBOS_FLAG_WIFI_LIST (1U << 0)
#define COMBOS_FLAG_NCFG_STAT (1U << 1)

#define ADV_FLAG_UUID_COMP  (1 << 0)
#define SETBIT(val, bit)    ((val) |= (1 << (bit)))

static int random_fill(tuya_ble_prov_state_t *state, uint8_t *out, size_t len)
{
    memset(out, 0, len);
    if (state->cfg.random_fn) {
        if (state->cfg.random_fn(out, len, state->cfg.random_ctx) != (int)len) {
            memset(out, 0, len);
            return -1;
        }
    } else {
        tuya_ble_hal_random(out, len);
    }
    return 0;
}

static int md5_hash(const uint8_t *input, size_t ilen, uint8_t output[16])
{
    return mbedtls_md5(input, ilen, output);
}

static uint16_t crc16_modbus(const uint8_t *data, uint16_t size)
{
    static const uint16_t poly[2] = {0, 0xA001};
    uint16_t crc = 0xFFFF;
    for (uint16_t j = 0; j < size; j++) {
        uint8_t ds = data[j];
        for (int i = 0; i < 8; i++) {
            crc = (crc >> 1) ^ poly[(crc ^ ds) & 1];
            ds >>= 1;
        }
    }
    return crc;
}

static int generate_key_11(const uint8_t *auth_key, const uint8_t *uuid,
                           const uint8_t *iv, uint8_t *out_key)
{
    uint8_t buf[AUTH_KEY_LEN + TUYA_BLE_ID_LEN + 16];
    memcpy(buf, auth_key, AUTH_KEY_LEN);
    memcpy(buf + AUTH_KEY_LEN, uuid, TUYA_BLE_ID_LEN);
    memcpy(buf + AUTH_KEY_LEN + TUYA_BLE_ID_LEN, iv, 16);
    return md5_hash(buf, sizeof(buf), out_key);
}

static int generate_key_12(const uint8_t *key_11, const uint8_t *pair_rand,
                           uint8_t *out_key)
{
    uint8_t buf[16 + TUYA_BLE_PAIR_RAND_LEN];
    memcpy(buf, key_11, 16);
    memcpy(buf + 16, pair_rand, TUYA_BLE_PAIR_RAND_LEN);
    return md5_hash(buf, sizeof(buf), out_key);
}

static int generate_register_key(const uint8_t *auth_key, const uint8_t *service_rand,
                                 uint8_t *out_key)
{
    mbedtls_aes_context aes;

    mbedtls_aes_init(&aes);
    int ret = mbedtls_aes_setkey_enc(&aes, auth_key, 128);
    if (ret == 0) {
        ret = mbedtls_aes_crypt_ecb(&aes, MBEDTLS_AES_ENCRYPT, service_rand, out_key);
    }
    mbedtls_aes_free(&aes);
    return ret;
}

static int aes_cbc_encrypt(const uint8_t *key, const uint8_t *iv,
                           const uint8_t *in, uint16_t in_len,
                           uint8_t *out, uint16_t *out_len)
{
    uint16_t padded_len;
    uint8_t padded[TUYA_BLE_TX_BUF_SIZE];

    if (in_len % 16 == 0) {
        padded_len = in_len;
        memcpy(padded, in, in_len);
    } else {
        uint8_t pad = 16 - (in_len % 16);
        padded_len = in_len + pad;
        if (padded_len > sizeof(padded)) return -1;
        memcpy(padded, in, in_len);
        memset(padded + in_len, pad, pad);
    }

    uint8_t iv_copy[16];
    memcpy(iv_copy, iv, 16);

    mbedtls_aes_context aes;
    mbedtls_aes_init(&aes);
    int ret = mbedtls_aes_setkey_enc(&aes, key, 128);
    if (ret == 0) {
        ret = mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_ENCRYPT, padded_len,
                                    iv_copy, padded, out);
    }
    mbedtls_aes_free(&aes);
    if (ret == 0) {
        *out_len = padded_len;
    }
    return ret;
}

static int aes_cbc_decrypt(const uint8_t *key, const uint8_t *iv,
                           const uint8_t *in, uint16_t in_len,
                           uint8_t *out, uint16_t *out_len)
{
    if (in_len == 0 || in_len % 16 != 0) return -1;

    uint8_t iv_copy[16];
    memcpy(iv_copy, iv, 16);

    mbedtls_aes_context aes;
    mbedtls_aes_init(&aes);
    int ret = mbedtls_aes_setkey_dec(&aes, key, 128);
    if (ret == 0) {
        ret = mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_DECRYPT, in_len,
                                    iv_copy, in, out);
    }
    mbedtls_aes_free(&aes);
    if (ret != 0) return ret;

    *out_len = in_len;
    return 0;
}

static void ble_id_compress(const uint8_t *in, uint8_t *out)
{
    uint8_t i, j, temp[4];
    for (i = 0; i < 5; i++) {
        for (j = i * 4; j < (i * 4 + 4); j++) {
            if (in[j] >= 0x30 && in[j] <= 0x39)
                temp[j - i * 4] = in[j] - 0x30;
            else if (in[j] >= 0x41 && in[j] <= 0x5A)
                temp[j - i * 4] = in[j] - 0x41 + 36;
            else if (in[j] >= 0x61 && in[j] <= 0x7A)
                temp[j - i * 4] = in[j] - 0x61 + 10;
            else
                temp[j - i * 4] = 0;
        }
        out[i * 3]     = (temp[0] & 0x3F) << 2 | ((temp[1] >> 4) & 0x03);
        out[i * 3 + 1] = (temp[1] & 0x0F) << 4 | ((temp[2] >> 2) & 0x0F);
        out[i * 3 + 2] = (temp[2] & 0x03) << 6 | (temp[3] & 0x3F);
    }
    out[15] = 0xFF;
}

static int rsp_id_encrypt(uint8_t *key, uint8_t key_len,
                          uint8_t *in_buf, uint8_t in_len, uint8_t *out_buf)
{
    uint8_t aes_key[16], aes_iv[16];
    int ret = md5_hash(key, key_len, aes_key);
    if (ret != 0) return ret;
    memcpy(aes_iv, aes_key, 16);
    mbedtls_aes_context aes;
    mbedtls_aes_init(&aes);
    ret = mbedtls_aes_setkey_enc(&aes, aes_key, 128);
    if (ret == 0) {
        ret = mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_ENCRYPT, in_len,
                                    aes_iv, in_buf, out_buf);
    }
    mbedtls_aes_free(&aes);
    return ret;
}

static int tuya_ble_send(tuya_ble_prov_state_t *state, uint16_t cmd,
                         const uint8_t *data, uint16_t data_len,
                         uint8_t encrypt_mode)
{
    if (state->cfg.send_fn == NULL) return -1;

    size_t needed = BLE_FRAME_MIN_LEN + (size_t)data_len;
    size_t packet_needed = encrypt_mode == ENCRYPTION_MODE_NONE
        ? 1 + needed : 17 + ((needed + 15) & ~(size_t)15);
    if ((data_len && !data) || packet_needed > TUYA_BLE_TX_BUF_SIZE ||
        state->tx_count == TUYA_BLE_TX_QUEUE_DEPTH || state->sn == UINT32_MAX) {
        TUYA_BLE_HAL_LOGE("[TX] rejected: cmd=0x%04X len=%u needed=%u queue=%u sn=%lu",
                          cmd, data_len, (unsigned)packet_needed,
                          state->tx_count, (unsigned long)state->sn);
        return -1;
    }
    uint16_t frame_len = (uint16_t)needed;
    uint8_t *frame = state->tx_frame;
    if (frame_len > sizeof(state->tx_frame)) {
        TUYA_BLE_HAL_LOGE("[TX] frame too large: %u > %u",
                          (unsigned)frame_len, (unsigned)sizeof(state->tx_frame));
        return -1;
    }

    uint32_t send_sn = ++state->sn;
    frame[0] = (send_sn >> 24) & 0xFF;
    frame[1] = (send_sn >> 16) & 0xFF;
    frame[2] = (send_sn >> 8) & 0xFF;
    frame[3] = send_sn & 0xFF;
    frame[4] = (state->last_rx_sn >> 24) & 0xFF;
    frame[5] = (state->last_rx_sn >> 16) & 0xFF;
    frame[6] = (state->last_rx_sn >> 8) & 0xFF;
    frame[7] = state->last_rx_sn & 0xFF;
    frame[8] = (cmd >> 8) & 0xFF;
    frame[9] = cmd & 0xFF;
    frame[10] = (data_len >> 8) & 0xFF;
    frame[11] = data_len & 0xFF;
    if (data_len > 0 && data) {
        memcpy(&frame[12], data, data_len);
    }
    uint16_t crc = crc16_modbus(frame, BLE_FRAME_HEADER_LEN + data_len);
    frame[BLE_FRAME_HEADER_LEN + data_len] = (crc >> 8) & 0xFF;
    frame[BLE_FRAME_HEADER_LEN + data_len + 1] = crc & 0xFF;

    TUYA_BLE_HAL_LOGI("[TX] sn=%lu ack_sn=%lu cmd=0x%04X, data_len=%d, encrypt=0x%02X",
                      (unsigned long)send_sn, (unsigned long)state->last_rx_sn,
                      cmd, data_len, encrypt_mode);

    uint8_t *enc_pkt = state->tx_enc_pkt;
    uint16_t enc_pkt_len;

    if (encrypt_mode == ENCRYPTION_MODE_NONE) {
        enc_pkt[0] = ENCRYPTION_MODE_NONE;
        memcpy(&enc_pkt[1], frame, frame_len);
        enc_pkt_len = 1 + frame_len;
    } else {
        uint16_t enc_len;
        int ret = 0;
        uint8_t iv[16];
        uint8_t enc_key[16];
        if (encrypt_mode == ENCRYPTION_MODE_KEY_11) {
            memcpy(iv, state->server_rand, 16);
            memcpy(enc_key, state->key_11, 16);
        } else {
            if (random_fill(state, iv, sizeof(iv)) != 0) return -1;
            if (encrypt_mode == ENCRYPTION_MODE_KEY_12) {
                ret = generate_key_12(state->key_11, state->pair_rand, enc_key);
                if (ret != 0) return ret;
            } else {
                memset(enc_key, 0, 16);
            }
        }

        enc_pkt[0] = encrypt_mode;
        memcpy(&enc_pkt[1], iv, 16);

        ret = aes_cbc_encrypt(enc_key, iv, frame, frame_len,
                              &enc_pkt[17], &enc_len);
        if (ret != 0) {
            TUYA_BLE_HAL_LOGE("Encryption failed, ret=%d", ret);
            return ret;
        }
        enc_pkt_len = 17 + enc_len;
    }

    unsigned slot = (state->tx_head + state->tx_count) % TUYA_BLE_TX_QUEUE_DEPTH;
    memcpy(state->tx_queue[slot], enc_pkt, enc_pkt_len);
    state->tx_queue_len[slot] = enc_pkt_len;
    if (!state->tx_count) state->tx_started_ms = state->now_ms;
    state->tx_count++;
    int rc = tuya_ble_prov_tx_ready(state);
    return rc == TUYA_BLE_SEND_BUSY ? 0 : rc;
}

int tuya_ble_prov_send_frame(tuya_ble_prov_state_t *state, uint16_t cmd,
                             const uint8_t *data, uint16_t data_len,
                             uint8_t encrypt_mode)
{
    return tuya_ble_send(state, cmd, data, data_len, encrypt_mode);
}

static void handle_dev_info_req(tuya_ble_prov_state_t *state, const uint8_t *data, uint16_t data_len)
{
    TUYA_BLE_HAL_LOGI("[PROTO] FRM_QRY_DEV_INFO_REQ, data_len=%d (pkt_len=%u)",
                      data_len, state->peer_pkt_len);
    TUYA_BLE_HAL_HEXDUMP(data, data_len > 32 ? 32 : data_len);

    state->handshake_ready = false;
    state->paired = false;
    state->authenticated = false;
    state->credentials_pending = false;
    state->wifi_scan_pending = false;
    state->wifi_scan_token++;
    if (state->wifi_scan_token == 0) state->wifi_scan_token++;
    if (random_fill(state, state->pair_rand, TUYA_BLE_PAIR_RAND_LEN) != 0) {
        TUYA_BLE_HAL_LOGE("[PROTO] pair_rand generation failed");
        return;
    }



    uint8_t resp[200];
    memset(resp, 0, sizeof(resp));

    resp[0] = 0x00;
    resp[1] = 0x00;
    resp[2] = TUYA_BLE_PROTOCOL_VER_HI;
    resp[3] = TUYA_BLE_PROTOCOL_VER_LO;
    resp[4] = 0x05;
    resp[5] = 0x00;
    memcpy(&resp[6], state->pair_rand, TUYA_BLE_PAIR_RAND_LEN);

    int reg_key_ret = generate_register_key((const uint8_t *)state->cfg.auth_key,
                                            state->server_rand, &resp[14]);
    if (reg_key_ret != 0) return;
    TUYA_BLE_HAL_LOGI("[PROTO] register_key ret=%d", reg_key_ret);

    resp[52] = (state->cfg.comm_ability >> 8) & 0xFF;
    resp[53] = state->cfg.comm_ability & 0xFF;
    resp[54] = 0x06;
    resp[83] = 0x01;
    resp[86] = 0x01;
    resp[95] = TUYA_BLE_ID_LEN;
    memset(&resp[96], 0, TUYA_BLE_ID_LEN);

    uint8_t payload_len = 96 + TUYA_BLE_ID_LEN;
    resp[payload_len++] = 0x08;
    payload_len = payload_len + 0x08;
    resp[payload_len++] = 0x08;
    payload_len = payload_len + 0x08;
    uint16_t pkt_len = state->peer_pkt_len;
    TUYA_BLE_HAL_LOGI("[PROTO] pkt_len=%d", pkt_len);

    if (pkt_len < 256) {
        resp[payload_len++] = 1;
        resp[payload_len++] = pkt_len & 0xFF;
    } else {
        resp[payload_len++] = 2;
        resp[payload_len++] = (pkt_len >> 8) & 0xFF;
        resp[payload_len++] = pkt_len & 0xFF;
    }

    if (state->cfg.wifi_scan_request != NULL) {
        resp[payload_len++] = 0;
        resp[payload_len++] = 1;
        resp[payload_len++] = COMBOS_FLAG_WIFI_LIST | COMBOS_FLAG_NCFG_STAT;
    }

    TUYA_BLE_HAL_LOGI("[PROTO] DevInfo response, %d bytes", payload_len);
    state->handshake_ready = tuya_ble_send(state, FRM_QRY_DEV_INFO_REQ, resp,
                                          payload_len, ENCRYPTION_MODE_KEY_11) == 0;
    TUYA_BLE_HAL_LOGI("[PROTO] handshake_ready=%d", state->handshake_ready);
}

static void handle_pair_req(tuya_ble_prov_state_t *state, const uint8_t *data, uint16_t data_len)
{
    TUYA_BLE_HAL_LOGI("[PROTO] FRM_PAIR_REQ, data_len=%d", data_len);

    uint8_t result = data_len < TUYA_BLE_ID_LEN ? 0x01 : 0x00;
    if (data_len >= TUYA_BLE_ID_LEN) {
        if (memcmp(data, state->ble_id, TUYA_BLE_ID_LEN) != 0) {
            TUYA_BLE_HAL_LOGW("[PROTO] BLE ID mismatch in pair request!");

            result = 0x01;
        }
    }

    if (result == 0x00) {
        state->paired = true;
        state->authenticated = true;
        TUYA_BLE_HAL_LOGI("[PROTO] Paired successfully");
    }

    uint8_t resp[1] = {result};
    if (tuya_ble_send(state, FRM_PAIR_REQ, resp, 1, ENCRYPTION_MODE_KEY_12) != 0) {
        TUYA_BLE_HAL_LOGE("[PROTO] Pair response send failed");
        state->paired = state->authenticated = false;
        return;
    }

    if (state->paired) {
        uint8_t net_stat = 0x00;
        tuya_ble_send(state, FRM_RPT_NET_STAT_REQ, &net_stat, 1, ENCRYPTION_MODE_KEY_12);
    }
}

static void handle_wifi_config(tuya_ble_prov_state_t *state, const uint8_t *data, uint16_t data_len)
{
    TUYA_BLE_HAL_LOGI("[PROTO] FRM_DOWNLINK_TRANSPARENT_REQ (%d bytes):", data_len);

    if (data_len < 4) {
        TUYA_BLE_HAL_LOGW("[PROTO] Transparent data too short");
        return;
    }

    uint16_t flag = (data[0] << 8) | data[1];
    uint16_t offset = 2;

    if (flag != 0x0000) {
        TUYA_BLE_HAL_LOGW("[PROTO] Unexpected transparent flag 0x%04X", flag);
        return;
    }

    uint16_t subcmd = (data[offset] << 8) | data[offset + 1];
    offset += 2;

    TUYA_BLE_HAL_LOGI("[PROTO] flag=0x%04X, subcmd=0x%04X", flag, subcmd);

    if (subcmd != 0x0001) {
        TUYA_BLE_HAL_LOGW("[PROTO] Unhandled subcmd 0x%04X", subcmd);
        return;
    }

    char json_str[512];
    uint16_t json_len = data_len - offset;
    if (json_len >= sizeof(json_str) || memchr(data + offset, 0, json_len)) {
        TUYA_BLE_HAL_LOGW("[PROTO] WiFi JSON too long or embedded NUL (%u)", data_len - offset);
        return;
    }
    memcpy(json_str, &data[offset], json_len);
    json_str[json_len] = '\0';
    TUYA_BLE_HAL_LOGI("[PROTO] WiFi JSON: %s", json_str);


    cJSON *root = cJSON_ParseWithOpts(json_str, NULL, true);
    if (root == NULL) {
        TUYA_BLE_HAL_LOGE("JSON parse failed: %s", json_str);
        return;
    }

    cJSON *ssid = cJSON_GetObjectItemCaseSensitive(root, "ssid");
    cJSON *pwd = cJSON_GetObjectItemCaseSensitive(root, "pwd");
    cJSON *token = cJSON_GetObjectItemCaseSensitive(root, "token");
    if (!cJSON_IsObject(root) || !cJSON_IsString(ssid) || !cJSON_IsString(pwd) ||
        !cJSON_IsString(token) || !ssid->valuestring[0] || !token->valuestring[0] ||
        strlen(ssid->valuestring) > TUYA_BLE_SSID_MAX_LEN ||
        strlen(pwd->valuestring) > TUYA_BLE_PASSWORD_MAX_LEN ||
        strlen(token->valuestring) > TUYA_BLE_TOKEN_MAX_LEN) {
        TUYA_BLE_HAL_LOGW("[PROTO] WiFi JSON rejected field validation");
        cJSON_Delete(root);
        return;
    }
    memset(&state->creds, 0, sizeof(state->creds));
    strcpy(state->creds.ssid, ssid->valuestring);
    strcpy(state->creds.password, pwd->valuestring);
    strcpy(state->creds.token, token->valuestring);

    cJSON_Delete(root);

    uint8_t resp[5] = {0x00, 0x00, 0x00, 0x01, 0x00};
    if (tuya_ble_send(state, FRM_UPLINK_TRANSPARENT_REQ, resp, sizeof(resp), ENCRYPTION_MODE_KEY_12) != 0) {
        TUYA_BLE_HAL_LOGE("[PROTO] Creds ack send failed");
        return;
    }

    state->credentials_pending = true;
    if (!state->tx_count) {
        state->credentials_pending = false;
        if (state->cfg.cb) state->cfg.cb(&state->creds);
    }
}

void tuya_ble_recv(tuya_ble_prov_state_t *state, const uint8_t *packet, uint16_t packet_len)
{
    TUYA_BLE_HAL_LOGI("[RX] packet_len=%d", packet_len);

    if (packet_len < 2) {
        TUYA_BLE_HAL_LOGW("[RX] Packet too short (%u)", packet_len);
        return;
    }

    uint8_t encrypt_mode = packet[0];
    TUYA_BLE_HAL_LOGI("[RX] encrypt_mode=0x%02X", encrypt_mode);
    TUYA_BLE_HAL_HEXDUMP(packet, packet_len > 64 ? 64 : packet_len);

    uint8_t candidate_key[16] = {0};
    uint8_t *frame = state->rx_frame;
    uint16_t frame_len;

    if (encrypt_mode == ENCRYPTION_MODE_NONE) {
        frame_len = packet_len - 1;
        if (frame_len > sizeof(state->rx_frame)) {
            TUYA_BLE_HAL_LOGW("[RX] plaintext frame too large: %u > %u",
                              (unsigned)frame_len, (unsigned)sizeof(state->rx_frame));
            return;
        }
        memcpy(frame, &packet[1], frame_len);
    } else {
        if (packet_len < 18) {
            TUYA_BLE_HAL_LOGW("[RX] Encrypted packet too short (%u)", packet_len);
            return;
        }
        const uint8_t *iv = &packet[1];
        const uint8_t *enc_data = &packet[17];
        uint16_t enc_len = packet_len - 17;

        uint8_t dec_key[16];
        int ret = 0;
        if (encrypt_mode == ENCRYPTION_MODE_KEY_11) {
            ret = generate_key_11((const uint8_t *)state->cfg.auth_key,
                                  state->ble_id, iv, dec_key);
            if (ret != 0) {
                TUYA_BLE_HAL_LOGE("[RX] key_11 derive failed, ret=%d", ret);
                return;
            }
            memcpy(candidate_key, dec_key, 16);

        } else if (encrypt_mode == ENCRYPTION_MODE_KEY_12) {
            if (!state->handshake_ready) {
                TUYA_BLE_HAL_LOGW("[RX] KEY_12 packet before handshake ready");
                return;
            }
            ret = generate_key_12(state->key_11, state->pair_rand, dec_key);
            if (ret != 0) {
                TUYA_BLE_HAL_LOGE("[RX] key_12 derive failed, ret=%d", ret);
                return;
            }
        } else {
            TUYA_BLE_HAL_LOGW("[RX] Unsupported encrypt_mode=0x%02X", encrypt_mode);
            return;
        }

        ret = aes_cbc_decrypt(dec_key, iv, enc_data, enc_len,
                              frame, &frame_len);
        if (ret != 0) {
            TUYA_BLE_HAL_LOGE("[RX] Decryption failed, ret=%d", ret);
            return;
        }
        TUYA_BLE_HAL_LOGI("[RX] Decrypted frame (%d bytes):", frame_len);
        TUYA_BLE_HAL_HEXDUMP(frame, frame_len > 64 ? 64 : frame_len);
    }

    if (frame_len < BLE_FRAME_MIN_LEN) {
        TUYA_BLE_HAL_LOGW("[RX] Frame too short (%d bytes)", frame_len);
        return;
    }

    uint32_t sn = ((uint32_t)frame[0] << 24) | ((uint32_t)frame[1] << 16) | ((uint32_t)frame[2] << 8) | frame[3];
    uint16_t cmd = (frame[8] << 8) | frame[9];
    uint16_t data_len = (frame[10] << 8) | frame[11];
    const uint8_t *data = &frame[12];

    size_t crc_offset = BLE_FRAME_HEADER_LEN + (size_t)data_len;
    if (crc_offset + BLE_FRAME_CRC_LEN > frame_len) {
        TUYA_BLE_HAL_LOGW("[RX] Invalid data_len=%d (frame=%u)", data_len, (unsigned)frame_len);
        return;
    }
    /* The Frame length is declared by its header; CBC padding after the CRC is
     * neither validated nor stripped (TuyaOpen ble_packet_recv). The real app
     * pads a block-aligned Frame with a full extra PKCS#7 block, so the
     * decrypted buffer is often longer than the Frame — never use it as the
     * length. */
    size_t exact_len = crc_offset + BLE_FRAME_CRC_LEN;
    if (frame_len != exact_len) {
        TUYA_BLE_HAL_LOGI("[RX] Frame %u bytes, buffer %u (padding ignored)",
                          (unsigned)exact_len, (unsigned)frame_len);
    }
    uint16_t recv_crc = (frame[crc_offset] << 8) | frame[crc_offset + 1];
    uint16_t calc_crc = crc16_modbus(frame, crc_offset);
    if (recv_crc != calc_crc) {
        TUYA_BLE_HAL_LOGW("[RX] CRC mismatch: recv=0x%04X calc=0x%04X", recv_crc, calc_crc);
        return;
    }

    TUYA_BLE_HAL_LOGI("[RX] SN=%lu, CMD=0x%04X, LEN=%d, CRC=OK",
                      (unsigned long)sn, cmd, data_len);

    /* Only validated, authorized Frames may commit counters or key material. */
    if (sn == 0 || sn <= state->last_rx_sn) {
        TUYA_BLE_HAL_LOGW("[RX] Stale/replayed SN=%lu (last=%lu)",
                          (unsigned long)sn, (unsigned long)state->last_rx_sn);
        return;
    }
    if (cmd == FRM_QRY_DEV_INFO_REQ) {
        if (encrypt_mode != ENCRYPTION_MODE_KEY_11 || state->tx_count || data_len < 2) {
            TUYA_BLE_HAL_LOGW("[RX] DevInfo gate: mode=0x%02X tx=%d len=%u",
                              encrypt_mode, state->tx_count, data_len);
            return;
        }
        uint16_t peer = ((uint16_t)data[0] << 8) | data[1];
        if (peer < 20 || peer > TUYA_BLE_TX_BUF_SIZE) {
            TUYA_BLE_HAL_LOGW("[RX] DevInfo peer_pkt_len=%u out of range", peer);
            return;
        }
        memcpy(state->server_rand, packet + 1, 16);
        memcpy(state->key_11, candidate_key, 16);
        state->peer_pkt_len = peer;
    } else if (cmd == FRM_PAIR_REQ) {
        if (encrypt_mode != ENCRYPTION_MODE_KEY_12 || !state->handshake_ready ||
            state->authenticated || data_len < TUYA_BLE_ID_LEN) {
            TUYA_BLE_HAL_LOGW("[RX] Pair gate: mode=0x%02X hs=%d auth=%d len=%u",
                              encrypt_mode, state->handshake_ready, state->authenticated, data_len);
            return;
        }
    } else if (cmd == FRM_DOWNLINK_TRANSPARENT_REQ) {
        if (encrypt_mode != ENCRYPTION_MODE_KEY_12 || !state->authenticated || !state->paired) {
            TUYA_BLE_HAL_LOGW("[RX] Creds gate: mode=0x%02X auth=%d paired=%d",
                              encrypt_mode, state->authenticated, state->paired);
            return;
        }
    } else if (cmd == TUYA_BLE_FRM_BIGDATA_DOWNLINK_REQ) {
        if (encrypt_mode != ENCRYPTION_MODE_KEY_12 || !state->authenticated || !state->paired) {
            TUYA_BLE_HAL_LOGW("[RX] Big-data gate: mode=0x%02X auth=%d paired=%d",
                              encrypt_mode, state->authenticated, state->paired);
            return;
        }
    } else {
        TUYA_BLE_HAL_LOGW("[RX] Unhandled CMD=0x%04X", cmd);
        return;
    }
    state->last_rx_sn = sn;

    switch (cmd) {
    case FRM_QRY_DEV_INFO_REQ:
        handle_dev_info_req(state, data, data_len);
        break;
    case FRM_PAIR_REQ:
        handle_pair_req(state, data, data_len);
        break;
    case FRM_DOWNLINK_TRANSPARENT_REQ:
        handle_wifi_config(state, data, data_len);
        break;
    case TUYA_BLE_FRM_BIGDATA_DOWNLINK_REQ:
        (void)tuya_ble_bigdata_on_downlink(state, data, data_len);
        break;
    default:
        TUYA_BLE_HAL_LOGW("[RX] Unhandled CMD=0x%04X", cmd);
        break;
    }
}

static void build_adv_data(tuya_ble_prov_state_t *state)
{
    state->adv_len = 0;
    state->rsp_len = 0;

    state->adv_data[state->adv_len++] = 0x02;
    state->adv_data[state->adv_len++] = 0x01;
    state->adv_data[state->adv_len++] = 0x06;

    state->adv_data[state->adv_len++] = 0x03;
    state->adv_data[state->adv_len++] = 0x02;
    state->adv_data[state->adv_len++] = TUYA_SVC_UUID_LO;
    state->adv_data[state->adv_len++] = TUYA_SVC_UUID_HI;

    state->adv_data[state->adv_len++] = 3 + 2 + 2 + TUYA_BLE_ID_LEN;
    state->adv_data[state->adv_len++] = 0x16;
    state->adv_data[state->adv_len++] = TUYA_SVC_UUID_LO;
    state->adv_data[state->adv_len++] = TUYA_SVC_UUID_HI;

    uint16_t frame_ctrl = 0;
    SETBIT(frame_ctrl, 2);
    SETBIT(frame_ctrl, 3);
    SETBIT(frame_ctrl, 8);
    SETBIT(frame_ctrl, 9);
    SETBIT(frame_ctrl, 14);

    uint8_t *key_in = &state->adv_data[state->adv_len];
    state->adv_data[state->adv_len++] = (frame_ctrl >> 8) & 0xFF;
    state->adv_data[state->adv_len++] = frame_ctrl & 0xFF;
    state->adv_data[state->adv_len++] = 0x00;
    state->adv_data[state->adv_len++] = TUYA_BLE_ID_LEN;

    memcpy(&state->adv_data[state->adv_len], state->cfg.product_key, TUYA_BLE_ID_LEN);
    state->adv_len += TUYA_BLE_ID_LEN;

    state->rsp_data[state->rsp_len++] = 0x17;
    state->rsp_data[state->rsp_len++] = 0xFF;
    state->rsp_data[state->rsp_len++] = TUYA_COMPANY_ID_LO;
    state->rsp_data[state->rsp_len++] = TUYA_COMPANY_ID_HI;
    state->rsp_data[state->rsp_len++] = TUYA_ENCRY_MODE;
    state->rsp_data[state->rsp_len++] = (state->cfg.comm_ability >> 8) & 0xFF;
    state->rsp_data[state->rsp_len++] = state->cfg.comm_ability & 0xFF;

    uint8_t *flag = &state->rsp_data[state->rsp_len++];
    *flag = 0x00;
    if (state->cfg.wifi_scan_request != NULL) *flag |= ADV_FLAG_WIFI_LIST;
    if (state->is_id_comp) *flag |= ADV_FLAG_UUID_COMP;

    rsp_id_encrypt(key_in, TUYA_BLE_ID_LEN + 4, state->ble_id, TUYA_BLE_ID_LEN,
                   &state->rsp_data[state->rsp_len]);
    state->rsp_len += TUYA_BLE_ID_LEN;

    TUYA_BLE_HAL_LOGI("UUID raw: %s (len=%d, compressed=%d)", state->cfg.uuid,
                      (int)strlen(state->cfg.uuid), state->is_id_comp);


    size_t name_len = strlen(state->cfg.device_name);
    if (name_len > TUYA_BLE_NAME_MAX_LEN) name_len = TUYA_BLE_NAME_MAX_LEN;
    state->rsp_data[state->rsp_len++] = name_len + 1;
    state->rsp_data[state->rsp_len++] = 0x09;
    memcpy(&state->rsp_data[state->rsp_len], state->cfg.device_name, name_len);
    state->rsp_len += name_len;

    TUYA_BLE_HAL_LOGI("adv_data (%d bytes), rsp_data (%d bytes)", state->adv_len, state->rsp_len);
}

int tuya_ble_prov_init(tuya_ble_prov_state_t *state, const tuya_ble_prov_cfg_ext_t *cfg)
{
    if (state == NULL || cfg == NULL || cfg->device_name == NULL ||
        cfg->product_key == NULL || cfg->uuid == NULL || cfg->auth_key == NULL) {
        return -1;
    }

    size_t supplied_uuid_len = strlen(cfg->uuid);
    if (strlen(cfg->product_key) != 16 || strlen(cfg->auth_key) != 32 ||
        (supplied_uuid_len != 16 && supplied_uuid_len != 20)) return -1;
    if (supplied_uuid_len == 20) {
        for (size_t i = 0; i < supplied_uuid_len; i++) {
            unsigned char c = cfg->uuid[i];
            if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') ||
                  (c >= 'a' && c <= 'z'))) return -1;
        }
    }
    memset(state, 0, sizeof(*state));
    state->cfg = *cfg;
    state->gatt_payload = 20;
    state->peer_pkt_len = 20;
    if (state->cfg.comm_ability == 0) {
        state->cfg.comm_ability = TUYA_BLE_COMM_ABILITY_2_4_GHZ;
    }

    size_t uuid_len = strlen(cfg->uuid);
    if (uuid_len >= 20) {
        ble_id_compress((const uint8_t *)cfg->uuid, state->ble_id);
        state->is_id_comp = true;
    } else {
        memset(state->ble_id, 0, sizeof(state->ble_id));
        memcpy(state->ble_id, cfg->uuid, uuid_len > TUYA_BLE_ID_LEN ? TUYA_BLE_ID_LEN : uuid_len);
        state->is_id_comp = false;
    }

    build_adv_data(state);
    return 0;
}

void tuya_ble_prov_reset_conn(tuya_ble_prov_state_t *state)
{
    if (state == NULL) return;
    state->trsmitr_seq = 0;
    state->rx_len = 0;
    state->rx_total_len = 0;
    state->rx_next_subpkg = 0;
    state->tx_count = state->tx_head = 0;
    state->tx_offset = 0;
    state->tx_subpkg = 0;
    state->credentials_pending = false;
    state->wifi_scan_pending = false;
    state->wifi_scan_token++;
    if (state->wifi_scan_token == 0) state->wifi_scan_token++;
}

void tuya_ble_prov_close(tuya_ble_prov_state_t *state)
{
    if (!state) return;
    tuya_ble_prov_cfg_ext_t cfg = state->cfg;
    uint64_t now_ms = state->now_ms;
    uint32_t generation = state->connection_generation + 1;
    uint32_t wifi_scan_token = state->wifi_scan_token + 1;
    if (wifi_scan_token == 0) wifi_scan_token++;
    /* Volatile stores erase session keys, credentials and queued Packets. */
    volatile uint8_t *p = (volatile uint8_t *)state;
    for (size_t i = 0; i < sizeof(*state); i++) p[i] = 0;
    (void)tuya_ble_prov_init(state, &cfg);
    state->connection_generation = generation;
    state->wifi_scan_token = wifi_scan_token;
    state->now_ms = now_ms;
}

void tuya_ble_prov_get_adv_data(const tuya_ble_prov_state_t *state,
                                 const uint8_t **adv_data, uint8_t *adv_len,
                                 const uint8_t **rsp_data, uint8_t *rsp_len)
{
    if (adv_data) *adv_data = state->adv_data;
    if (adv_len) *adv_len = state->adv_len;
    if (rsp_data) *rsp_data = state->rsp_data;
    if (rsp_len) *rsp_len = state->rsp_len;
}

void tuya_ble_prov_get_read_payload(const tuya_ble_prov_state_t *state,
                                     const uint8_t **adv_data, uint8_t *adv_len,
                                     const uint8_t **rsp_data, uint8_t *rsp_len)
{
    tuya_ble_prov_get_adv_data(state, adv_data, adv_len, rsp_data, rsp_len);
}

void tuya_ble_prov_set_paired(tuya_ble_prov_state_t *state, bool paired)
{
    if (state) {
        state->paired = paired;
        if (!paired) state->authenticated = false;
    }
}
