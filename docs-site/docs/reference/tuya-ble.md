---
title: Tuya BLE 配网与本地控制
sidebar_label: Tuya BLE
---

# Tuya BLE 配网与本地控制

## 职责 {#overview}

`modules/tuya-ble`（CMake 目标 `tuya_ble`）提供两种设备到 App 的 BLE 模式：
未激活设备通过 Pairing 接收 WiFi 凭据；已激活设备使用保存的激活凭据进行本地 DP 控制。
两种模式复用广播、加密帧和 Trsmitr 分段传输，由初始化配置选择。

BLE 不持有产品 schema 或 DP state，也不创建线程。所有接收、发送就绪、定时 tick、
DP 回调和上报都运行在 port 的 BLE 所属上下文。应用负责把本地控制接入已有 IoT DP
状态与硬件业务；LAN、MQTT 和 BLE 可以分别连接与上报。

协议参考 [TuyaOpen 固定版本](https://github.com/tuya/TuyaOpen/tree/68f76c966ef6154c319ce122506d4e0934b2979b/src/tuya_cloud_service/ble)。
WiFi 配网接入步骤见 [BLE 配网教程](../tutorials/pair-by-ble.md)。

## 配置与模式 {#configuration}

公共头 `tuya_ble_prov.h` 的 `tuya_ble_prov_cfg_ext_t` 继续保留已有配网配置，增加：

- `local_key`、`secret_key`：同一设备激活后保存的凭据，均为恰好 16 字节、NUL
  结尾的字符串。两者同时为空时保持配网；两者同时提供时开启已绑定控制；缺少一个
  或长度不符会使初始化失败。字符串由调用方持有，必须覆盖 BLE state 生命周期。
- `dp_write(tlv, len, dp_ctx)`：收到校验通过的 DP TLV 批次。
- `dp_query(ids, count, dp_ctx)`：收到校验通过的 DPID 列表；空列表表示查询全部。
- `dp_ctx`：两个 DP 回调共用的应用上下文。

DP 回调中的缓冲只在本次调用有效。返回 0 表示已复制并接受工作，其他返回值使 App
收到失败 ACK。回调不能阻塞、重入该 BLE state 或调用 MQTT；队列已满应返回失败。
ACK 表示请求已被接受，后续 schema 校验和硬件操作的结果由应用另行处理。

这是运行时配置，不依赖 `AGENTIC_KIT_ENABLE_LAN`；关闭 LAN 编译选项仍可使用 BLE
本地控制。使用方需重新编译以匹配更新后的公共配置和 state 布局。

## 鉴权与协议 {#authentication}

配网仍使用 KEY_11 / KEY_12。已绑定控制使用 KEY_14 / KEY_15：

```text
KEY_14 = MD5(local_key || secret_key)
KEY_15 = MD5(local_key || secret_key || pair_rand)
```

`pair_rand` 是本次设备信息交换产生的 6 字节设备随机数。已绑定广播和设备信息携带
对应的绑定标记；成功 Pairing 返回结果 2。只有成功 Pairing 后的 KEY_15 帧可以执行
DP 写入 `0x0027` 和查询 `0x0003`。旧配网密钥不能授权已绑定控制，配网模式也不接收
DP 控制。已绑定控制模式关闭 WiFi 凭据与 big-data 配网命令。

帧继续检查长度、CRC、模式、命令授权和递增非零 SN。`set_paired(true)` 兼容接口
不能授予加密授权。该能力不自动执行解绑、复位或完整 PSK3.0 激活流程。

## DP TLV 与转换 {#dp-format}

公共头 `tuya_ble_dp.h` 提供 `tuya_ble_dp_validate()` 和 `tuya_ble_dp_to_json()`。
一个批次由若干 `[DPID:1][type:1][length:2][value:length]` 组成，长度采用大端。
DPID 必须非零且批次内不重复；转换与回调前均校验整个批次。

- raw（0）：任意字节，转 JSON 时使用 Base64 字符串。
- bool（1）：单字节 0 或 1，转 JSON boolean。
- value（2）：4 字节大端有符号整数，转 JSON number。
- string（3）：有效 UTF-8，转 JSON 字符串并转义特殊字符。
- enum（4）和 bitmap（5）：1、2 或 4 字节大端无符号整数，转 JSON number。

例如 DP 1 的 bool `true` 编码为 `01 01 00 01 01`，转换结果为裸 DP JSON
`{"1":true}`。`tuya_ble_dp_to_json(tlv, len, out, capacity, &written)` 写入调用方的
有界缓冲，并返回实际 JSON 长度；传给 IoT 输入的长度不包含 NUL 终止符。

BLE 校验的是线上的表示，schema 类型、取值范围和产品授权策略仍由应用判断。
现有 IoT registry 不支持 bitmap 类型；产品可在自己的 BLE 回调中处理 bitmap TLV，
无需把它送入该 registry。

## 复用 IoT DP {#integration}

应用按以下顺序衔接两个所属上下文：

1. BLE 回调复制完整 TLV 或查询 ID，并记录当前 `connection_generation` 后入队。
2. IoT 所属线程取出写请求，用 `tuya_ble_dp_to_json()` 转换，再调用
   `iot_client_apply_local_dps(client, json, len)`；查询读取同一产品 DP state。
3. 应用根据实际结果构造状态 TLV，将报告和原 generation 复制回 BLE 队列。
4. BLE 所属上下文丢弃 generation 已过期的结果，对当前连接显式调用报告接口。

这段流程描述应用需要提供的队列，不是 SDK 内置 worker。跨任务不要直接读取 BLE
state；通过队列传递 generation，或在各所属上下文维护应用自己的有效性标记。
端口停止、重新初始化或凭据替换时，还应取消或清空旧请求与报告。

`iot_client_apply_local_dps()` 对完整批次进行 schema 校验，准备成功后才更新状态，
标记 dirty 并调用已有持久化与 DP 回调；它不发布 MQTT。云端上报由应用在 IoT
所属线程显式调用 `iot_dp_report_all_dirty()`，云端重连后仍需 `iot_dp_report_all()`。
查询报告所需的 TLV 由应用按产品类型构造；没有自动 JSON 转 TLV 或自动查询回复。

## 显式状态上报 {#reporting}

`tuya_ble_dp_report(state, tlv, len, query_response)` 发送 `0x8006`；
`tuya_ble_dp_report_with_time(state, tlv, len, utc_seconds, query_response)` 发送
`0x8007`，时间由应用提供 Unix UTC 秒。两者都要求当前已绑定连接完成 Pairing，并
使用同样的 TLV 校验与有界 TX 队列。

`query_response = true` 表示查询结果，`false` 表示主动状态通知。写入 ACK、查询 ACK
和实际状态报告是不同步骤；模块不会在写入或查询后自行生成报告。报告成功入队也不
证明手机已经收到，应用需处理返回错误和端口背压。BLE 报告不发布 MQTT，也不清除
MQTT dirty 标记。

## 生命周期与移植 {#lifecycle}

使用 JSON 配网能力前调用一次 `iot_init()` 或 `iot_init_default()`，绑定共享 PAL、
RNG 和 cJSON 分配器。端口提供完整填充的 CSPRNG，调用 `tuya_ble_prov_on_data()`、
`tuya_ble_prov_tx_ready()` 和单调时钟驱动的 `tuya_ble_prov_tick()`。

GATT 断开或 BLE host 复位时调用 `tuya_ble_prov_close()`；它擦除会话数据、密钥与
待发队列，并保留借用配置以支持重连。历史 `reset_conn()` 保留 Pairing 和密钥，
不能替代 close。close 与接受新的设备信息查询都会改变 `connection_generation`，
因此旧 Pairing 产生的异步结果不能用于新连接或新握手。

激活、解绑、复位或换密钥时，先停止 port，清理旧队列，再用当前激活凭据重新初始化。
不要在运行中修改借用的凭据字符串。配网完成并不自动切换为已绑定控制。

Trsmitr 在默认 20 字节 GATT 预算下也支持分段；port 可通过
`tuya_ble_prov_set_gatt_payload()` 更新实际预算。发送回调必须复制缓冲后返回：
0 表示接受、1 表示忙、其他值表示失败。port 继续通过 TX-ready/tick 重试；超时会
关闭会话。SDK 无后台线程，现有 NimBLE 配网示例由 port 自己管理 WiFi 扫描任务。

## 验证与边界 {#verification}

```bash
cmake --build build --target tuya_ble_test -j 4
ctest --test-dir build -R '^tuya_ble_test$' --output-on-failure
```

回归测试使用独立加密端验证配网及本地控制帧，无需云凭据；完整 ctest 仍保持串行。
目标设备的无线通信、真实 Tuya App 和产品面板需另行联调。
