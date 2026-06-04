---
title: DP 状态持久化
sidebar_label: DP 状态持久化
sidebar_position: 4
---

# DP 状态持久化

DP（Data Point，数据点）的当前状态和 schema 需要跨重启保留,但**持久化本身不是 SDK 的能力** —— SDK 不读写文件 / flash / NVS。SDK 只提供一对对称的机制:把可序列化的状态交给应用,以及在启动时接收应用回灌的状态。**何时存、存到哪、怎么存,完全由应用决定。**

本指南说明如何正确地保存与恢复 DP 状态,以及保存回调在嵌入式上的注意事项。

## 需要持久化的两类数据

| 数据 | 来源 | 何时变化 |
|---|---|---|
| `schema_id` + `schema` | 激活响应(首次),或 schema 升级回调 | 很少(仅产品 schema 升版) |
| DP 当前状态(`{"dps":{...}}`) | 设备运行中 set / 下发 | 频繁 |

## 保存:两种通道

### 通道一(推荐):变化即推送的保存回调

注册 `iot_dp_set_save_callback()` 后,**任何改变 DP 值的操作**(本地 `iot_dp_set` / `iot_dp_report`,或云端下发)在锁外触发一次回调,携带当前完整快照 `{"dps":{...}}`:

```c
static void on_dp_save(const char *dp_state_json, void *user_data)
{
    // 把快照写入存储,按 devid 归档
    storage_write("dp_state", dp_state_json);
}

iot_dp_set_save_callback(client, on_dp_save, NULL);
```

### 通道二:按需主动拉取

```c
char *json = NULL;
if (iot_dp_dump_json(client, &json) == OPRT_OK && json) {
    storage_write("dp_state", json);
    client->pal->free(json);   // 必须用 PAL 释放
}
```

`iot_dp_dump_json` 与 `iot_dp_restore_json` 产出 / 消费同一种 `{"dps":{...}}` 格式,可无损往返。

## 保存回调在嵌入式上的注意事项

> 这几条直接决定 flash 寿命与稳定性,务必遵守。

- **每次回调都是全量快照,丢弃中间次是安全的。** 回调携带的是当前**完整** DP 状态,不是增量。高频变化场景(如传感器频繁 `iot_dp_set`)下,应用应当**去抖 / 降频**写 flash —— 例如缓存最新一份快照,定时(或在空闲/休眠前)落盘一次。跳过中间快照不会丢数据,因为最后一次必然包含最新完整状态。

  ```c
  // 去抖示例:回调里只缓存,不立即写 flash
  static char g_pending[1024];
  static bool g_dirty;
  static void on_dp_save(const char *json, void *u) {
      snprintf(g_pending, sizeof(g_pending), "%s", json);   // 仅拷贝
      g_dirty = true;
  }
  // 主循环里限频落盘
  if (g_dirty && elapsed_since_last_write() > 5000) {
      storage_write("dp_state", g_pending);
      g_dirty = false;
  }
  ```

- **不要在保存回调里改动 DP 状态。** 回调里调用 `iot_dp_set` / `iot_dp_report` 会触发再一次保存回调(递归回写)。要么只读不写,要么只缓存快照。

- **`dp_state_json` 仅在回调期间有效。** 它由 SDK 拥有;要保留必须在回调内**同步写入或拷贝**,不能把指针存下来异步使用。

- **回调在哪个线程触发?** 本地 `set`/`report` 在你的应用线程触发;云端下发在 `iot_client_process()` 所在线程触发。若你的存储 API 非线程安全,要么统一在单 loop 里落盘,要么自行加锁。

## 恢复:启动时回灌

重启后,把上次持久化的三项填进 `iot_client_config_t`,`iot_client_init()` 会自动按 `schema` 重建 DP registry,并用 `dp_state` 恢复各 DP 当前值(**不置 dirty、不上报**):

```c
iot_client_config_t cfg = {0};
// devid / secret_key / local_key / region / env ...
cfg.schema_id = saved_schema_id;   // 之前持久化的
cfg.schema    = saved_schema;
cfg.dp_state  = saved_dp_state;    // {"dps":{...}}
iot_client_t *client = iot_client_init(&cfg);
```

运行期也可随时显式调 `iot_dp_restore_json(client, json)` 达到同样效果。

## 上报由应用负责

恢复的值只填本地缓存,**SDK 不会自动上报**。请在每次(重)连成功后由应用调一次 `iot_dp_report_all()` 刷新云端缓存,否则 App 端看到的是旧状态。详见 [iot-client 参考](../reference/iot-client.md)。

## schema 升级时的持久化

应用周期调用 `iot_dp_schema_check_update()` 轮询最新 schema。若有更新,SDK 会**保留仍存在的 DP 的当前值、给新增 DP 填默认值**,然后触发 `iot_schema_update_callback_t`。在该回调里:

1. 把新的 `schema_id` / `schema` 覆盖持久化;
2. 建议随后调一次 `iot_dp_report_all()` 做一次全量同步。
