---
title: 调用未封装的云端接口（ATOP 通用入口）
sidebar_label: ATOP 通用调用
sidebar_position: 8
---

# 调用未封装的云端接口

设备与涂鸦云之间的 HTTP 接口统称 **ATOP 接口**，由 `api` 名和 `version` 两个字段唯一确定，例如 `tuya.device.upgrade.get` v4.4。

SDK 为其中一部分接口提供了**具名接口**（`iot_ota_*`、`iot_dp_*` 等），返回类型化的结构体。云端可调的 ATOP 接口远多于此，为了不让业务被 SDK 的排期卡住，SDK 提供一个**通用入口** `iot_atop_call()`：你给出 `api`、`version` 和 JSON 请求体，拿回 `result` 字段的 JSON 字符串。

签名、请求体 AES-GCM 加密、TLS、host 解析、信封解析都在 SDK 内部完成，**设备密钥不会离开 SDK**。

## 先确认有没有具名接口

已经封装好的接口不要用通用入口重新实现——它们额外处理了云端返回的各种变体，并且有单测覆盖：

| ATOP api | version | 具名接口 |
| --- | --- | --- |
| `tuya.device.upgrade.get` | 4.4 | `iot_ota_check_upgrade()` |
| `tuya.device.versions.update` | 4.1 | `iot_ota_report_version()` |
| `tuya.device.upgrade.status.update` | 4.1 | `iot_ota_report_status()` |
| `tuya.device.schema.newest.get` | 1.0 | DP 层内部自动查询 |
| `thing.ai.agent.token.get` | 1.0 | `iot_client_get_session_token()` |
| `tuya.device.qrcode.info.get` | 1.1 | `iot_get_qrcode_info()` |
| `thing.device.opensdk.active` | 2.0 | `iot_client_init_on_boarding()` |
| `tuya.device.meta.save` | 1.0 | 激活流程内部调用 |

## 什么时候该要一个具名接口

通用入口是逃生通道，不是首选。满足下面**任意一条**时，这个接口值得进 SDK 变成具名接口，欢迎提 issue：

- **多产品复用** —— 两个以上产品线都要调。否则等于把单个产品的业务逻辑塞进 SDK。
- **协议语义不平凡** —— 有状态机、多步时序，或者云端返回形态需要宽容解析。这类知识重复实现必然出偏差。
- **需要参与 SDK 内部状态** —— 要改 `iot_client_t` 的字段或触发 SDK 回调。业务层拿不到内部状态，物理上只能在 SDK 里做。

三条都不满足的接口，留在业务层用通用入口调是合适的终态，不是技术债。

## API

```c
#include "iot_atop.h"

typedef struct {
    const char *api;      /* 例 "tuya.device.upgrade.get"，必填 */
    const char *version;  /* 例 "4.4"，必填 */
    const char *data;     /* 请求体，JSON 对象字符串；NULL 或 "" 视为 "{}" */
} iot_atop_request_t;

typedef struct {
    char *result;            /* result 字段的 JSON 字符串；云端没返回则为 NULL */
    char  error_code[48];    /* 云端 errorCode；成功时为 "" */
    char  error_msg[128];    /* 云端 errorMsg；成功时为 "" */
    int32_t server_time;     /* 信封里的服务器时间 t */
} iot_atop_response_t;

int  iot_atop_call(iot_client_t *client,
                   const iot_atop_request_t *request,
                   iot_atop_response_t *response);
void iot_atop_response_free(iot_client_t *client, iot_atop_response_t *response);
```

## 示例

```c
#include "iot_atop.h"

char body[192];
snprintf(body, sizeof(body),
         "{\"schemaId\":\"%s\",\"version\":\"\",\"t\":%u}",
         schema_id, (unsigned)time(NULL));

iot_atop_request_t  req  = { .api     = "tuya.device.schema.newest.get",
                             .version = "1.0",
                             .data    = body };
iot_atop_response_t resp = {0};

int rc = iot_atop_call(client, &req, &resp);
if (rc == OPRT_OK) {
    if (resp.result != NULL) {
        my_parse(resp.result);          /* result 是 JSON 文本，用什么库解析都行 */
    }
} else if (rc == OPRT_ATOP_BUSINESS_ERROR) {
    /* 请求到达了云端，被云端拒绝 —— error_code 说明原因 */
    log_error("rejected: %s (%s)", resp.error_code, resp.error_msg);
} else {
    /* 传输层失败：DNS / TLS / HTTP / 解密 */
    log_error("call failed: %d", rc);
}

iot_atop_response_free(client, &resp);   /* 每条路径都要调，包括失败路径 */
```

## 返回值：区分"云端拒绝"和"没连上"

这是使用通用入口时最重要的一点。你调的接口 SDK 并不认识，所以 SDK 无法替你判断业务是否成功，**云端自己的 errorCode 是唯一可靠的线索**：

| 返回值 | 含义 | 怎么处理 |
| --- | --- | --- |
| `OPRT_OK` | 云端接受了这次调用 | `result` 是结果 JSON，或 NULL（云端没返回 result，这是合法的） |
| `OPRT_ATOP_BUSINESS_ERROR` | 到达了云端，被云端拒绝 | 看 `error_code` / `error_msg`，按你的接口文档处理 |
| `OPRT_INVALID_PARAMETER` | 参数不对，或 `data` 不是 JSON 对象 | 本地就拦下了，没发网络请求 |
| `OPRT_UNINITIALIZED` | 设备还没有激活凭据 | 先完成激活 |
| 其他 | 传输层失败（DNS / TLS / HTTP / 解密） | 可重试 |

`error_code[0] == '\0'` 等价于"业务成功"。

## 限制

**只支持已激活的设备。** 通用入口用 `devid` + `secret_key` 签名。激活本身用的是 `uuid` + `authkey`，是另一条路径，仍然只能通过 `iot_client_init_on_boarding()` 走。在没有凭据的 client 上调用返回 `OPRT_UNINITIALIZED`。

**请求体原样透传。** SDK 不会改写你的请求体，所以接口要求的字段必须自己带齐——**包括大多数 ATOP 接口在请求体里要求的 `t` 时间戳字段**。SDK 只校验到"能解析成 JSON 对象"为止，目的是把手误变成一个立刻返回的 `OPRT_INVALID_PARAMETER`，而不是花一次 HTTPS 往返换一句含义模糊的云端拒绝。

**`result` 是字符串，不是 cJSON 对象。** 这样 cJSON 不会进入 SDK 的公共 ABI，业务层不必绑定 SDK 的 cJSON 版本，内存归属也清晰：`iot_atop_response_free()` 负责释放。

**设备时钟要基本准确。** 签名带时间戳，设备时间偏差过大云端会拒签。`resp.server_time` 是云端返回的时间（秒级 Unix 时间戳），可以用来校正本地时钟。
