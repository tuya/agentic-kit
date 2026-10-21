# LAN is application-driven and shares the IoT DP state

LAN controls need the same schema, values and hardware callbacks as cloud
controls. Routing them through the MQTT downlink dispatcher would silently
mark them clean, while using `iot_dp_set()` alone would omit the control callback.

The LAN module therefore exposes bounded DP/query callbacks, without importing
IoT private headers. Applications use `iot_client_apply_local_dps()` to validate
and apply local controls transactionally, marking them dirty for a later cloud
report. Query and explicit LAN reports use the same DP state document.

Both inputs share length-bounded JSON lexical checks in `common/json_lexical`.
This keeps UTF-8, escape, surrogate and number rules in one implementation.
cJSON still parses JSON structure; the IoT Client owns canonical DP IDs and
duplicate-ID checks, while LAN validates command envelopes and uses the bounded
nesting precheck.

The application pumps LAN and owns all reporting. LAN does not add a second
MQTT owner or change the AI/BLE threading models. All platform capabilities live
in one `pal_t`. Optional listening/datagram callbacks extend that table, and
accepted TCP connections reuse the existing send/receive/poll/close callbacks.
LAN uses timeout zero for nonblocking TCP I/O. The implementations live directly
in `pal_posix.c` and `pal_freertos.c`, using BSD sockets and lwIP respectively.
There is no separate network capability table or socket implementation file.

The optional fields are always present, independent of the build option, so all
consumers must rebuild after this layout change. `pal_is_valid()` continues to
require only the base callbacks; LAN additionally requires the complete group
checked by `pal_has_server_network()`. `tuya_lan_config_t` takes only the shared
`pal`, with no separate network configuration.

LAN is an optional build capability. The host CMake option
`AGENTIC_KIT_ENABLE_LAN` and ESP-IDF `CONFIG_AGENTIC_KIT_ENABLE_LAN` default off,
so existing products do not acquire the service or optional server/UDP code by
default. The built-in PAL factories leave those callbacks NULL when disabled.
The build exports `AGENTIC_KIT_ENABLE_LAN=0/1` for conditional application code.
The general IoT local DP ingress remains available independently of LAN.

P2P signaling may be carried as an authenticated extension. The P2P module owns
negotiation/media; the application connects that module to LAN or MQTT.

## Consequences

- Existing IoT initialization and cloud-report APIs retain their behavior.
- Products explicitly enable LAN when configuring their build; runtime
  start/stop manages the compiled service's listening state.
- Applications must call `tuya_lan_process()` frequently, decide when to report,
  and stop LAN before replacing device credentials.
- A product can run LAN using persisted credentials and schema while MQTT is
  disconnected. Discovery supplies no control authorization by itself.
- Platform ports without listening/datagram support continue to work for the
  existing SDK contexts, and implement the optional `pal_t` group when using LAN.
