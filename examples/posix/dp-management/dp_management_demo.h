#ifndef DP_MANAGEMENT_DEMO_H
#define DP_MANAGEMENT_DEMO_H

/**
 * @brief Run the DP (Data Point) management demo for an already-activated device.
 *
 * Demonstrates the full device-side DP lifecycle the SDK is built around:
 *   - restore schema + DP state from local persistence on boot;
 *   - connect MQTT and report-on-connect (mandatory; the cloud only learns
 *     state from device-initiated reports — see the DP-layer ADR);
 *   - react to cloud downlinks (DP set) via the DP callback;
 *   - push local changes uplink with iot_dp_set() + iot_dp_report_all_dirty();
 *   - persist every state change via the save callback (app owns storage);
 *   - poll for a schema upgrade and persist the newer schema.
 *
 * The credentials come from a prior activation (run activate_demo first and
 * copy the devid / secret_key / local_key / schema_id it prints).
 *
 * @param devid       Device ID from activation.
 * @param secret_key  Secret key from activation.
 * @param local_key   Local key from activation.
 * @param schema_id   Schema ID from activation (may be NULL/"" — only needed
 *                    for the schema-upgrade query).
 * @return 0 on success, non-zero on error.
 */
int demo_dp_management_run(const char *devid,
                           const char *secret_key,
                           const char *local_key,
                           const char *schema_id);

#endif /* DP_MANAGEMENT_DEMO_H */
