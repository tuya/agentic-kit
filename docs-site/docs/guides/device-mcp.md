---
title: 开发设备 MCP 功能
sidebar_label: 设备 MCP
sidebar_position: 5
---

# 开发设备 MCP 功能

本指南说明如何在设备端实现 MCP（Model Context Protocol）功能，使云端 AI 能够主动调用设备上的工具（如查询传感器、控制外设等）。

完整示例代码见 `examples/posix/ai/rtc-tcp-client/mcp_demo.c`。

## 工作原理

设备作为 MCP **Server**，云端 AI 作为 MCP **Client**。交互流程：

```
用户发送文本/语音 ──> 云端 AI（LLM）
                         │
         LLM 判断需要调用设备工具
                         │
                         v
设备收到 TAI_EVT_MCP_CMD  <── JSON-RPC 2.0 请求
       │
       v
设备解析请求、执行工具、返回结果
       │
       v
tai_send_mcp_response() ──> 云端 AI 继续生成回复
```

云端会依次发送三种 MCP 请求：

| 方法 | 用途 |
|------|------|
| `initialize` | 握手，获取设备 MCP 能力声明 |
| `tools/list` | 获取设备暴露的工具列表 |
| `tools/call` | 调用某个具体工具并获取结果 |

## 前置条件

- 设备已完成 IoT SDK 初始化和 TAI 连接（参考[快速开始](../tutorials/quick-start)）
- 连接时声明设备支持 MCP

## 步骤

### 1. 声明设备支持 MCP

在 `tai_config_t` 的 `session_attrs_json` 中设置 `supportCustomMCP` 为 `true`：

```c
static const char SESSION_ATTRS[] =
    "{\"deviceMcp\":{\"supportCustomMCP\":true}}";

tai_config_t cfg = {
    // ... 其他字段
    .session_attrs_json = SESSION_ATTRS,
};
```

### 2. 定义工具注册表

每个工具需要四个要素：名称、描述、JSON Schema 格式的输入参数定义、C 处理函数。

```c
typedef int (*tool_fn_t)(const char *args_json, char *out, size_t out_cap);

typedef struct {
    const char *name;
    const char *description;
    const char *input_schema_json;
    tool_fn_t   fn;
} mcp_tool_t;
```

示例——注册两个工具：

```c
static int tool_get_device_status(const char *args_json,
                                  char *out, size_t out_cap)
{
    (void)args_json;
    return snprintf(out, out_cap,
        "{\"online\":true,\"battery\":87,\"volume\":40}");
}

static int tool_control_device(const char *args_json,
                               char *out, size_t out_cap)
{
    // 从 args_json 中解析 "action" 和 "target" 字段
    // 执行对应硬件操作
    // 返回结果 JSON
    return snprintf(out, out_cap, "{\"ok\":true}");
}

static const mcp_tool_t k_tools[] = {
    {
        .name = "get_device_status",
        .description = "Return device state: online, battery, volume.",
        .input_schema_json =
            "{\"type\":\"object\",\"properties\":{},\"required\":[]}",
        .fn = tool_get_device_status,
    },
    {
        .name = "control_device",
        .description = "Send on/off control to a named subsystem.",
        .input_schema_json =
            "{\"type\":\"object\","
            "\"properties\":{"
                "\"action\":{\"type\":\"string\",\"enum\":[\"on\",\"off\"]},"
                "\"target\":{\"type\":\"string\"}"
            "},"
            "\"required\":[\"action\",\"target\"]}",
        .fn = tool_control_device,
    },
};
#define K_TOOLS_COUNT (sizeof(k_tools) / sizeof(k_tools[0]))
```

