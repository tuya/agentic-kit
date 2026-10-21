#include "tuya_ble_dp.h"
#include "tuya_ble_internal.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#define DP_CMD_QUERY       0x0003
#define DP_CMD_WRITE       0x0027
#define DP_CMD_REPORT      0x8006
#define DP_CMD_REPORT_TIME 0x8007
#define DP_REPORT_PREFIX   7
#define DP_TIME_PREFIX     5
#define DP_FRAME_OVERHEAD  14
#define DP_PACKET_OVERHEAD 17

static uint16_t read_u16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static uint32_t read_unsigned(const uint8_t *p, size_t len)
{
    uint32_t value = 0;
    for (size_t i = 0; i < len; i++) value = (value << 8) | p[i];
    return value;
}

static void write_u32(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)(value >> 24);
    p[1] = (uint8_t)(value >> 16);
    p[2] = (uint8_t)(value >> 8);
    p[3] = (uint8_t)value;
}

static int utf8_without_nul(const uint8_t *p, size_t len)
{
    for (size_t i = 0; i < len;) {
        unsigned first = p[i++];
        if (first == 0) return -1;
        if (first < 0x80) continue;

        size_t continuation;
        unsigned low = 0x80, high = 0xbf;
        if (first >= 0xc2 && first <= 0xdf) {
            continuation = 1;
        } else if (first >= 0xe0 && first <= 0xef) {
            continuation = 2;
            if (first == 0xe0) low = 0xa0;
            if (first == 0xed) high = 0x9f;
        } else if (first >= 0xf0 && first <= 0xf4) {
            continuation = 3;
            if (first == 0xf0) low = 0x90;
            if (first == 0xf4) high = 0x8f;
        } else {
            return -1;
        }
        if (len - i < continuation || p[i] < low || p[i] > high) return -1;
        i++;
        while (--continuation) {
            if (p[i] < 0x80 || p[i] > 0xbf) return -1;
            i++;
        }
    }
    return 0;
}

int tuya_ble_dp_validate(const uint8_t *tlv, size_t len)
{
    uint8_t seen[32] = {0};
    if (!tlv || !len) return -1;

    size_t offset = 0;
    while (offset < len) {
        if (len - offset < 4) return -1;
        unsigned id = tlv[offset];
        unsigned type = tlv[offset + 1];
        size_t value_len = read_u16(tlv + offset + 2);
        if (!id || (seen[id >> 3] & (1u << (id & 7))) ||
            value_len > len - offset - 4) return -1;
        seen[id >> 3] |= (uint8_t)(1u << (id & 7));
        const uint8_t *value = tlv + offset + 4;
        switch (type) {
        case TUYA_BLE_DP_TYPE_RAW:
            break;
        case TUYA_BLE_DP_TYPE_BOOL:
            if (value_len != 1 || value[0] > 1) return -1;
            break;
        case TUYA_BLE_DP_TYPE_VALUE:
            if (value_len != 4) return -1;
            break;
        case TUYA_BLE_DP_TYPE_STRING:
            if (utf8_without_nul(value, value_len)) return -1;
            break;
        case TUYA_BLE_DP_TYPE_ENUM:
        case TUYA_BLE_DP_TYPE_BITMAP:
            if (value_len != 1 && value_len != 2 && value_len != 4) return -1;
            break;
        default:
            return -1;
        }
        offset += 4 + value_len;
    }
    return 0;
}

typedef struct {
    char *out;
    size_t cap;
    size_t used;
} json_writer_t;

static int append(json_writer_t *writer, const char *text, size_t len)
{
    if (len >= writer->cap - writer->used) return -1;
    memcpy(writer->out + writer->used, text, len);
    writer->used += len;
    writer->out[writer->used] = '\0';
    return 0;
}

