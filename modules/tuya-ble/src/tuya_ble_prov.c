#include "tuya_ble_prov.h"

#include "cJSON.h"
#include "mbedtls/aes.h"
#include "mbedtls/md5.h"

#include <string.h>

#define FRM_QRY_DEV_INFO_REQ         0x0000
#define FRM_PAIR_REQ                 0x0001
#define FRM_RPT_NET_STAT_REQ         0x001E
#define FRM_DOWNLINK_TRANSPARENT_REQ 0x801B
#define FRM_UPLINK_TRANSPARENT_REQ   0x801C

#define ENCRYPTION_MODE_NONE        0x00
#define ENCRYPTION_MODE_KEY_11      0x0B
#define ENCRYPTION_MODE_KEY_12      0x0C

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
#define TUYA_COMM_ABILITY   0x000C
#define TUYA_ENCRY_MODE     0x00

#define ADV_FLAG_UUID_COMP  (1 << 0)
#define SETBIT(val, bit)    ((val) |= (1 << (bit)))

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

static int var_len_encode(uint32_t value, uint8_t *buf)
{
    int len = 0;
    do {
        uint8_t byte = value & 0x7F;
        value >>= 7;
        if (value > 0) byte |= 0x80;
        buf[len++] = byte;
    } while (value > 0);
    return len;
}

static int var_len_decode(const uint8_t *buf, uint16_t buf_len, uint32_t *value)
{
    *value = 0;
    int shift = 0;
    for (int i = 0; i < (int)buf_len && i < 4; i++) {
        *value |= (uint32_t)(buf[i] & 0x7F) << shift;
        shift += 7;
        if ((buf[i] & 0x80) == 0) {
            return i + 1;
        }
    }
    return -1;
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

    uint8_t pad = out[in_len - 1];
    if (pad == 0 || pad > 16) {
        *out_len = in_len;
    } else {
        *out_len = in_len - pad;
    }
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

    uint16_t frame_len = BLE_FRAME_HEADER_LEN + data_len + BLE_FRAME_CRC_LEN;
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
            tuya_ble_hal_random(iv, 16);
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

    uint8_t *trsmitr_buf = state->tx_trsmitr_buf;
    int toff = 0;

    trsmitr_buf[toff++] = 0x00;
    toff += var_len_encode(enc_pkt_len, &trsmitr_buf[toff]);
    trsmitr_buf[toff++] = (TUYA_BLE_PROTOCOL_VER_HI << 4) | (state->trsmitr_seq & 0x0F);
    state->trsmitr_seq++;
    memcpy(&trsmitr_buf[toff], enc_pkt, enc_pkt_len);
    uint16_t total_len = toff + enc_pkt_len;

    TUYA_BLE_HAL_LOGI("[TX] trsmitr_len=%d (hdr=%d + enc=%d)", total_len, toff, enc_pkt_len);
    TUYA_BLE_HAL_HEXDUMP(trsmitr_buf, total_len);

    int rc = state->cfg.send_fn(trsmitr_buf, total_len, state->cfg.send_ctx);
    if (rc != 0) {
        TUYA_BLE_HAL_LOGE("Notify send failed, rc=%d", rc);
    }
    return rc;
}

static void handle_dev_info_req(tuya_ble_prov_state_t *state, const uint8_t *data, uint16_t data_len)
{
    TUYA_BLE_HAL_LOGI("[PROTO] FRM_QRY_DEV_INFO_REQ, data_len=%d", data_len);

    tuya_ble_hal_random(state->pair_rand, TUYA_BLE_PAIR_RAND_LEN);
    TUYA_BLE_HAL_LOGI("[PROTO] pair_rand:");
    TUYA_BLE_HAL_HEXDUMP(state->pair_rand, TUYA_BLE_PAIR_RAND_LEN);
    TUYA_BLE_HAL_LOGI("[PROTO] server_rand (APP's IV):");
    TUYA_BLE_HAL_HEXDUMP(state->server_rand, 16);

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
    (void)reg_key_ret;
    TUYA_BLE_HAL_LOGI("[PROTO] register_key ret=%d", reg_key_ret);
    TUYA_BLE_HAL_HEXDUMP(&resp[14], 16);

    resp[52] = (TUYA_COMM_ABILITY >> 8) & 0xFF;
    resp[53] = TUYA_COMM_ABILITY & 0xFF;
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

    TUYA_BLE_HAL_LOGI("[PROTO] DevInfo response, %d bytes", payload_len);
    TUYA_BLE_HAL_HEXDUMP(resp, payload_len);
    tuya_ble_send(state, FRM_QRY_DEV_INFO_REQ, resp, payload_len, ENCRYPTION_MODE_KEY_11);
}

