/*
 * tuya_ble_config_defaults.h -- tuya-ble build-time knobs. There are none
 * today: the module's only config dependency is the SDK-wide log ceiling
 * (AGENTIC_KIT_LOG_LEVEL, applied in src/tuya_ble_internal.h).
 *
 * What looks like knobs in the module's public headers is deliberately not
 * movable into this file:
 *   - TUYA_BLE_RX_BUF_SIZE / TUYA_BLE_TX_BUF_SIZE / TUYA_BLE_TX_QUEUE_DEPTH
 *     (include/tuya_ble_prov.h) size the public tuya_ble_prov_state_t that
 *     ports embed and size their own buffers against -- they are part of
 *     the port API surface, not build knobs;
 *   - the rest (SSID/token/name lengths, frame IDs, encryption modes,
 *     radio capability bits, and the WiFi-list caps / derived JSON bound in
 *     tuya_ble_bigdata.h) is protocol geometry fixed by the Tuya BLE
 *     pairing / big-data protocol.
 * Future tuya-ble product-tunable knobs land here, named
 * AGENTIC_KIT_TUYA_BLE_*.
 *
 * src/tuya_ble_internal.h includes this file (mirroring the other modules),
 * so future knobs are wired from day one; it pulls common/log.h FIRST --
 * that header is where integrator overrides (agentic_kit_config.h on the
 * include path, -D, AGENTIC_KIT_USER_CONFIG) are applied and the SDK-wide
 * log ceiling defaults. Not a public API header.
 */

#ifndef AGENTIC_KIT_TUYA_BLE_CONFIG_DEFAULTS_H
#define AGENTIC_KIT_TUYA_BLE_CONFIG_DEFAULTS_H

#include "log.h"

/* No tuya-ble knobs yet -- see the banner above for why the module's
 * existing constants stay in their public headers. */

#endif /* AGENTIC_KIT_TUYA_BLE_CONFIG_DEFAULTS_H */
