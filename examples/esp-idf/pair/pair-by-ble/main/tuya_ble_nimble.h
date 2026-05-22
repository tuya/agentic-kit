#ifndef TUYA_BLE_NIMBLE_H
#define TUYA_BLE_NIMBLE_H

#include "tuya_ble_prov.h"

#ifdef __cplusplus
extern "C" {
#endif

int tuya_ble_nimble_start(const tuya_ble_prov_cfg_t *cfg);
int tuya_ble_nimble_stop(void);

#ifdef __cplusplus
}
#endif

#endif /* TUYA_BLE_NIMBLE_H */
