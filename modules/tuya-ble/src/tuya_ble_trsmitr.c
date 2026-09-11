/* Trsmitr wire layout verified against TuyaOpen 1b92eb5a3b86a81d45290043008a95be75ef5574.
 * Independent bounded implementation; no global transfer state or worker. */
#include "tuya_ble_internal.h"
#include <string.h>
#define TUYA_BLE_PROTOCOL_VER_HI 4

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

int tuya_ble_prov_on_data(tuya_ble_prov_state_t *state, const uint8_t *raw, uint16_t len)
{
    if (!state || !raw || !len || len > TUYA_BLE_RX_BUF_SIZE) {
        TUYA_BLE_HAL_LOGW("[TRSMITR] on_data rejected: state=%p raw=%p len=%u",
                          (void *)state, (const void *)raw, len);
        return -1;
    }
    uint32_t subpkg = 0, total = 0;
    int n = var_len_decode(raw, len, &subpkg);
    if (n < 0) {
        TUYA_BLE_HAL_LOGW("[TRSMITR] bad SubPkgNum varint (len=%u)", len);
        goto invalid;
    }
    size_t offset = (size_t)n;
    if (!subpkg) {
        state->rx_len = 0;
        state->rx_total_len = 0;
        n = var_len_decode(raw + offset, len - offset, &total);
        if (n < 0) {
            TUYA_BLE_HAL_LOGW("[TRSMITR] bad TotalLen varint (len=%u)", len);
            goto invalid;
        }
        offset += (size_t)n;
        if (offset >= len || !total || total > sizeof(state->rx_buf)) {
            TUYA_BLE_HAL_LOGW("[TRSMITR] bad TotalLen=%lu (offset=%u, len=%u)",
                              (unsigned long)total, (unsigned)offset, len);
            goto invalid;
        }
        uint8_t ver_seq = raw[offset++];
        if ((ver_seq >> 4) != TUYA_BLE_PROTOCOL_VER_HI) {
            TUYA_BLE_HAL_LOGW("[TRSMITR] unsupported ver=%u (want %u)",
                              ver_seq >> 4, TUYA_BLE_PROTOCOL_VER_HI);
            goto invalid;
        }
        state->rx_total_len = total;
        state->rx_next_subpkg = 0;
        state->rx_started_ms = state->now_ms;
        TUYA_BLE_HAL_LOGI("[TRSMITR] RX start: total=%lu ver=%u seq=%u",
                          (unsigned long)total, ver_seq >> 4, ver_seq & 0x0F);
    }
    /* Continuations have no version/sequence byte (TuyaOpen Trsmitr). */
    if (!state->rx_total_len) {
        TUYA_BLE_HAL_LOGW("[TRSMITR] continuation with no transfer in progress");
        goto invalid;
    }
    if (subpkg != state->rx_next_subpkg) {
        TUYA_BLE_HAL_LOGW("[TRSMITR] out-of-order subpkg=%lu (want %lu)",
                          (unsigned long)subpkg, (unsigned long)state->rx_next_subpkg);
        goto invalid;
    }
    if (offset >= len) {
        TUYA_BLE_HAL_LOGW("[TRSMITR] empty subpkg=%lu", (unsigned long)subpkg);
        goto invalid;
    }
    if (len - offset > state->rx_total_len - state->rx_len) {
        TUYA_BLE_HAL_LOGW("[TRSMITR] overflow: have=%lu+%u > total=%lu",
                          (unsigned long)state->rx_len,
                          (unsigned)(len - offset), (unsigned long)state->rx_total_len);
        goto invalid;
    }
    memcpy(state->rx_buf + state->rx_len, raw + offset, len - offset);
    state->rx_len += len - offset;
    state->rx_next_subpkg++;
    if (state->rx_len == state->rx_total_len) {
        TUYA_BLE_HAL_LOGI("[TRSMITR] RX complete, %u bytes in %lu subpkg(s)",
                          state->rx_len, (unsigned long)state->rx_next_subpkg);
        tuya_ble_recv(state, state->rx_buf, state->rx_len);
        state->rx_len = 0;
        state->rx_total_len = 0;
        state->rx_next_subpkg = 0;
    } else {
        TUYA_BLE_HAL_LOGI("[TRSMITR] Partial %u/%lu",
                          state->rx_len, (unsigned long)state->rx_total_len);
    }
    return 0;
invalid:
    state->rx_len = 0;
    state->rx_total_len = 0;
    state->rx_next_subpkg = 0;
    return -1;
}

