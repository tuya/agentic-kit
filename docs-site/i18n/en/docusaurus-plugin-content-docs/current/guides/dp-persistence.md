---
title: DP State Persistence
sidebar_label: DP State Persistence
sidebar_position: 4
---

# DP State Persistence

The current state and Schema of DPs (Data Points) must be retained across restarts, but **persistence itself is not an SDK capability** -- the SDK does not read or write files, flash, or NVS. The SDK only provides a pair of symmetric mechanisms: handing serializable state to the application and accepting state restored by the application at startup. **When to save, where to save, and how to save are entirely up to the application.**

This guide explains how to save and restore DP state correctly, along with embedded-system considerations for the save callback.

## Two Types of Data to Persist {#需要持久化的两类数据}

| Data | Source | When it changes |
|---|---|---|
| `schema_id` + `schema` | Activation response (initially), or the Schema upgrade callback | Rarely (only when the product Schema is upgraded) |
| Current DP state (`{"dps":{...}}`) | A set or Downlink while the device is running | Frequently |

## Saving: Two Channels {#保存两种通道}

### Channel 1 (Recommended): Save Callback Pushed on Every Change {#通道一推荐变化即推送的保存回调}

After `iot_dp_set_save_callback()` is registered, **any operation that changes a DP value** (a local `iot_dp_set` / `iot_dp_report`, or a cloud Downlink) triggers one callback outside the lock, carrying the current complete snapshot `{"dps":{...}}`:

```c
static void on_dp_save(const char *dp_state_json, void *user_data)
{
    // Write the snapshot to storage, organized by devid
    storage_write("dp_state", dp_state_json);
}

iot_dp_set_save_callback(client, on_dp_save, NULL);
```

### Channel 2: Pull On Demand {#通道二按需主动拉取}

```c
char *json = NULL;
if (iot_dp_dump_json(client, &json) == OPRT_OK && json) {
    storage_write("dp_state", json);
    client->pal->free(json);   // Must be freed through the PAL
}
```

`iot_dp_dump_json` and `iot_dp_restore_json` produce and consume the same `{"dps":{...}}` format, allowing round-trip restoration. **Note: the snapshot from `iot_dp_dump_json` does not include RAW DPs** (see the RAW omission note in `iot_dp.h`), so a dump-to-restore cycle loses runtime RAW values. If RAW values must be retained, save them separately in the application; the save-callback snapshot also omits RAW. Alternatively, do not rely on this round trip for RAW values.

## Embedded-System Considerations for the Save Callback {#保存回调在嵌入式上的注意事项}

> These points directly affect flash lifetime and stability. Be sure to follow them.

- **Each callback contains a full snapshot, so it is safe to discard intermediate callbacks.** The callback carries the current **complete** DP state, not a delta. In scenarios with frequent changes (such as a sensor calling `iot_dp_set` often), the application should **debounce or rate-limit** writes to flash. For example, cache the latest snapshot and persist it periodically, when idle, or before sleep. Skipping intermediate snapshots does not lose data because the last snapshot always contains the latest complete state.

  ```c
  // Debouncing example: only cache in the callback; do not write flash immediately
  static char g_pending[1024];
  static bool g_dirty;
  static void on_dp_save(const char *json, void *u) {
      snprintf(g_pending, sizeof(g_pending), "%s", json);   // Copy only
      g_dirty = true;
  }
  // Rate-limit persistence in the main loop
  if (g_dirty && elapsed_since_last_write() > 5000) {
      storage_write("dp_state", g_pending);
      g_dirty = false;
  }
  ```

- **Do not modify DP state inside the save callback.** Calling `iot_dp_set` / `iot_dp_report` in the callback triggers another save callback (recursive write-back). Either read without writing or only cache the snapshot.

- **`dp_state_json` is valid only for the duration of the callback.** The SDK owns it. To retain it, you must **write or copy it synchronously** inside the callback; you must not store the pointer for asynchronous use.

- **Which thread triggers the callback?** A local `set`/`report` triggers it on your application thread; a cloud Downlink triggers it on the thread running `iot_client_process()`. If your storage API is not thread-safe, either persist from a single loop or provide your own locking.

## Restoring: Supply Persisted State at Startup {#恢复启动时回灌}

After a restart, populate `iot_client_config_t` with the three previously persisted values. `iot_client_init()` automatically rebuilds the DP registry from `schema` and restores each DP's current value from `dp_state` (**without marking it dirty or reporting it**):

```c
iot_client_config_t cfg = {0};
// devid / secret_key / local_key / region / env ...
cfg.schema_id = saved_schema_id;   // Previously persisted
cfg.schema    = saved_schema;
cfg.dp_state  = saved_dp_state;    // {"dps":{...}}
iot_client_t *client = iot_client_init(&cfg);
```

You can also call `iot_dp_restore_json(client, json)` explicitly at any time while the application is running to achieve the same result.

## Reporting Is the Application's Responsibility {#上报由应用负责}

Restored values only populate the local cache; **the SDK does not report them automatically**. After every successful connection or reconnection, the application should call `iot_dp_report_all()` once to refresh the cloud cache. Otherwise, the App sees stale state. See the [iot-client reference](../reference/iot-client.md) for details.

## Persistence During a Schema Upgrade {#schema-升级时的持久化}

The application periodically calls `iot_dp_schema_check_update()` to poll for the latest Schema. If an upgrade is available, the SDK **retains the current values of DPs that still exist and gives newly added DPs their default values**, then invokes `iot_schema_update_callback_t`. In that callback:

1. Overwrite the persisted `schema` with the new one (`schema_id` is unchanged and does not need to be rewritten).
2. It is recommended to call `iot_dp_report_all()` afterward to report all DP values.
