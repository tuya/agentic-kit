---
title: Tuya LAN 本地控制
sidebar_label: Tuya LAN
---

# Tuya LAN 本地控制

模块关系、功能边界与数据流见[Tuya LAN 架构与功能说明](../guides/tuya-lan-architecture.md)。
本页说明接口、配置与接入方法。

## 职责 {#overview}

`modules/tuya-lan`（CMake 目标 `tuya_lan`）为已激活设备提供 Tuya LAN 3.5
本地控制。它负责 UDP 发现、TCP 会话、三步鉴权、心跳、DP 查询、DP 下发和显式状态上报。
设备身份与 `local_key` 来自已有激活流程；schema、DP state 和产品业务继续由应用持有。

模块由应用调用 `tuya_lan_process()` 驱动，没有后台线程。LAN 与 MQTT 可独立连接；
持有有效凭据和 schema 后，LAN 控制不需要保持 MQTT 在线。

协议参考 [TuyaOpen 固定版本](https://github.com/tuya/TuyaOpen/tree/68f76c966ef6154c319ce122506d4e0934b2979b/src/tuya_cloud_service/lan)。
本实现只提供 LAN 3.5。旧版 LAN、AP 配网和 P2P 媒体属于其他能力。

## 构建与示例 {#build}

公共头为 `tuya_lan.h` 和统一平台接口 `pal.h`。网络实现分别位于
`pal_posix.c`（BSD sockets）和 `pal_freertos.c`（lwIP）。`pal_t` 末尾新增可选
服务端/UDP 回调，所有使用方须重新编译；自定义移植仅在使用 LAN 时需要实现这组回调。

LAN 默认不参与构建。主机使用 CMake 选项 `AGENTIC_KIT_ENABLE_LAN` 显式开启：

```bash
cmake -S . -B build -DAGENTIC_KIT_ENABLE_LAN=ON -DPython3_EXECUTABLE=$(which python3)
cmake --build build -j 4
```

关闭时重新配置 `cmake -S . -B build -DAGENTIC_KIT_ENABLE_LAN=OFF`。关闭后不构建
`tuya_lan`、LAN 测试、`pal_net_test`、`pal_freertos_net_test` 和 `lan_demo`；
平台 PAL 中的可选服务端/UDP 实现也被裁剪，其回调为 NULL，基础 PAL 仍然可用；
`iot_client_apply_local_dps()` 仍可供其他本地控制通道使用。

独立示例工程也需显式开启：

```bash
cmake -S examples/posix -B build-examples -DAGENTIC_KIT_ENABLE_LAN=ON -DPython3_EXECUTABLE=$(which python3)
cmake --build build-examples --target lan_demo -j 4
./build-examples/lan_demo 192.168.1.20
```

ESP-IDF 在 `idf.py menuconfig` 的 **Agentic Kit** 菜单中开启 LAN，或在应用的
`sdkconfig.defaults` 中设置 `CONFIG_AGENTIC_KIT_ENABLE_LAN=y`；默认关闭。
构建系统向 SDK 使用方导出 `AGENTIC_KIT_ENABLE_LAN=0` 或 `1`，应用可用
`#if AGENTIC_KIT_ENABLE_LAN` 条件编译 LAN 集成代码。该宏由构建选项决定。

运行前，在环境中设置 `TUYA_DEVICE_ID`、`TUYA_LOCAL_KEY`、`TUYA_UUID` 和
`TUYA_PRODUCT_KEY`，使用同一设备保存的激活资料。示例不输出密钥。
`local_key` 必须为 16 字节字符串。

示例展示一个 bool DP（ID 1），使用离线 DP registry，不连接云端；生产应用应复用
已有 IoT client 及真实产品 schema。示例可额外传入 TCP/UDP 端口，端口 0 使用系统
分配的临时端口；默认监听 TCP 6668、UDP 7000，发现公告发送到 UDP 6667。

## 接口与配置 {#api}

- `tuya_lan_config_init()`：设置默认端口、最多 3 个会话、10 秒握手超时、30 秒
  心跳超时和 5 秒发现公告间隔。`discovery_interval_ms = 0` 关闭周期公告。
- `tuya_lan_create()` / `start()` / `stop()` / `destroy()`：分配服务、开始监听、
  关闭连接和释放资源。停止后可以重新启动；凭据改变时销毁并重新创建。
- `tuya_lan_process()`：非阻塞处理有界的接收、发送、发现和超时工作，应用应频繁调用。
- `tuya_lan_report()`：显式向已鉴权会话上报完整 `{"dps":{...}}` 文档。
- `tuya_lan_send()`：向指定会话发送扩展命令回复。
- `tuya_lan_disconnect_all()`：关闭所有会话；端口 getter 可取得实际监听端口。

`start()` / `stop()` 控制已编译 LAN 服务的运行状态；是否编译 LAN 由上述构建选项控制。

`on_dp` 接收裸 DP 对象，例如 `{"1":true}`；返回 0 表示应用接受，非 0 产生错误回复。
`on_query` 向调用方的有界缓冲写入完整 `{"dps":{...}}` 文档，并设置实际长度。
输入与输出长度都不含 NUL 终止符。回调收到的数据只在回调期间有效。

## 复用 IoT DP {#integration}

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

`iot_client_apply_local_dps()` 验证完整非空批次并准备通知快照，全部成功后才更新状态。
错误不修改值或 dirty 标记。成功将本地变更标记 dirty，在锁外先调用持久化回调，
再逐项调用现有 DP 回调。裸 DP ID 必须是无前导零、无重复的十进制 1–255。

LAN 查询和上报不清除 MQTT dirty 标记，也不发起云端上报。应用在 IoT 所属线程
调用 `iot_dp_report_all_dirty()`；每次云端重连后仍需 `iot_dp_report_all()`。
`iot_dp_dump_json()` 按现有规则省略 raw DP；需要 raw 状态通知时由应用显式构造报告。
schema 的 access mode 当前没有强制执行，产品授权策略应在应用接受 DP 前完成。

## 生命周期与平台 {#lifecycle}

启动前调用一次 `iot_init()` 或 `iot_init_default()`，建立共享 PAL/RNG/cJSON 分配器。
配置字符串必须活到服务销毁。设备解绑、复位、换密钥或释放 IoT client 前先停止 LAN，
然后用新凭据和当前 IPv4 重新创建服务。

所有 LAN 接口和回调使用同一个应用线程；回调可发送回复或报告，销毁、停止、断开全部
连接和递归 `process()` 要等回调返回后执行。与 MQTT 共用线程时也要保证 LAN 获得
足够调用频率；`iot_client_process()` 的阻塞预算由编译期配置决定。

移植通过同一份 `pal_t` 提供可选的监听、接受连接和 UDP 操作。
`tuya_lan_create()` 使用 `pal_has_server_network()` 检查完整能力组；基础
`pal_is_valid()` 仍只检查原有必填回调。接受的 TCP 连接复用已有
`tcp_send/recv/poll/close`，LAN 收发传入 `timeout_ms=0`，不会等待网络数据。
配置只需填写 `config.pal`，并复用 SDK 初始化时的共享 PAL。
模块处理部分发送和 would-block；每会话缓冲和每次 pump 工作有界。
不要在模块源文件直接调用平台 socket。

## 协议边界 {#limits}

LAN 3.5 使用 AES-GCM 帧与 HMAC 随机数握手。鉴权前不接受 DP、心跳或扩展命令；
坏标签、重放、异常长度和超时关闭对应会话。发现密钥是公开协议常量，不能授权控制。
发现回复校验请求内 IP 与实际来源一致，并回复实际来源地址和端口。

LAN 与本地 DP 输入共用有界 JSON 字符串和数字词法校验，拒绝无效 UTF-8、
错误转义、不成对代理项及解码后的 NUL；JSON 结构仍由 cJSON 校验。

`on_command` 可接收已鉴权的扩展数据，包括 `0x20` P2P 信令；P2P 协商和媒体传输
仍由另外的模块实现。AI session token 不用于 LAN 会话。

`AGENTIC_KIT_LAN_MAX_FRAME_SIZE` 默认 4096（允许 128–65536）。
`AGENTIC_KIT_LAN_LOG_LEVEL` 默认跟随全局日志上限，且只能降低日志级别。

## 验证 {#verification}

```bash
cmake -S . -B build -DAGENTIC_KIT_ENABLE_LAN=ON -DPython3_EXECUTABLE=$(which python3)
cmake --build build -j 4
ctest --test-dir build -R '^(tuya_lan_.*|pal_net_test|pal_freertos_net_test|iot_dp_test)$' --output-on-failure
```

协议测试使用独立加密端验证握手、重放、错误帧及资源失败；服务测试验证背压和生命周期；
Python peer 使用临时回环端口贯穿真实网络服务和 IoT DP registry，无需云凭据。
完整 ctest 保持串行。真实 App 联调和硬件无线网络验证应在目标设备上完成。