static void handle_pair_req(tuya_ble_prov_state_t *state, const uint8_t *data, uint16_t data_len)
{
    TUYA_BLE_HAL_LOGI("[PROTO] FRM_PAIR_REQ, data_len=%d", data_len);
    if (data_len > 0) {
        TUYA_BLE_HAL_HEXDUMP(data, data_len);
    }

    uint8_t result = 0x00;
    if (data_len >= TUYA_BLE_ID_LEN) {
        if (memcmp(data, state->ble_id, TUYA_BLE_ID_LEN) != 0) {
            TUYA_BLE_HAL_LOGW("[PROTO] BLE ID mismatch in pair request!");
            TUYA_BLE_HAL_LOGI("[PROTO] Expected BLE ID:");
            TUYA_BLE_HAL_HEXDUMP(state->ble_id, TUYA_BLE_ID_LEN);
            result = 0x01;
        }
    }

    if (result == 0x00) {
        state->paired = true;
        TUYA_BLE_HAL_LOGI("[PROTO] Paired successfully");
    }

    uint8_t resp[1] = {result};
    tuya_ble_send(state, FRM_PAIR_REQ, resp, 1, ENCRYPTION_MODE_KEY_12);

    if (state->paired) {
        uint8_t net_stat = 0x00;
        tuya_ble_send(state, FRM_RPT_NET_STAT_REQ, &net_stat, 1, ENCRYPTION_MODE_KEY_12);
    }
}

static void handle_wifi_config(tuya_ble_prov_state_t *state, const uint8_t *data, uint16_t data_len)
{
    TUYA_BLE_HAL_LOGI("[PROTO] FRM_DOWNLINK_TRANSPARENT_REQ (%d bytes):", data_len);
    TUYA_BLE_HAL_HEXDUMP(data, data_len);

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
    if (json_len >= sizeof(json_str)) json_len = sizeof(json_str) - 1;
    memcpy(json_str, &data[offset], json_len);
    json_str[json_len] = '\0';

    TUYA_BLE_HAL_LOGI("[PROTO] WiFi JSON: %s", json_str);

    cJSON *root = cJSON_Parse(json_str);
    if (root == NULL) {
        TUYA_BLE_HAL_LOGE("JSON parse failed");
        return;
    }

    memset(&state->creds, 0, sizeof(state->creds));

    cJSON *ssid = cJSON_GetObjectItem(root, "ssid");
    if (cJSON_IsString(ssid) && ssid->valuestring) {
        strncpy(state->creds.ssid, ssid->valuestring, TUYA_BLE_SSID_MAX_LEN);
        TUYA_BLE_HAL_LOGI("[PROTO] SSID: %s", state->creds.ssid);
    }

    cJSON *pwd = cJSON_GetObjectItem(root, "pwd");
    if (cJSON_IsString(pwd) && pwd->valuestring) {
        strncpy(state->creds.password, pwd->valuestring, TUYA_BLE_PASSWORD_MAX_LEN);
        TUYA_BLE_HAL_LOGI("[PROTO] Password: %s", state->creds.password);
    }

    cJSON *token = cJSON_GetObjectItem(root, "token");
    if (cJSON_IsString(token) && token->valuestring) {
        strncpy(state->creds.token, token->valuestring, TUYA_BLE_TOKEN_MAX_LEN);
        TUYA_BLE_HAL_LOGI("[PROTO] Token: %s", state->creds.token);
    }

    cJSON_Delete(root);

    uint8_t resp[5] = {0x00, 0x00, 0x00, 0x01, 0x00};
    tuya_ble_send(state, FRM_UPLINK_TRANSPARENT_REQ, resp, sizeof(resp), ENCRYPTION_MODE_KEY_12);

    if (state->creds.ssid[0] && state->cfg.cb) {
        state->cfg.cb(&state->creds);
    }
}