static int append_string(json_writer_t *writer, const uint8_t *value, size_t len)
{
    static const char hex[] = "0123456789abcdef";
    if (append(writer, "\"", 1)) return -1;
    for (size_t i = 0; i < len; i++) {
        unsigned c = value[i];
        if (c == '"' || c == '\\') {
            char escaped[2] = {'\\', (char)c};
            if (append(writer, escaped, sizeof(escaped))) return -1;
        } else if (c < 0x20) {
            char escaped[6] = {'\\', 'u', '0', '0', hex[c >> 4], hex[c & 15]};
            if (append(writer, escaped, sizeof(escaped))) return -1;
        } else {
            char byte = (char)c;
            if (append(writer, &byte, 1)) return -1;
        }
    }
    return append(writer, "\"", 1);
}

static int append_base64(json_writer_t *writer, const uint8_t *value, size_t len)
{
    static const char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    if (append(writer, "\"", 1)) return -1;
    for (size_t i = 0; i < len;) {
        size_t remaining = len - i;
        unsigned a = value[i++];
        unsigned b = remaining > 1 ? value[i++] : 0;
        unsigned c = remaining > 2 ? value[i++] : 0;
        char group[4] = {
            alphabet[a >> 2], alphabet[((a & 3) << 4) | (b >> 4)],
            remaining > 1 ? alphabet[((b & 15) << 2) | (c >> 6)] : '=',
            remaining > 2 ? alphabet[c & 63] : '='
        };
        if (append(writer, group, sizeof(group))) return -1;
    }
    return append(writer, "\"", 1);
}

int tuya_ble_dp_to_json(const uint8_t *tlv, size_t len,
                        char *out, size_t cap, size_t *written)
{
    if (written) *written = 0;
    if (out && cap) out[0] = '\0';
    if (!out || !cap || !written || tuya_ble_dp_validate(tlv, len)) return -1;

    json_writer_t writer = {.out = out, .cap = cap};
    if (append(&writer, "{", 1)) goto fail;
    for (size_t offset = 0; offset < len;) {
        unsigned id = tlv[offset];
        unsigned type = tlv[offset + 1];
        size_t value_len = read_u16(tlv + offset + 2);
        const uint8_t *value = tlv + offset + 4;
        char key[8];
        int count = snprintf(key, sizeof(key), "%s\"%u\":", offset ? "," : "", id);
        if (count < 0 || (size_t)count >= sizeof(key) ||
            append(&writer, key, (size_t)count)) goto fail;

        if (type == TUYA_BLE_DP_TYPE_RAW) {
            if (append_base64(&writer, value, value_len)) goto fail;
        } else if (type == TUYA_BLE_DP_TYPE_STRING) {
            if (append_string(&writer, value, value_len)) goto fail;
        } else if (type == TUYA_BLE_DP_TYPE_BOOL) {
            const char *boolean = value[0] ? "true" : "false";
            if (append(&writer, boolean, value[0] ? 4 : 5)) goto fail;
        } else {
            uint32_t scalar = read_unsigned(value, value_len);
            char number[16];
            if (type == TUYA_BLE_DP_TYPE_VALUE) {
                /* Avoid an implementation-defined uint32_t -> int32_t cast. */
                int64_t signed_value = scalar;
                if (scalar & UINT32_C(0x80000000)) signed_value -= INT64_C(0x100000000);
                count = snprintf(number, sizeof(number), "%" PRId64, signed_value);
            } else {
                count = snprintf(number, sizeof(number), "%" PRIu32, scalar);
            }
            if (count < 0 || (size_t)count >= sizeof(number) ||
                append(&writer, number, (size_t)count)) goto fail;
        }
        offset += 4 + value_len;
    }
    if (append(&writer, "}", 1)) goto fail;
    *written = writer.used;
    return 0;

fail:
    out[0] = '\0';
    return -1;
}

static int authorized(const tuya_ble_prov_state_t *state)
{
    return state && state->cfg.local_key && state->authenticated && state->paired;
}

