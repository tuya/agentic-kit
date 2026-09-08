#ifndef TUYA_BLE_INTERNAL_H
#define TUYA_BLE_INTERNAL_H
#include "tuya_ble_prov.h"
/* Called only after bounded Trsmitr reassembly, on the BLE owner context. */
void tuya_ble_recv(tuya_ble_prov_state_t *state, const uint8_t *packet, uint16_t packet_len);
#endif
