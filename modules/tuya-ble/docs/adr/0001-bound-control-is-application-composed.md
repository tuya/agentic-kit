# Bound control is application-composed

Activated devices need BLE DP control as well as WiFi Provisioning. The existing
IoT Client already owns product schema, DP state, persistence and hardware
callbacks. BLE callbacks run on the port's BLE owner, whereas the MQTT client
requires a single application owner. Calling the IoT Client directly from BLE
would introduce a second MQTT owner through existing product callbacks.

The BLE module therefore validates the wire representation and exposes bounded
`dp_write` / `dp_query` callbacks. The application copies accepted work to its
IoT owner, converts DP TLV to bare JSON when appropriate, and uses the public
`iot_client_apply_local_dps()` input. Products may handle TLVs directly where
their state model differs, including bitmap DPs unsupported by the IoT registry.
The BLE module imports no IoT private API and creates no worker.

Bound control is selected at initialization by supplying both borrowed
Activation credentials, `local_key` and `secret_key`, each exactly 16 bytes.
Neither supplied preserves Provisioning; supplying only one is an error.
The two modes have separate authorization: Provisioning uses KEY_11/KEY_12,
while Bound control uses KEY_14/KEY_15 and requires successful Pairing before
DP commands. Bound control advertises the bound identity and disables WiFi
credential/big-data Provisioning commands. A Provisioning key cannot downgrade
the device into accepting Bound-control commands.

A successful write/query callback means the application accepted the copied
request. Its ACK cannot promise later schema validation or hardware success.
The application explicitly constructs BLE reports and returns them to the BLE
owner, choosing query-response flags and optional UTC seconds. BLE does not
automatically report after writes or queries, publish MQTT, or clear MQTT dirty
bits. Cloud reporting remains the application's decision.

## Consequences

- Existing Provisioning configurations retain their mode when both new
  credentials are NULL. The public config/state layout changes require a rebuild.
- DP callback data is borrowed only for that call. A full application queue must
  reject work rather than acknowledge a request it cannot retain.
- Queue items capture their originating `connection_generation`. Session close
  and accepted device-info re-query invalidate old Pairing exchanges; the BLE
  owner drops stale completions before sending reports to the new exchange.
- Ports close the state on GATT disconnect. Credential replacement requires
  stopping the port, discarding old queued work and reinitializing with the new
  Activation artefact; borrowed credential strings must not change live.
- Bound control is independent of the optional LAN build capability and does
  not add automatic unbind/reset, complete PSK3.0 activation or P2P media.
