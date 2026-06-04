# DP layer never initiates uplink; the application drives reporting and the connection lifecycle

The IoT client does no background work: it never auto-reconnects MQTT, and the DP
layer never publishes on its own (no report-on-connect, and no cloud-query-triggered
report — there is no device-query protocol). The application owns the connect/reconnect
loop (`iot_client_message_connect` + `iot_client_process`) and all DP reporting; after
each successful (re)connect it must call `iot_dp_report_all()` to refresh the cloud's
cached DP state, and it reports changes via `iot_dp_report()` / `iot_dp_report_all_dirty()`.

We chose this for a single-threaded, deterministic, embedded-friendly model where every
uplink and every socket operation happens at a time the application controls. The cost:
the app must remember to report after a (re)connect, otherwise the cloud (and therefore
the phone app) shows stale state. Restored values (`dp_state` / `iot_dp_restore_json`)
likewise only seed the local cache and are never auto-reported.

## Consequences

- No connection-state callback and no auto-reconnect: a dropped link surfaces as an error
  return from `iot_client_process`; the app reconnects and then re-reports.
- The cloud learns device state only from device-initiated reports, so "report on connect"
  is mandatory app behaviour, documented in the examples.
