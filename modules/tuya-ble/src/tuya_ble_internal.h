#ifndef TUYA_BLE_INTERNAL_H
#define TUYA_BLE_INTERNAL_H
#include "tuya_ble_prov.h"
#include "log.h"

/* tuya-ble knob defaults (none yet) live in include/tuya_ble_config_defaults.h,
 * included here so future knobs are wired from day one; it pulls common/log.h
 * first -- that header is where integrator overrides are picked up. */
#include "tuya_ble_config_defaults.h"

#include <stdio.h>

/* Shared SDK-internal binding of the HAL log macros onto the global log
 * facade: every module diagnostic carries the "[ble] " prefix, and the
 * compile-time ceiling is the SDK-wide AGENTIC_KIT_LOG_LEVEL (log.h). */
#undef TUYA_BLE_HAL_LOGI
#undef TUYA_BLE_HAL_LOGW
#undef TUYA_BLE_HAL_LOGE
#undef TUYA_BLE_HAL_HEXDUMP
#define TUYA_BLE_HAL_LOGI(fmt, ...) log_tag_debug("ble", fmt, ##__VA_ARGS__)
#define TUYA_BLE_HAL_LOGW(fmt, ...) log_tag_warn("ble",  fmt, ##__VA_ARGS__)
#define TUYA_BLE_HAL_LOGE(fmt, ...) log_tag_error("ble", fmt, ##__VA_ARGS__)
/* HEXDUMP formats every byte before dispatching; below the debug ceiling
 * the whole body and its arguments are compiled out. */
#if AGENTIC_KIT_LOG_LEVEL >= 4
#define TUYA_BLE_HAL_HEXDUMP(buf, len)                                         \
    do {                                                                       \
        const uint8_t *p_ = (const uint8_t *)(buf);                            \
        size_t n_ = (len);                                                      \
        char hex_[193];                                                         \
        size_t o_ = 0;                                                          \
        for (size_t i_ = 0; i_ < n_ && o_ + 3 < sizeof(hex_); i_++) {           \
            o_ += (size_t)snprintf(hex_ + o_, sizeof(hex_) - o_, "%02X ",       \
                                   p_[i_]);                                     \
        }                                                                      \
        if (o_) hex_[o_ - 1] = '\0';                                            \
        log_tag_debug("ble", "HEX(%u): %s", (unsigned)n_, hex_);                \
    } while (0)
#else
#define TUYA_BLE_HAL_HEXDUMP(...) ((void)0)
#endif

/* Scan tokens are nonzero and never reused across invalidations: bump and
 * skip the zero wrap. */
static inline uint32_t tuya_ble_next_scan_token(uint32_t token)
{
    token++;
    return token ? token : 1;
}

/* Deliver the pending credentials once the last queued Packet has been
 * accepted by the port: called right after the credential ACK is queued and
 * again from tuya_ble_prov_tx_ready() once the TX queue drains. */
void tuya_ble_prov_flush_credentials(tuya_ble_prov_state_t *state);

/* Called only after bounded Trsmitr reassembly, on the BLE owner context. */
void tuya_ble_recv(tuya_ble_prov_state_t *state, const uint8_t *packet, uint16_t packet_len);
#endif