`input_schema_json` 遵循 [JSON Schema](https://json-schema.org/) 格式，云端 AI 会据此生成合法的调用参数。

### 3. 处理 MCP 事件

在 `on_event` 回调中监听 `TAI_EVT_MCP_CMD`，将请求分发给处理函数：

```c
static void on_event(tai_ctx_t *ctx, uint16_t event_type,
                     const uint8_t *data, size_t len, void *ud)
{
    if (event_type == TAI_EVT_MCP_CMD) {
        handle_mcp_request(ctx, (const char *)data, len);
    }
}
```

### 4. 实现 MCP 请求分发器

收到的 `data` 是 JSON-RPC 2.0 格式的请求。需要解析 `method` 和 `id` 字段，根据 `method` 构造对应的响应：

```c
static void handle_mcp_request(tai_ctx_t *ctx,
                               const char *payload, size_t len)
{
    // 解析 method 和 id
    char id[64] = "null";
    char method[64] = {0};
    copy_id(payload, id, sizeof(id));
    json_get_string(payload, "method", method, sizeof(method));

    char resp[2048];
    int  resp_len = 0;

    if (strcmp(method, "initialize") == 0) {
        resp_len = build_initialize_response(id, resp, sizeof(resp));
    } else if (strcmp(method, "tools/list") == 0) {
        resp_len = build_tools_list_response(id, resp, sizeof(resp));
    } else if (strcmp(method, "tools/call") == 0) {
        // 解析 params.name 和 params.arguments
        // 查找并调用对应工具
        resp_len = build_tools_call_response(id, name, args,
                                             resp, sizeof(resp));
    } else {
        resp_len = build_error_response(id, -32601, "Method not found",
                                        resp, sizeof(resp));
    }

    // 发送响应
    if (resp_len > 0) {
        tai_send_mcp_response(ctx, resp);
    }
}
```

### 5. 构造 JSON-RPC 响应

每种方法需要返回特定格式的 JSON-RPC 2.0 响应。`id` 字段必须与请求中的 `id` 一致。

**initialize 响应：**

```json
{
  "jsonrpc": "2.0",
  "id": 1,
  "result": {
    "protocolVersion": "2024-11-05",
    "serverInfo": { "name": "my-device", "version": "1.0.0" },
    "capabilities": { "tools": {} }
  }
}
```

**tools/list 响应：**

```json
{
  "jsonrpc": "2.0",
  "id": 2,
  "result": {
    "tools": [
      {
        "name": "get_device_status",
        "description": "Return device state: online, battery, volume.",
        "inputSchema": { "type": "object", "properties": {}, "required": [] }
      }
    ]
  }
}
```

**tools/call 响应：**

```json
{
  "jsonrpc": "2.0",
  "id": 3,
  "result": {
    "content": [{ "type": "text", "text": "{\"online\":true,\"battery\":87}" }],
    "isError": false
  }
}
```

工具执行失败时，将 `isError` 设为 `true`，`text` 中填写错误信息。

**错误响应（未知方法）：**

```json
{
  "jsonrpc": "2.0",
  "id": 4,
  "error": { "code": -32601, "message": "Method not found" }
}
```

### 6. 发送响应

使用 `tai_send_mcp_response()` 将构造好的 JSON 字符串发回云端：

```c
int rc = tai_send_mcp_response(ctx, resp);
if (rc != TAI_OK) {
    // 处理发送失败
}
```

## 编译与运行

```bash
cmake -S examples/posix -B build -DAGENTIC_KIT_BUILD_EXAMPLES=ON
cmake --build build --target tai_mcp_demo
./build/tai_mcp_demo [devid] [secret_key] [local_key]
```

## 添加自定义工具

要添加新工具，只需三步：

1. **编写处理函数** — 解析 `args_json`，执行业务逻辑，将结果写入 `out`：

```c
static int tool_read_sensor(const char *args_json,
                            char *out, size_t out_cap)
{
    float temp = read_temperature_sensor();
    return snprintf(out, out_cap,
        "{\"temperature\":%.1f,\"unit\":\"celsius\"}", temp);
}
```

2. **在 `k_tools` 数组中注册**：

```c
{
    .name = "read_sensor",
    .description = "Read the temperature sensor value in celsius.",
    .input_schema_json =
        "{\"type\":\"object\",\"properties\":{},\"required\":[]}",
    .fn = tool_read_sensor,
},
```

3. **重新编译** — 无需修改分发逻辑，`tools/list` 和 `tools/call` 会自动包含新工具。

## 注意事项

- 所有回调（包括 `TAI_EVT_MCP_CMD`）在后台接收线程中执行，工具函数应避免长时间阻塞
- 响应的 `id` 必须与请求的 `id` 完全一致，否则云端无法匹配
- `tai_send_mcp_response()` 的参数是完整的 JSON-RPC 2.0 响应字符串（非 `result` 部分）
- 工具输出中的双引号和反斜杠需要转义
- 响应缓冲区大小需根据工具输出长度合理设置，避免截断
- `input_schema_json` 中描述的字段越精确，AI 生成的调用参数质量越高