static int report(tuya_ble_prov_state_t *state, const uint8_t *tlv, size_t len,
                   bool with_time, uint32_t utc_seconds, bool query_response)
{
    uint8_t payload[TUYA_BLE_TX_BUF_SIZE];
    size_t prefix = DP_REPORT_PREFIX + (with_time ? DP_TIME_PREFIX : 0);
    size_t plaintext_cap = ((TUYA_BLE_TX_BUF_SIZE - DP_PACKET_OVERHEAD) / 16) * 16;
    if (!authorized(state) || state->dp_report_sn == UINT32_MAX ||
        len > plaintext_cap - DP_FRAME_OVERHEAD - prefix ||
        tuya_ble_dp_validate(tlv, len)) return -1;

    uint32_t report_sn = state->dp_report_sn + 1;
    payload[0] = 0; /* DP protocol version. */
    write_u32(payload + 1, report_sn);
    payload[5] = query_response ? 1 : 0;
    payload[6] = 0; /* No segmentation at the DP layer. */
    if (with_time) {
        payload[7] = 1; /* Unix UTC seconds, not a monotonic timestamp. */
        write_u32(payload + 8, utc_seconds);
    }
    memcpy(payload + prefix, tlv, len);
    int rc = tuya_ble_prov_send_report_frame(state,
                                            with_time ? DP_CMD_REPORT_TIME : DP_CMD_REPORT,
                                            payload, (uint16_t)(prefix + len));
    if (!rc) state->dp_report_sn = report_sn;
    return rc == 0 ? 0 : -1;
}

int tuya_ble_dp_report(tuya_ble_prov_state_t *state,
                       const uint8_t *tlv, size_t len, bool query_response)
{
    return report(state, tlv, len, false, 0, query_response);
}

int tuya_ble_dp_report_with_time(tuya_ble_prov_state_t *state,
                                 const uint8_t *tlv, size_t len,
                                 uint32_t utc_seconds, bool query_response)
{
    return report(state, tlv, len, true, utc_seconds, query_response);
}

static int query_ids_valid(const uint8_t *ids, size_t count)
{
    uint8_t seen[32] = {0};
    if (count > 255 || (count && !ids)) return -1;
    for (size_t i = 0; i < count; i++) {
        unsigned id = ids[i];
        if (!id || (seen[id >> 3] & (1u << (id & 7)))) return -1;
        seen[id >> 3] |= (uint8_t)(1u << (id & 7));
    }
    return 0;
}

int tuya_ble_dp_on_frame(tuya_ble_prov_state_t *state, uint16_t cmd,
                         const uint8_t *data, uint16_t len)
{
    if (!authorized(state) || (len && !data)) return -1;
    if (cmd == DP_CMD_REPORT || cmd == DP_CMD_REPORT_TIME) return 0;
    /* Do not admit application work if its immediate ACK has no queue slot. */
    if (state->tx_count >= TUYA_BLE_TX_QUEUE_DEPTH || state->sn == UINT32_MAX ||
        !state->cfg.send_fn) return -1;
    if (cmd == DP_CMD_WRITE) {
        if (len < 5) return -1;
        uint8_t response[6];
        memcpy(response, data, 5);
        response[5] = 1;
        if (data[0] == 0 && !tuya_ble_dp_validate(data + 5, len - 5) &&
            state->cfg.dp_write &&
            state->cfg.dp_write(data + 5, len - 5, state->cfg.dp_ctx) == 0) {
            response[5] = 0;
        }
        return tuya_ble_prov_send_frame(state, cmd, response, sizeof(response),
                                         TUYA_BLE_ENCRYPTION_MODE_KEY_15);
    }
    if (cmd == DP_CMD_QUERY) {
        uint8_t response = 1;
        if (!query_ids_valid(data, len) && state->cfg.dp_query &&
            state->cfg.dp_query(data, len, state->cfg.dp_ctx) == 0) response = 0;
        return tuya_ble_prov_send_frame(state, cmd, &response, 1,
                                         TUYA_BLE_ENCRYPTION_MODE_KEY_15);
    }
    return -1;
}