static void tuya_ble_recv(tuya_ble_prov_state_t *state, const uint8_t *packet, uint16_t packet_len)
{
    TUYA_BLE_HAL_LOGI("[RX] packet_len=%d", packet_len);
    TUYA_BLE_HAL_HEXDUMP(packet, packet_len > 64 ? 64 : packet_len);

    if (packet_len < 2) return;

    uint8_t encrypt_mode = packet[0];
    TUYA_BLE_HAL_LOGI("[RX] encrypt_mode=0x%02X", encrypt_mode);

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
            TUYA_BLE_HAL_LOGW("[RX] Encrypted packet too short");
            return;
        }
        const uint8_t *iv = &packet[1];
        const uint8_t *enc_data = &packet[17];
        uint16_t enc_len = packet_len - 17;

        uint8_t dec_key[16];
        int ret = 0;
        if (encrypt_mode == ENCRYPTION_MODE_KEY_11) {
            memcpy(state->server_rand, iv, 16);
            ret = generate_key_11((const uint8_t *)state->cfg.auth_key,
                                  state->ble_id, iv, dec_key);
            if (ret != 0) return;
            memcpy(state->key_11, dec_key, 16);
            TUYA_BLE_HAL_LOGI("[RX] Cached service_rand and KEY_11");
            TUYA_BLE_HAL_HEXDUMP(state->server_rand, 16);
        } else if (encrypt_mode == ENCRYPTION_MODE_KEY_12) {
            ret = generate_key_12(state->key_11, state->pair_rand, dec_key);
            if (ret != 0) return;
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

    uint32_t sn = (frame[0] << 24) | (frame[1] << 16) | (frame[2] << 8) | frame[3];
    state->last_rx_sn = sn;
    uint16_t cmd = (frame[8] << 8) | frame[9];
    uint16_t data_len = (frame[10] << 8) | frame[11];
    const uint8_t *data = &frame[12];

    if (cmd == FRM_QRY_DEV_INFO_REQ && data_len >= 2) {
        state->peer_pkt_len = ((uint16_t)data[0] << 8) | data[1];
        TUYA_BLE_HAL_LOGI("[PROTO] peer_pkt_len=%u", state->peer_pkt_len);
    }

    uint16_t crc_offset = BLE_FRAME_HEADER_LEN + data_len;
    if (crc_offset + 2 > frame_len) {
        TUYA_BLE_HAL_LOGW("[RX] Invalid data_len=%d", data_len);
        return;
    }
    uint16_t recv_crc = (frame[crc_offset] << 8) | frame[crc_offset + 1];
    uint16_t calc_crc = crc16_modbus(frame, crc_offset);
    if (recv_crc != calc_crc) {
        TUYA_BLE_HAL_LOGW("[RX] CRC mismatch: recv=0x%04X calc=0x%04X", recv_crc, calc_crc);
        return;
    }

    TUYA_BLE_HAL_LOGI("[RX] SN=%lu, CMD=0x%04X, LEN=%d, CRC=OK",
                      (unsigned long)sn, cmd, data_len);

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
    state->rsp_data[state->rsp_len++] = (TUYA_COMM_ABILITY >> 8) & 0xFF;
    state->rsp_data[state->rsp_len++] = TUYA_COMM_ABILITY & 0xFF;

    uint8_t *flag = &state->rsp_data[state->rsp_len++];
    *flag = 0x00;
    if (state->is_id_comp) *flag |= ADV_FLAG_UUID_COMP;

    rsp_id_encrypt(key_in, TUYA_BLE_ID_LEN + 4, state->ble_id, TUYA_BLE_ID_LEN,
                   &state->rsp_data[state->rsp_len]);
    state->rsp_len += TUYA_BLE_ID_LEN;

    TUYA_BLE_HAL_LOGI("UUID raw: %s (len=%d, compressed=%d)", state->cfg.uuid,
                      (int)strlen(state->cfg.uuid), state->is_id_comp);
    TUYA_BLE_HAL_LOGI("ble_id:");
    TUYA_BLE_HAL_HEXDUMP(state->ble_id, TUYA_BLE_ID_LEN);

    uint8_t name_len = strlen(state->cfg.device_name);
    if (name_len > TUYA_BLE_NAME_MAX_LEN) name_len = TUYA_BLE_NAME_MAX_LEN;
    state->rsp_data[state->rsp_len++] = name_len + 1;
    state->rsp_data[state->rsp_len++] = 0x09;
    memcpy(&state->rsp_data[state->rsp_len], state->cfg.device_name, name_len);
    state->rsp_len += name_len;

    TUYA_BLE_HAL_LOGI("adv_data (%d bytes), rsp_data (%d bytes)", state->adv_len, state->rsp_len);
    TUYA_BLE_HAL_HEXDUMP(state->adv_data, state->adv_len);
    TUYA_BLE_HAL_HEXDUMP(state->rsp_data, state->rsp_len);
}

