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

**Schema upgrade**:
Replacing the device's schema with a newer version for the same Schema ID, fetched by
the application polling the cloud (there is no MQTT schema-change notification).
_Avoid_: migration, update (too generic).

### Flagged ambiguities

- **"state"** is overloaded: *DP state* (the values), *schema* (the definitions), and
  *connection state* (MQTT up/down) are three different things — always qualify it.
- **"update"** is overloaded: *DP set* (cloud changes a value) vs *schema upgrade*
  (the DP definitions change). Use the specific term.

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
