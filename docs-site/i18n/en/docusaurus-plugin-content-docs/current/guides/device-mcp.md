---
title: Develop Device MCP Features
sidebar_label: Device MCP
sidebar_position: 5
---

# Develop Device MCP Features

This guide explains how to implement MCP (Model Context Protocol) features on a device so that the cloud AI can proactively invoke tools on the device, such as querying sensors or controlling peripherals.

For the complete example code, see `examples/posix/ai/rtc-tcp-client/mcp_demo.c`.

## How It Works {#工作原理}

The device acts as the MCP **Server**, and the cloud AI acts as the MCP **Client**. The interaction flow is:

```
User sends text/voice ──> Cloud AI (LLM)
                              │
                LLM decides to invoke a device tool
                              │
                              v
Device receives TAI_EVT_MCP_CMD  <── JSON-RPC 2.0 request
        │
        v
Device parses request, executes tool, and returns result
        │
        v
tai_send_mcp_response() ──> Cloud AI continues generating its response
```

The cloud sends three types of MCP requests in sequence:

| Method | Purpose |
|------|------|
| `initialize` | Perform the handshake and obtain the device's MCP capability declaration |
| `tools/list` | Obtain the list of tools exposed by the device |
| `tools/call` | Invoke a specific tool and obtain its result |

## Prerequisites {#前置条件}

- The device has completed IoT SDK initialization and established the TAI Connection (see [Quick Start](../tutorials/quick-start))
- MCP support is declared when establishing the Connection

## Steps {#步骤}

### 1. Declare MCP Support {#1-声明设备支持-mcp}

Set `supportCustomMCP` to `true` in `session_attrs_json` in `tai_config_t`:

```c
static const char SESSION_ATTRS[] =
    "{\"deviceMcp\":{\"supportCustomMCP\":true}}";

tai_config_t cfg = {
    // ... Other fields
    .session_attrs_json = SESSION_ATTRS,
};
```

### 2. Define the Tool Registry {#2-定义工具注册表}

Each tool requires four elements: a name, a description, an input parameter definition in JSON Schema format, and a C handler function.

```c
typedef int (*tool_fn_t)(const char *args_json, char *out, size_t out_cap);

typedef struct {
    const char *name;
    const char *description;
    const char *input_schema_json;
    tool_fn_t   fn;
} mcp_tool_t;
```

Example that registers two tools:

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
    // Parse the "action" and "target" fields from args_json
    // Perform the corresponding hardware operation
    // Return the result as JSON
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