int tuya_ble_prov_init(tuya_ble_prov_state_t *state, const tuya_ble_prov_cfg_ext_t *cfg)
{
    if (state == NULL || cfg == NULL || cfg->device_name == NULL ||
        cfg->product_key == NULL || cfg->uuid == NULL || cfg->auth_key == NULL) {
        return -1;
    }

    memset(state, 0, sizeof(*state));
    state->cfg = *cfg;

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
}

int tuya_ble_prov_on_data(tuya_ble_prov_state_t *state, const uint8_t *raw, uint16_t len)
{
    if (state == NULL || raw == NULL || len == 0 || len > TUYA_BLE_RX_BUF_SIZE) {
        return -1;
    }

    TUYA_BLE_HAL_HEXDUMP(raw, len > 64 ? 64 : len);

    int offset = 0;
    uint32_t subpkg_num = 0;
    int consumed = var_len_decode(&raw[offset], len - offset, &subpkg_num);
    if (consumed < 0) {
        TUYA_BLE_HAL_LOGW("[TRSMITR] Failed to decode SubPkgNum");
        return 0;
    }
    offset += consumed;

    if (subpkg_num == 0) {
        uint32_t total_len = 0;
        consumed = var_len_decode(&raw[offset], len - offset, &total_len);
        if (consumed < 0) {
            TUYA_BLE_HAL_LOGW("[TRSMITR] Failed to decode TotalLen");
            return 0;
        }
        offset += consumed;
        state->rx_total_len = total_len;
        state->rx_len = 0;
    }

    if (offset >= len) return 0;
    uint8_t ver_seq = raw[offset++];
    (void)ver_seq;
    TUYA_BLE_HAL_LOGI("[TRSMITR] subpkg=%lu, ver=%d, seq=%d, total=%lu",
                      (unsigned long)subpkg_num, (ver_seq >> 4) & 0x0F,
                      ver_seq & 0x0F, (unsigned long)state->rx_total_len);

    uint16_t payload_len = len - offset;
    if (state->rx_len + payload_len > sizeof(state->rx_buf)) {
        TUYA_BLE_HAL_LOGE("[TRSMITR] RX buffer overflow");
        state->rx_len = 0;
        return 0;
    }
    memcpy(state->rx_buf + state->rx_len, raw + offset, payload_len);
    state->rx_len += payload_len;

    if (state->rx_len >= state->rx_total_len) {
        TUYA_BLE_HAL_LOGI("[TRSMITR] Complete, %d bytes", state->rx_len);
        tuya_ble_recv(state, state->rx_buf, state->rx_len);
        state->rx_len = 0;
    } else {
        TUYA_BLE_HAL_LOGI("[TRSMITR] Partial %d/%lu", state->rx_len, (unsigned long)state->rx_total_len);
    }

    return 0;
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
    if (state) state->paired = paired;
}
