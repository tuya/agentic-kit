#ifndef _IOT_DP_H_
#define _IOT_DP_H_

/*
 * Data Point (DP) management layer for the Tuya IoT client.
 *
 * Sits on top of the existing transport layer: uplink reuses iot_client_publish()
 * (P2.3 encryption -> smart/device/out/{devid}); downlink is intercepted on the
 * existing iot_client_process() receive path. No new encryption path, no new
 * threads/timers. See the design doc for the full picture.
 *
 * Persistence is NOT performed by the SDK. The SDK only provides the mechanism:
 *   - export current DP state via iot_dp_dump_json() (pull) or the save callback (push);
 *   - restore schema/schema_id via iot_client_config_t and DP values via
 *     iot_client_config_t.dp_state / iot_dp_restore_json() on startup.
 * The application decides when/where/how to persist.
 */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "iot_client.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- DP error codes (continue the iot_client.h OPRT_* sequence; last was
 *      OPRT_TLS_HANDSHAKE_FAILED = -0x0007) ---- */
#define OPRT_DP_SCHEMA_PARSE_FAILED  (-0x0008) //-8,  Schema JSON could not be parsed
#define OPRT_DP_INVALID_ID           (-0x0009) //-9,  No such DP id in the schema registry
#define OPRT_DP_TYPE_MISMATCH        (-0x000A) //-10, Value type does not match the DP type
#define OPRT_DP_VALUE_OUT_OF_RANGE   (-0x000B) //-11, Value out of range / too long / not in enum
#define OPRT_DP_PAYLOAD_TOO_LARGE    (-0x000C) //-12, Report payload exceeds the MQTT packet budget — split via iot_dp_report()

/**
 * @brief DP value types (mirrors the Tuya product schema types).
 */
typedef enum {
    IOT_DP_TYPE_BOOL = 0,   // boolean
    IOT_DP_TYPE_VALUE,      // integer value (with optional min/max/scale/step)
    IOT_DP_TYPE_STRING,     // UTF-8 string (with optional maxlen)
    IOT_DP_TYPE_ENUM,       // enumeration (stored as an index into the schema range[])
    IOT_DP_TYPE_RAW,        // opaque bytes (with optional maxlen)
} iot_dp_type_t;

/**
 * @brief Tagged DP value.
 *
 * Ownership of string/raw pointers:
 *  - In iot_dp_callback_t the string/raw pointers are SDK-owned and valid ONLY
 *    during the callback. Copy them if you need to retain.
 *  - In iot_dp_set()/iot_dp_report() the SDK copies string/raw internally, so the
 *    caller may free the input immediately after the call returns.
 *  - In iot_dp_get() the returned string/raw point into SDK-owned storage, valid
 *    until the next set/dispatch for that dp_id (or deinit). Do not free, and do
 *    NOT retain the pointer: the next iot_dp_set/iot_dp_report or downlink for that
 *    dp_id frees the backing buffer, leaving any stored pointer dangling. Copy the
 *    bytes out if you need them beyond the immediate read. (Scalar bool/value/enum
 *    values are returned by copy and carry no such hazard.)
 */
typedef struct {
    iot_dp_type_t type;
    union {
        bool        boolean;
        int32_t     integer;       // IOT_DP_TYPE_VALUE: raw integer value
        const char *string;        // IOT_DP_TYPE_STRING: NUL-terminated UTF-8
        int32_t     enum_index;    // IOT_DP_TYPE_ENUM: index into schema range[]
        struct {
            const uint8_t *data;
            size_t         len;
        } raw;                     // IOT_DP_TYPE_RAW
    } value;
} iot_dp_value_t;

/**
 * @brief Cloud-to-device DP callback.
 *
 * Fired once per changed DP after a downlink DP-set, after the internal lock is
 * released. @p value (and any string/raw it points to) is valid only for the
 * duration of the call.
 */
typedef void (*iot_dp_callback_t)(uint8_t dp_id, const iot_dp_value_t *value, void *user_data);

