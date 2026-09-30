# IoT Client (Device ↔ Tuya Cloud)

The device-side context that authenticates a device with the Tuya cloud and exchanges
device state over MQTT: activation, the Data Point model, and DP reporting/downlink.

## Language

**Data Point (DP)**:
A single addressable unit of device state or capability, identified by a numeric id
(1–255) and typed by the product schema.
_Avoid_: attribute, property, point, tag.

**Schema**:
A product's complete, versioned set of DP definitions — each DP's id, type, access mode,
and value constraints — carried as a JSON array.
_Avoid_: model, profile, template.

**Schema ID**:
The stable identifier for a product's DP-set; it survives schema-version upgrades and is
the key used to fetch the newest schema.
_Avoid_: product key (a different credential).

**DP type**:
The data kind of a DP: one of `bool`, `value` (integer), `string`, `enum`, `raw`.

**Access mode**:
A DP's read/write direction from the cloud/app's point of view: `ro` (report-only —
device→cloud), `wr` (write-only — cloud→device), or `rw` (both). The device may report
`ro` and `rw` DPs but not `wr`; the cloud may set any DP on downlink, so the access mode
only restricts device-initiated writes (to `wr` DPs).
_Avoid_: permission.

**DP state**:
The device's current values for its DPs, serialisable as a `{"dps":{...}}` document for
persistence and restore.
_Avoid_: snapshot (reserve that for the serialised form), payload.

**Report** (uplink):
A device→cloud push of current DP values.
_Avoid_: publish (that is the transport verb), sync, send.

**Downlink** (DP set):
A cloud→device message that sets DP values.
_Avoid_: command, control, write.

**Activation** (on-boarding):
First-time provisioning that authenticates the device and returns its credentials
(devid / secret_key / local_key) together with its schema and schema id.
_Avoid_: pairing, registration, binding (those are app/cloud-side terms).

**Registration key**:
The opaque four-byte secret appended to the App's BLE authToken after the
two-byte region and eight-byte activation token, or the 1-4 byte `data.env`
from QR/MQTT activation (default `pro` when absent). The device passes it unchanged
as IoT DNS `env` to discover the Self HTTPS/MQTT endpoints before activation;
it is not the device credential `secret_key` and is not an `iot_env_t` value.
Applications persist it together with credentials and region, then restore
`iot_client_config_t.registration_key` on reboot. Older records without a key
retain their legacy `env` routing; do not force them to `pro` or re-activate them.
_Avoid_: mapping its spelling to production/pre-production enum values.

**Schema upgrade**:
Replacing the device's schema with a newer version for the same Schema ID, fetched by
the application polling the cloud (there is no MQTT schema-change notification).
_Avoid_: migration, update (too generic).

**ATOP interface**:
A named device-side HTTP service in the cloud, identified by the pair `api` name and
`version` — e.g. `tuya.device.upgrade.get` v4.4. Nothing else distinguishes one from
another.
_Avoid_: interface (too generic), HTTP request (that is the transport).

**Envelope**:
The outer `{success, result, t, errorCode, errorMsg}` structure wrapping every ATOP
response. A well-formed envelope does not mean the call succeeded: `success` is a
separate verdict, and a rejection carries `errorCode` instead of `result`.
_Avoid_: response, payload.

**Named wrapper**:
A typed SDK function covering one ATOP interface — it builds the request body, parses
the result into a struct, and defines who frees what. E.g. `iot_ota_check_upgrade()`.
_Avoid_: 封装 / wrapping (verb, ambiguous about which layer), packaging.

**Generic call**:
The public entry point (`iot_atop_call()`) that reaches any ATOP interface by `api` +
`version`, JSON in and JSON out, with no typing. Signing and encryption still happen
inside the SDK.
_Avoid_: passthrough (implies unsigned, which it is not), raw call.

### Flagged ambiguities

- **"state"** is overloaded: *DP state* (the values), *schema* (the definitions), and
  *connection state* (MQTT up/down) are three different things — always qualify it.
- **"update"** is overloaded: *DP set* (cloud changes a value) vs *schema upgrade*
  (the DP definitions change). Use the specific term.
- **"封装" / "wrap"** is overloaded when talking about cloud interfaces: adding a
  *named wrapper* to the SDK is a different act from calling an interface through the
  *generic call*. Say which one — "does this need a named wrapper?" is answerable,
  "should we wrap this?" is not.

## Example dialogue

> **Dev:** When the cloud turns the light on, that's a downlink?
> **Expert:** Right — a downlink DP set on DP 1 (a `bool`, `rw`). We update the local DP
> state and call the app's DP callback. We don't report it back; the cloud already knows.
> **Dev:** And when the device itself changes — say a sensor reading?
> **Expert:** The app sets the DP locally, which marks it dirty, then reports it (uplink).
> A report is the only thing that refreshes the cloud's cached DP state.
> **Dev:** What if the product gains a new DP later?
> **Expert:** That's a schema upgrade, not a DP set. Same Schema ID, newer schema; the app
> polls for it, we rebuild the registry, and the app persists the new schema.
