#ifndef TUYA_BLE_DP_H
#define TUYA_BLE_DP_H

#include "tuya_ble_prov.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Each DP TLV is [id:1][type:1][length:2, big-endian][value:length].
 * IDs are nonzero and unique within one batch. Multi-byte scalars are
 * big-endian. A value is signed int32; enums and bitmaps are unsigned. */
#define TUYA_BLE_DP_TYPE_RAW    0
#define TUYA_BLE_DP_TYPE_BOOL   1
#define TUYA_BLE_DP_TYPE_VALUE  2
#define TUYA_BLE_DP_TYPE_STRING 3
#define TUYA_BLE_DP_TYPE_ENUM   4
#define TUYA_BLE_DP_TYPE_BITMAP 5

/* Validate a complete, nonempty batch before accepting any of its DPs.
 * Strings must be valid UTF-8 without embedded NUL. Empty raw/string values
 * are supported. Returns 0 on success, -1 on invalid input. */
int tuya_ble_dp_validate(const uint8_t *tlv, size_t len);

/* Convert a validated batch to the bare JSON object accepted by
 * iot_client_apply_local_dps(), for example {"1":true,"2":-3}. Raw bytes use
 * base64; enums and bitmaps use numbers. The IoT schema remains responsible
 * for DP support and constraints (including enum label conversion).
 *
 * No allocation occurs. cap includes the trailing NUL; written excludes it.
 * On failure, written is zero and out[0] is NUL when out/cap permit it.
 * Copy/queue requests to the application owner before applying JSON to an
 * IoT client; these helpers do not change the BLE callback's thread context. */
int tuya_ble_dp_to_json(const uint8_t *tlv, size_t len,
                        char *out, size_t cap, size_t *written);

/* Explicitly report DP TLVs on an authenticated bound-device connection.
 * query_response selects type 1 (query response); false selects type 0
 * (application-initiated report). These calls do not publish to MQTT or
 * mutate IoT DP state. Call only on the BLE owner context, outside callbacks.
 * An accepted write/query ACK does not automatically send a report.
 *
 * Returns 0 after the bounded TX queue accepts the Packet, -1 for invalid
 * TLVs, absent authorization, exhausted counters, capacity or send failure.
 * The report sequence is independent of the Frame SN. */
int tuya_ble_dp_report(tuya_ble_prov_state_t *state,
                       const uint8_t *tlv, size_t len, bool query_response);
int tuya_ble_dp_report_with_time(tuya_ble_prov_state_t *state,
                                 const uint8_t *tlv, size_t len,
                                 uint32_t utc_seconds, bool query_response);

#ifdef __cplusplus
}
#endif

#endif /* TUYA_BLE_DP_H */
