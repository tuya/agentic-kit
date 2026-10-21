---
title: Tuya LAN Local Control
sidebar_label: Tuya LAN
---

# Tuya LAN Local Control

See [Tuya LAN Architecture and Features](../guides/tuya-lan-architecture.md) for
module relationships, feature boundaries and data flow. This page covers APIs,
configuration and integration.

## Responsibilities {#overview}

`modules/tuya-lan` (CMake target `tuya_lan`) provides Tuya LAN 3.5 local control
for activated devices: UDP discovery, TCP Sessions, three-step authentication,
heartbeats, DP queries, local DP controls and explicit status reports. Device
identity and `local_key` come from Activation; the application retains the
schema, DP state and product behavior.

The application drives `tuya_lan_process()`; there is no worker. LAN and MQTT
connections are independent. With saved credentials and schema, LAN control
does not require an active MQTT connection.

The wire reference is this [pinned TuyaOpen version](https://github.com/tuya/TuyaOpen/tree/68f76c966ef6154c319ce122506d4e0934b2979b/src/tuya_cloud_service/lan).
Only LAN 3.5 is implemented. Legacy LAN versions, AP provisioning and P2P media
are separate capabilities.

## Build and Example {#build}

Public headers are `tuya_lan.h` and the unified `pal.h` platform interface.
Networking lives in `pal_posix.c` (BSD sockets) and `pal_freertos.c` (lwIP).
Optional server/datagram callbacks are appended to `pal_t`; all consumers must
rebuild. Custom ports need to implement this group only when using LAN.

LAN is disabled by default. Enable it explicitly with the host CMake option
`AGENTIC_KIT_ENABLE_LAN`:

```bash
cmake -S . -B build -DAGENTIC_KIT_ENABLE_LAN=ON -DPython3_EXECUTABLE=$(which python3)
cmake --build build -j 4
```

To disable it, reconfigure with `cmake -S . -B build -DAGENTIC_KIT_ENABLE_LAN=OFF`.
Disabled builds omit `tuya_lan`, LAN tests, `pal_net_test`,
`pal_freertos_net_test` and `lan_demo`. The optional server/UDP implementations
inside the platform PALs are excluded and their callbacks are NULL; the base PAL
remains available. `iot_client_apply_local_dps()` remains available to other
local control channels.

The standalone example project also needs the option enabled:

```bash
cmake -S examples/posix -B build-examples -DAGENTIC_KIT_ENABLE_LAN=ON -DPython3_EXECUTABLE=$(which python3)
cmake --build build-examples --target lan_demo -j 4
./build-examples/lan_demo 192.168.1.20
```

For ESP-IDF, enable LAN under **Agentic Kit** in `idf.py menuconfig`, or set
`CONFIG_AGENTIC_KIT_ENABLE_LAN=y` in the application's `sdkconfig.defaults`.
It defaults to disabled. The build publishes `AGENTIC_KIT_ENABLE_LAN=0` or `1`
to SDK consumers, so applications can guard integration code with
`#if AGENTIC_KIT_ENABLE_LAN`. The build option determines this macro's value.

Set `TUYA_DEVICE_ID`, `TUYA_LOCAL_KEY`, `TUYA_UUID` and `TUYA_PRODUCT_KEY` in the
environment, using the same device's saved Activation data. The example does
not print keys. `local_key` must be a 16-byte string.

The example exposes bool DP 1 using an offline DP registry without connecting
to the cloud. Products reuse their existing IoT client and actual schema.
Optional extra arguments select TCP/UDP ports; zero selects an ephemeral port.
Defaults are TCP 6668, UDP 7000 and discovery announcements to UDP 6667.

## API and Configuration {#api}

- `tuya_lan_config_init()` sets default ports, up to three Sessions, a 10-second
  handshake deadline, a 30-second heartbeat deadline and five-second discovery
  announcements. `discovery_interval_ms = 0` disables periodic announcements.
- `tuya_lan_create()` / `start()` / `stop()` / `destroy()` allocate, listen,
  close connections and release resources. A stopped service can restart;
  destroy and recreate it when credentials change.
- `tuya_lan_process()` performs bounded nonblocking I/O, discovery and timeout
  work. Call it frequently from the application owner.
- `tuya_lan_report()` explicitly reports a complete `{"dps":{...}}` document to
  authenticated Sessions.
- `tuya_lan_send()` replies to an extension command on a specified Session.
- `tuya_lan_disconnect_all()` closes Sessions; port getters return actual ports.

`start()` / `stop()` control a compiled LAN service at runtime; the build option
above controls whether LAN is compiled.

`on_dp` receives a bare DP object such as `{"1":true}`. Return zero after
accepting it; any other return produces an error reply. `on_query` writes a
complete `{"dps":{...}}` document into a bounded caller buffer and sets its length.
Lengths exclude a terminating NUL. Callback data is borrowed during the call.

## Reusing IoT DP State {#integration}

```c
static int on_lan_dp(const char *json, size_t len, void *user)
{
    return iot_client_apply_local_dps(user, json, len);
}

static int on_lan_query(char *out, size_t cap, size_t *len, void *user)
{
    iot_client_t *client = user;
    char *state = NULL;
    if (iot_dp_dump_json(client, &state) != OPRT_OK) return LAN_ERR_MEMORY;
    size_t n = strlen(state);
    int rc = n <= cap ? LAN_OK : LAN_ERR_LIMIT;
    if (rc == LAN_OK) { memcpy(out, state, n); *len = n; }
    client->pal->free(state);
    return rc;
}
```

`iot_client_apply_local_dps()` validates the complete nonempty batch and prepares
notification snapshots before committing. Errors leave values and dirty flags
unchanged. Success marks local writes dirty, invokes persistence first, then
the existing per-DP callback, all outside the lock. Bare DP IDs must be unique
decimal strings from 1 to 255 without leading zeros.

LAN queries and reports neither clear MQTT dirty bits nor publish to the cloud.
The application invokes `iot_dp_report_all_dirty()` on its IoT owner thread and
still calls `iot_dp_report_all()` after every cloud reconnect. As before,
`iot_dp_dump_json()` omits raw DPs; build an explicit report for raw notifications.
Schema access mode is not enforced; apply product authorization before accepting
local controls.

## Lifecycle and Porting {#lifecycle}

Call `iot_init()` or `iot_init_default()` once at startup to bind the shared
PAL/RNG/cJSON allocator. Config strings must outlive the service. Stop LAN
before unbinding, reset, credential replacement or IoT client release; recreate
it with the new credentials and current IPv4 address.

All LAN calls and callbacks share one application thread. Callbacks may send
or report. Defer destroy, stop, disconnect-all and recursive process calls until
callbacks return. When sharing the MQTT owner, pump LAN frequently enough;
`iot_client_process()` has a compile-time blocking budget.

Ports provide optional listening, accept and datagram operations through the
same `pal_t`. `tuya_lan_create()` checks the full group with
`pal_has_server_network()`; base `pal_is_valid()` still checks only the mandatory
callbacks. Accepted TCP connections reuse `tcp_send/recv/poll/close`; LAN sends
and receives with `timeout_ms=0` so network I/O never waits. Configuration takes
only `config.pal`, reusing the PAL bound during SDK initialization. The module
retains partial writes and handles would-block with bounded per-Session queues
and pump work. Platform socket calls belong in the PAL.

## Protocol Boundaries {#limits}

LAN 3.5 uses AES-GCM Frames and HMAC nonce negotiation. DP, heartbeat and
extensions require authentication. Invalid tags, replay, invalid lengths and
timeouts close the affected Session. The public discovery key grants no control
authorization. Discovery requests must advertise the actual sender IP; replies
go to that source address and port.

LAN and local DP input share bounded JSON string and number checks, rejecting
invalid UTF-8, malformed escapes, unpaired surrogates and decoded NUL before
cJSON validates the JSON structure.

`on_command` receives authenticated extension data, including P2P signaling
command `0x20`. A separate module implements P2P negotiation and media. AI
session tokens are not LAN credentials.

`AGENTIC_KIT_LAN_MAX_FRAME_SIZE` defaults to 4096 (range 128–65536).
`AGENTIC_KIT_LAN_LOG_LEVEL` follows and can only lower the global log ceiling.

## Verification {#verification}

```bash
cmake -S . -B build -DAGENTIC_KIT_ENABLE_LAN=ON -DPython3_EXECUTABLE=$(which python3)
cmake --build build -j 4
ctest --test-dir build -R '^(tuya_lan_.*|pal_net_test|pal_freertos_net_test|iot_dp_test)$' --output-on-failure
```

The independent encrypted peer covers negotiation, replay, malformed Frames and
resource failures; service tests cover backpressure and lifecycle. A Python peer
uses ephemeral loopback ports through the actual socket service and IoT DP
registry without cloud credentials. Run the full ctest suite serially. Validate
the real App and wireless network on the target hardware.
