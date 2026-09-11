#ifndef TUYA_BLE_BIGDATA_H
#define TUYA_BLE_BIGDATA_H

#ifdef __cplusplus
extern "C" {
#endif

#include "tuya_ble_prov.h"

#define TUYA_BLE_FRM_BIGDATA_DOWNLINK_REQ 0x801E
#define TUYA_BLE_FRM_BIGDATA_UPLINK_RSP   0x801F
#define TUYA_BLE_SUB_WIFI_LIST            0x0003
#define TUYA_BLE_SUB_NCFG_STAT            0x0004
#define TUYA_BLE_WIFI_LIST_MAX            20
#define TUYA_BLE_WIFI_SSID_MAX            32

/* The encrypted Packet includes a 1-byte mode, 16-byte IV and CBC padding.
 * 974 bytes of JSON yields a 992-byte ciphertext Frame and a 1009-byte Packet. */
#define TUYA_BLE_WIFI_LIST_JSON_MAX 974

typedef struct {
    char ssid[TUYA_BLE_WIFI_SSID_MAX + 1];
    int8_t rssi;
    uint8_t sec;
} tuya_ble_wifi_ap_t;

/* Called after a successful paired KEY_12 0x801E frame. */
int tuya_ble_bigdata_on_downlink(tuya_ble_prov_state_t *state,
                                 const uint8_t *payload, uint16_t len);

/* Complete the one outstanding scan. The completion must run on the same BLE
 * owner context as tuya_ble_prov_on_data. token is supplied to the port's
 * scan request callback; stale, cancelled and mismatched completions fail. */
int tuya_ble_bigdata_wifi_list_complete(tuya_ble_prov_state_t *state,
                                        uint32_t token,
                                        const tuya_ble_wifi_ap_t *aps,
                                        uint16_t count);

#ifdef __cplusplus
}
#endif

#endif /* TUYA_BLE_BIGDATA_H */