`input_schema_json` follows the [JSON Schema](https://json-schema.org/) format. The cloud AI uses it to generate valid invocation arguments.

### 3. Handle MCP Events {#3-处理-mcp-事件}

Listen for `TAI_EVT_MCP_CMD` in the `on_event` callback and dispatch the request to a handler:

```c
static void on_event(tai_ctx_t *ctx, const tai_event_msg_t *msg, void *ud)
{
    if (msg->event_type == TAI_EVT_MCP_CMD) {
        handle_mcp_request(ctx, (const char *)msg->data, msg->len);
    }
}
```

### 4. Implement the MCP Request Dispatcher {#4-实现-mcp-请求分发器}

The received `data` is a JSON-RPC 2.0 request. Parse the `method` and `id` fields, then construct the corresponding response based on `method`:

```c
static void handle_mcp_request(tai_ctx_t *ctx,
                               const char *payload, size_t len)
{
    // msg->data is borrowed from the SDK receive buffer and is not terminated
    // by '\0', while every demo_json.h function requires a NUL-terminated
    // buffer. First copy exactly len bytes into an owned buffer.
    char *req = (char *)malloc(len + 1);
    if (!req) return;
    memcpy(req, payload, len);
    req[len] = '\0';

    // Parse method and id, taking only top-level members. The params.arguments
    // object of tools/call may contain its own "id" / "method". A textual
    // search can match that first; echoing the wrong id prevents the server
    // from correlating the response.
    char id[64];
    char method[64] = {0};
    int  have_id = (demo_mcp_copy_id(req, id, sizeof(id)) == 0);
    json_object_get_string(req, "method", method, sizeof(method));

    // A request without id is a notification. JSON-RPC 2.0 forbids a response.
    if (!have_id) { free(req); return; }

    char resp[2048];
    int  resp_len = 0;

    if (strcmp(method, "initialize") == 0) {
        resp_len = build_initialize_response(id, resp, sizeof(resp));
    } else if (strcmp(method, "tools/list") == 0) {
        resp_len = build_tools_list_response(id, resp, sizeof(resp));
    } else if (strcmp(method, "tools/call") == 0) {
        // Parse params.name and params.arguments
        // Find and invoke the corresponding tool
        resp_len = build_tools_call_response(id, name, args,
                                             resp, sizeof(resp));
    } else {
        resp_len = build_error_response(id, -32601, "Method not found",
                                        resp, sizeof(resp));
    }

    // Send the response
    if (resp_len > 0) {
        tai_send_mcp_response(ctx, resp);
    }
    free(req);
}
```

### 5. Construct JSON-RPC Responses {#5-构造-json-rpc-响应}

Each method requires a JSON-RPC 2.0 response in a specific format. The `id` field must exactly match the request's `id`.

**`initialize` response:**

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

**`tools/list` response:**

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

**`tools/call` response:**

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

When tool execution fails, set `isError` to `true` and put the error message in `text`.

**Error response (unknown method):**

```json
{
  "jsonrpc": "2.0",
  "id": 4,
  "error": { "code": -32601, "message": "Method not found" }
}
```

### 6. Send the Response {#6-发送响应}

Use `tai_send_mcp_response()` to send the constructed JSON string back to the cloud:

```c
int rc = tai_send_mcp_response(ctx, resp);
if (rc != TAI_OK) {
    // Handle a send failure
}
```

## Build and Run {#编译与运行}

```bash
cmake -S examples/posix -B build -DAGENTIC_KIT_BUILD_EXAMPLES=ON
cmake --build build --target mcp_demo
./build/mcp_demo [devid] [secret_key] [local_key]
```

## Add a Custom Tool {#添加自定义工具}

Adding a new tool requires only three steps:

1. **Write a handler function** -- parse `args_json`, execute the business logic, and write the result to `out`:

```c
static int tool_read_sensor(const char *args_json,
                            char *out, size_t out_cap)
{
    float temp = read_temperature_sensor();
    return snprintf(out, out_cap,
        "{\"temperature\":%.1f,\"unit\":\"celsius\"}", temp);
}
```

2. **Register it in the `k_tools` array**:

```c
{
    .name = "read_sensor",
    .description = "Read the temperature sensor value in celsius.",
    .input_schema_json =
        "{\"type\":\"object\",\"properties\":{},\"required\":[]}",
    .fn = tool_read_sensor,
},
```

3. **Rebuild** -- no dispatch logic changes are required. `tools/list` and `tools/call` automatically include the new tool.

## Considerations {#注意事项}

- All callbacks, including `TAI_EVT_MCP_CMD`, run on the background receive thread. Tool functions should avoid blocking for long periods.
- The response `id` must exactly match the request `id`, or the cloud cannot correlate them. There are two common pitfalls. First, use only the **top-level** `id`; a business-level `id` inside `params.arguments` (such as a light ID or song ID) may be matched first by code that takes the first textual match. Second, echo the `id` verbatim. Truncating a quoted ID drops its closing quote, while truncating an object or array value produces unbalanced JSON. `demo_mcp_copy_id()` in `demo_mcp.h` handles both cases.
- A request **without** an `id` is a notification. JSON-RPC 2.0 forbids responding to it, including with an error response containing `"id":null`.
- The argument to `tai_send_mcp_response()` is the complete JSON-RPC 2.0 response string, not just the `result` portion.
- Double quotes and backslashes in tool output must be escaped.
- Size the response buffer appropriately for the tool output to avoid truncation.
- The more precisely `input_schema_json` describes its fields, the higher the quality of the invocation arguments generated by the AI.