/**
 * @brief Schema-upgrade notification.
 *
 * Fired by iot_dp_schema_check_update() when a newer schema is fetched and the
 * registry has been rebuilt. @p new_schema is SDK-owned and valid only during
 * the callback; the application should persist it (keyed by @p schema_id).
 */
typedef void (*iot_schema_update_callback_t)(const char *schema_id, const char *new_schema, void *user_data);

/**
 * @brief DP-state-changed (persistence) callback.
 *
 * Fired (batched once per change event, after the internal lock is released)
 * whenever a DP is written — via a downlink DP-set, iot_dp_set(), or
 * iot_dp_report(). It fires even when the value written equals the current one
 * (there is no value-equality check), so treat it as "a DP was written" rather
 * than strictly "the value differs". @p dp_state_json is the current snapshot
 * {"dps":{...}},
 * SDK-owned and valid only during the callback; write it (or copy it) before
 * returning. Each callback carries the complete current state, so dropping
 * intermediate callbacks (debounce / rate-limit flash writes) is safe.
 *
 * RAW DPs are omitted from the snapshot: raw carries transient binary frames
 * that are reported uplink but never persisted/restored.
 *
 * NOT fired by iot_dp_restore_json(), startup dp_state restore, the report_all*
 * helpers, or a protocol-5 query.
 *
 * Do NOT mutate DP state (iot_dp_set / iot_dp_report) from within this callback;
 * doing so would trigger a re-entrant save. Persist (or copy) and return.
 */
typedef void (*iot_dp_save_callback_t)(const char *dp_state_json, void *user_data);

/* ---- Callback registration ---- */

/**
 * @brief Register the cloud-to-device DP callback. cb/user_data may be NULL.
 * @return OPRT_OK, or OPRT_INVALID_PARAMETER if client is NULL,
 *         OPRT_UNINITIALIZED if the DP context could not be allocated.
 */
IOT_API int iot_dp_set_callback(iot_client_t *client, iot_dp_callback_t cb, void *user_data);

/**
 * @brief Register the schema-upgrade notification callback. cb/user_data may be NULL.
 */
IOT_API int iot_dp_set_schema_update_callback(iot_client_t *client, iot_schema_update_callback_t cb, void *user_data);

/**
 * @brief Register the DP-state-changed (persistence) callback. cb/user_data may be NULL.
 */
IOT_API int iot_dp_set_save_callback(iot_client_t *client, iot_dp_save_callback_t cb, void *user_data);

/* ---- Local state ---- */

/**
 * @brief Validate and update a DP in the local cache, marking it dirty. Does NOT publish.
 *
 * Use for sensor/state changes that will later be batch-reported via
 * iot_dp_report_all_dirty(). Fires the save callback (if registered).
 *
 * @return OPRT_OK, or OPRT_DP_INVALID_ID / OPRT_DP_TYPE_MISMATCH /
 *         OPRT_DP_VALUE_OUT_OF_RANGE on validation failure.
 */
IOT_API int iot_dp_set(iot_client_t *client, uint8_t dp_id, const iot_dp_value_t *value);

/**
 * @brief Read the current cached value of a DP into @p out.
 *
 * For STRING/RAW, the pointers in @p out reference SDK-owned storage; do not free.
 *
 * RISK — the returned STRING/RAW pointer aliases the live DP cache and has no
 * independent lifetime. The backing buffer is freed by the NEXT write to the same
 * dp_id (iot_dp_set / iot_dp_report / a downlink DP-set), after which any retained
 * pointer dangles (use-after-free if dereferenced):
 *   - Single-threaded: safe only if you consume or copy the value BEFORE the next
 *     write to that dp_id; never stash the pointer across such a call.
 *   - Multi-threaded: a concurrent write to the same dp_id on another thread can
 *     free the buffer WHILE you read it. The caller MUST serialize its read-and-use
 *     against writes to that dp_id, or copy the bytes out under its own lock.
 * Scalar (bool/value/enum) results are returned by copy and carry no such hazard.
 *
 * @return OPRT_OK, OPRT_DP_INVALID_ID if unknown, OPRT_INVALID_RESULT if no value yet.
 */
