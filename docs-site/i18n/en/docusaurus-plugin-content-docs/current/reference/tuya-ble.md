---
title: Tuya BLE provisioning and local control
sidebar_label: Tuya BLE
---

# Tuya BLE provisioning and local control

## Responsibilities {#overview}

`modules/tuya-ble` (CMake target `tuya_ble`) provides two device-to-app BLE modes:
unactivated devices receive WiFi credentials after Pairing; activated devices use saved
Activation credentials for local DP control. Both reuse advertising, encrypted Frames and
Trsmitr segmentation, with their mode selected by initialization configuration.

BLE owns neither the product schema nor DP state and creates no thread. Reception, TX-ready,
monotonic ticks, DP callbacks and reports all run on the port's BLE owner context. The
application connects local control to existing IoT DP state and hardware behavior. LAN,
MQTT and BLE can connect and report independently.

The protocol follows a [fixed TuyaOpen version](https://github.com/tuya/TuyaOpen/tree/68f76c966ef6154c319ce122506d4e0934b2979b/src/tuya_cloud_service/ble).
For WiFi onboarding, see the [BLE provisioning tutorial](../tutorials/pair-by-ble.md).

## Configuration and modes {#configuration}

`tuya_ble_prov_cfg_ext_t` in public header `tuya_ble_prov.h` retains the existing
Provisioning configuration and adds:

- `local_key` and `secret_key`: saved Activation credentials for the same device, each
  exactly 16 bytes followed by NUL. Both NULL preserve Provisioning; supplying both
  enables Bound control. A missing partner or invalid length fails initialization.
  The caller owns these strings for the entire BLE state lifetime.
- `dp_write(tlv, len, dp_ctx)`: receives a validated DP TLV batch.
- `dp_query(ids, count, dp_ctx)`: receives validated DP IDs; an empty list queries all.
- `dp_ctx`: application context shared by the two DP callbacks.

Callback buffers are borrowed only for that call. Return 0 after copying and accepting
work; any other result sends a failure ACK to the app. Callbacks must not block, reenter
the BLE state or call MQTT. Reject work when the application queue is full. The ACK
means the request was accepted; the application handles later schema or hardware results.

This runtime configuration is independent of `AGENTIC_KIT_ENABLE_LAN`: disabling LAN
still allows BLE local control. Rebuild consumers for the updated public config/state layout.

## Authentication and protocol {#authentication}

Provisioning retains KEY_11 / KEY_12. Bound control uses KEY_14 / KEY_15:

```text
KEY_14 = MD5(local_key || secret_key)
KEY_15 = MD5(local_key || secret_key || pair_rand)
```

`pair_rand` is the six-byte device nonce generated for this device-info exchange. Bound
advertising and device-info carry the bound indicators; successful Pairing returns result
2. Only KEY_15 Frames after successful Pairing may execute DP writes (`0x0027`) and
queries (`0x0003`). Provisioning keys cannot authorize Bound control, and Provisioning
mode does not accept DP control. Bound control disables WiFi credentials and big-data
Provisioning commands.

Frames still check length, CRC, mode, command authorization and nonzero increasing SN.
The compatibility `set_paired(true)` interface cannot grant cryptographic authorization.
This capability does not automatically unbind, reset or perform complete PSK3.0 Activation.

## DP TLV and conversion {#dp-format}

Public header `tuya_ble_dp.h` provides `tuya_ble_dp_validate()` and
`tuya_ble_dp_to_json()`. A batch contains entries encoded as
`[DPID:1][type:1][length:2][value:length]`, with a big-endian length. DP IDs must be
nonzero and unique within a batch. The whole batch is validated before conversion or callbacks.

- raw (0): arbitrary bytes, converted to a Base64 JSON string.
- bool (1): one byte containing 0 or 1, converted to JSON boolean.
- value (2): four-byte big-endian signed integer, converted to JSON number.
- string (3): valid UTF-8, converted to a JSON string with special characters escaped.
- enum (4) and bitmap (5): one-, two- or four-byte big-endian unsigned integer,
  converted to JSON number.

For example, bool `true` for DP 1 is `01 01 00 01 01`, producing bare DP JSON
`{"1":true}`. `tuya_ble_dp_to_json(tlv, len, out, capacity, &written)` writes into a
caller-owned bounded buffer and returns the actual JSON length; the length passed to
the IoT input excludes the NUL terminator.

BLE validates the wire representation. The application still checks schema types, ranges
and product authorization policy. The existing IoT registry has no bitmap type; products
may handle bitmap TLVs directly in their BLE callback without using that registry.

## Reusing IoT DP state {#integration}

The application connects the two owner contexts as follows:

1. The BLE callback copies the whole TLV batch or query IDs and the current
   `connection_generation` into an application queue.
2. The IoT owner takes a write, converts it with `tuya_ble_dp_to_json()` and calls
   `iot_client_apply_local_dps(client, json, len)`. Queries read the same product DP state.
3. The application builds state TLVs from the actual result and queues a copied report
   with the original generation back to the BLE owner.
4. The BLE owner discards results with stale generations and explicitly reports to the
   current connection.

These queues belong to the application; the SDK adds no worker. Do not read BLE state
directly from another task. Carry the generation through queues or maintain application
validity markers in their respective owner contexts. Port stop, reinitialization and
credential replacement must also cancel or discard old requests and reports.

`iot_client_apply_local_dps()` validates the complete batch against the schema, stages
the update, then marks accepted state dirty and invokes existing persistence/DP callbacks.
It does not publish MQTT. The application explicitly calls `iot_dp_report_all_dirty()`
on the IoT owner for cloud reports and still calls `iot_dp_report_all()` after cloud
reconnection. The application encodes query state TLVs using its product types; there
is no automatic JSON-to-TLV conversion or automatic state reply.

## Explicit state reports {#reporting}

`tuya_ble_dp_report(state, tlv, len, query_response)` sends `0x8006`.
`tuya_ble_dp_report_with_time(state, tlv, len, utc_seconds, query_response)` sends
`0x8007`, using application-supplied Unix UTC seconds. Both require a bound connection
with successful Pairing and use the same TLV validation and bounded TX queue.

`query_response = true` marks query results; `false` marks unsolicited status. Write ACKs,
query ACKs and actual state reports are separate steps. The module does not generate
reports after writes or queries. Successful queueing does not prove that the phone
received a report; handle errors and port backpressure. BLE reports neither publish
MQTT nor clear MQTT dirty bits.

## Lifecycle and ports {#lifecycle}

Before using JSON Provisioning, call `iot_init()` or `iot_init_default()` once to bind
the shared PAL, RNG and cJSON allocator. The port supplies a full-fill CSPRNG and drives
`tuya_ble_prov_on_data()`, `tuya_ble_prov_tx_ready()` and monotonic
`tuya_ble_prov_tick()`.

Call `tuya_ble_prov_close()` on GATT disconnect or BLE host reset. It wipes session data,
keys and queued Packets while retaining borrowed configuration for reconnect. Historical
`reset_conn()` preserves Pairing and keys and cannot replace close. Close and an accepted
device-info re-query change `connection_generation`, so asynchronous results from an
old Pairing exchange must not reach a new connection or handshake.

On Activation, unbind, reset or credential rotation, stop the port, discard old queue
items and reinitialize with current Activation credentials. Never mutate borrowed
credential strings while running. Completing Provisioning does not automatically switch
to Bound control.

Trsmitr supports segmentation even with the default 20-byte GATT budget. The port can
update that budget through `tuya_ble_prov_set_gatt_payload()`. Send callbacks must copy
buffers before returning: 0 means accepted, 1 means busy, and other values mean failure.
The port retries through TX-ready/tick; timeouts close the session. The SDK has no
background thread; the existing NimBLE Provisioning example owns its WiFi scan task.

## Verification and boundaries {#verification}

```bash
cmake --build build --target tuya_ble_test -j 4
ctest --test-dir build -R '^tuya_ble_test$' --output-on-failure
```

Regression tests use an independent encrypted peer for Provisioning and local-control
Frames without cloud credentials. Keep the complete ctest suite serial. Validate target
radio behavior, the real Tuya App and the product panel separately on hardware.