int tuya_ble_prov_set_gatt_payload(tuya_ble_prov_state_t *state, uint16_t bytes)
{
    if (!state || bytes < 20 || bytes > 512) {
        TUYA_BLE_HAL_LOGW("[TRSMITR] set_gatt_payload rejected: %u", bytes);
        return -1;
    }
    state->gatt_payload = bytes;
    return 0;
}

int tuya_ble_prov_tx_ready(tuya_ble_prov_state_t *state)
{
    if (!state || !state->cfg.send_fn) return -1;
    while (state->tx_count) {
        uint8_t *out = state->tx_trsmitr_buf;
        uint16_t total = state->tx_queue_len[state->tx_head];
        size_t offset = var_len_encode(state->tx_subpkg, out);
        if (!state->tx_subpkg) {
            offset += var_len_encode(total, out + offset);
            out[offset++] = (TUYA_BLE_PROTOCOL_VER_HI << 4) | (state->trsmitr_seq & 15);
        }
        size_t budget = state->gatt_payload;
        if (budget > state->peer_pkt_len) budget = state->peer_pkt_len;
        if (budget <= offset) return -1;
        size_t count = total - state->tx_offset;
        if (count > budget - offset) count = budget - offset;
        memcpy(out + offset, state->tx_queue[state->tx_head] + state->tx_offset, count);
        TUYA_BLE_HAL_LOGI("[TRSMITR] TX subpkg=%lu, %u+%u bytes (total=%lu)",
                          (unsigned long)state->tx_subpkg, (unsigned)offset,
                          (unsigned)count, (unsigned long)total);
        int rc = state->cfg.send_fn(out, offset + count, state->cfg.send_ctx);
        if (rc == TUYA_BLE_SEND_BUSY) return rc;
        if (rc) {
            tuya_ble_prov_close(state);
            return rc;
        }
        state->tx_offset += count;
        state->tx_subpkg++;
        if (state->tx_offset == total) {
            TUYA_BLE_HAL_LOGI("[TRSMITR] TX complete, %lu bytes in %lu subpkg(s)",
                              (unsigned long)total,
                              (unsigned long)state->tx_subpkg);
            memset(state->tx_queue[state->tx_head], 0, total);
            state->tx_head = (state->tx_head + 1) % TUYA_BLE_TX_QUEUE_DEPTH;
            state->tx_count--;
            state->tx_offset = 0;
            state->tx_subpkg = 0;
            state->trsmitr_seq++;
            state->tx_started_ms = state->now_ms;
        }
    }
    tuya_ble_prov_flush_credentials(state);
    return 0;
}

int tuya_ble_prov_tick(tuya_ble_prov_state_t *state, uint64_t now_ms)
{
    if (!state || now_ms < state->now_ms) return -1;
    state->now_ms = now_ms;
    if (state->rx_total_len && now_ms - state->rx_started_ms >= 10000) {
        TUYA_BLE_HAL_LOGW("[TRSMITR] RX transfer expired after 10s (%u/%lu)",
                          state->rx_len, (unsigned long)state->rx_total_len);
        state->rx_len = 0;
        state->rx_total_len = 0;
        state->rx_next_subpkg = 0;
    }
    if (state->tx_count && now_ms - state->tx_started_ms >= 10000) {
        TUYA_BLE_HAL_LOGW("[TRSMITR] TX stalled after 10s (head=%lu, offset=%lu/%lu)",
                          (unsigned long)state->tx_count, (unsigned long)state->tx_offset,
                          (unsigned long)state->tx_queue_len[state->tx_head]);
        tuya_ble_prov_close(state);
        return -1;
    }
    return tuya_ble_prov_tx_ready(state);
}