IOT_API int iot_dp_get(iot_client_t *client, uint8_t dp_id, iot_dp_value_t *out);

/* ---- Reporting (uplink) ---- */

/**
 * @brief Validate, cache, and immediately report a single DP. Clears its dirty bit on success.
 *        Fires the save callback (if registered).
 */
IOT_API int iot_dp_report(iot_client_t *client, uint8_t dp_id, const iot_dp_value_t *value);

/**
 * @brief Report all dirty DPs in one batched message; clears dirty bits on success.
 * @return OPRT_OK (also when nothing is dirty); OPRT_DP_PAYLOAD_TOO_LARGE if the
 *         batch exceeds the MQTT packet budget (split via iot_dp_report());
 *         or a transport error if the publish fails.
 */
IOT_API int iot_dp_report_all_dirty(iot_client_t *client);

/**
 * @brief Report every known DP regardless of dirty state (reconnect sync); clears
 *        dirty on success. A DP counts as "known" once it has a value (set/reported/
 *        restored, or seeded by first-activation defaults); schema DPs that have
 *        never held a value are skipped. If no DP has a value yet (e.g. a clean
 *        first boot with no defaults seeded), this publishes nothing and returns OPRT_OK.
 * @return OPRT_OK; OPRT_DP_PAYLOAD_TOO_LARGE if the batch exceeds the MQTT packet
 *         budget (split via iot_dp_report()); or a transport error.
 */
IOT_API int iot_dp_report_all(iot_client_t *client);

/* ---- Persistence mechanism ---- */

/**
 * @brief Export the current DP state as a {"dps":{...}} JSON string for persistence.
 *
 * RAW DPs are omitted: raw carries transient binary frames that are reported
 * uplink but never persisted/restored.
 *
 * @param[out] out_json  Receives a newly allocated string; the caller MUST free
 *                       it via the PAL allocator (client->pal->free). Empty
 *                       ({"dps":{}}) in loose mode or when no persistable value is set.
 * @return OPRT_OK on success.
 */
IOT_API int iot_dp_dump_json(iot_client_t *client, char **out_json);

/**
 * @brief Validate a {"dps":{...}} JSON string against the current schema, WITHOUT
 *        applying it (no cache mutation, no dirty, no save callback).
 *
 * Use on boot to vet a persisted dp_state before restoring it: validate first,
 * then restore only on OPRT_OK (all-or-nothing), discard otherwise. This is
 * stricter than iot_dp_restore_json, which skips bad entries individually.
 *
 * @return OPRT_OK if every entry matches a schema DP with a valid value;
 *         otherwise the first failure: OPRT_DP_SCHEMA_PARSE_FAILED (bad JSON),
 *         OPRT_INVALID_PARAMETER (no "dps" object), OPRT_DP_INVALID_ID,
 *         OPRT_DP_TYPE_MISMATCH, or OPRT_DP_VALUE_OUT_OF_RANGE.
 */
IOT_API int iot_dp_validate_json(iot_client_t *client, const char *dp_state_json);

/**
 * @brief Restore DP values from a {"dps":{...}} JSON string.
 *
 * Restores cached values WITHOUT marking dirty and WITHOUT publishing; the save
 * callback is NOT fired. Unknown / mistyped / out-of-range entries are logged
 * and skipped (restore never fails the whole operation). To reject a
 * non-conforming snapshot wholesale instead, gate this on iot_dp_validate_json().
 */
IOT_API int iot_dp_restore_json(iot_client_t *client, const char *dp_state_json);

/* ---- Schema upgrade ---- */

/**
 * @brief Query the cloud for the newest schema (tuya.device.schema.newest.get).
 *
 * On a newer schema: replaces client->schema, rebuilds the registry, and fires
 * the schema-update callback so the application can persist it. On no update
 * (empty / []): returns OPRT_OK without changes. Call this from the application
 * loop (optionally gated on a protocol-27 notice).
 */
IOT_API int iot_dp_schema_check_update(iot_client_t *client);

#ifdef __cplusplus
}
#endif

#endif /* _IOT_DP_H_ */
