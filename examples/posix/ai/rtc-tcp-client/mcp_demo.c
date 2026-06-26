/*
 * mcp_demo.c -- Device-side MCP (Model Context Protocol) demo
 *              using the rtc-tcp-client library.
 *
 * The Tuya AI server can push MCP JSON-RPC 2.0 requests to the device
 * as TAI_EVT_MCP_CMD events.  This example shows how to answer the
 * three methods the server actually uses:
 *
 *   - "initialize"  -> serverInfo + capabilities
 *   - "tools/list"  -> static catalog of device-exposed tools
 *   - "tools/call"  -> dispatch by name, return tool output text
 *
 * Bootstrap (identical to edu_threaded_chat_demo.c):
 *   1. iot-sdk init + login with devid / secret_key / local_key
 *   2. fetch session token via iot_client_get_session_token()
 *   3. parse connect_conf / session_conf out of the token
 *   4. build a TAI context, call tai_connect()
 *   5. send a text query so the server actually issues MCP calls
 *
 * Build:
 *   cmake -S examples/posix -B build -DAGENTIC_KIT_BUILD_EXAMPLES=ON
 *   cmake --build build --target tai_mcp_demo
 *
 * Usage:
 *   ./build/tai_mcp_demo [devid] [secret_key] [local_key]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "mbedtls/base64.h"

#include "tuya_ai.h"
#include "iot_client.h"
#include "demo_reconnect.h"

extern const pal_t *tai_pal_posix(void);

/* -- Defaults ----------------------------------------------------------- */

#define DEFAULT_DEVID      "6cd370251e8be96de8vwoe"
#define DEFAULT_SECRET_KEY "[SPT;N:b@)wPzK/)"
#define DEFAULT_LOCAL_KEY  "#d[<4y*N.vE]RAAG"

#define MCP_PROTOCOL_VERSION "2024-11-05"
#define MCP_SERVER_NAME       "tuya-mcp-demo"
#define MCP_SERVER_VERSION    "1.0.0"
#define MAX_WAIT_MS           60000
#define MAX_TOOL_OUTPUT       512

/* -- Demo context ------------------------------------------------------- */

typedef struct {
    volatile int     got_done;   /* set by on_event when the turn ends */
    demo_reconnect_t reconn;      /* app-side reconnect policy/state    */
} demo_ctx_t;

/* -------------------------------------------------------------------------
 * Tool registry
 *
 * Each tool is a (name, description, inputSchema) triple plus a C handler
 * that fills `out` with the textual result.  Real firmware would dispatch
 * to GPIO, sensors, NVS, etc.; here we return plausible demo data.
 * ------------------------------------------------------------------------- */

typedef int (*tool_fn_t)(const char *args_json, char *out, size_t out_cap);

typedef struct {
    const char *name;
    const char *description;
    const char *input_schema_json;
    tool_fn_t   fn;
} mcp_tool_t;

static int tool_get_device_status(const char *args_json, char *out, size_t out_cap)
{
    (void)args_json;
    /* Fake but stable device state. */
    return snprintf(out, out_cap,
        "{\"online\":true,\"battery\":87,"
        "\"volume\":40,\"light\":\"on\","
        "\"firmware\":\"%s\"}", MCP_SERVER_VERSION);
}

static int tool_control_device(const char *args_json, char *out, size_t out_cap)
{
    /* Expects: {"action":"on"|"off"|<other>, "target":"light"|"fan"|...} */
    char action[32] = {0};
    char target[32] = {0};

    const char *p = strstr(args_json ? args_json : "", "\"action\"");
    if (p) {
        p = strchr(p, ':');
        if (p) {
            p++;
            while (*p == ' ' || *p == '\"') p++;
            const char *end = strchr(p, '\"');
            if (end && (size_t)(end - p) < sizeof(action)) {
                memcpy(action, p, (size_t)(end - p));
                action[end - p] = '\0';
            }
        }
    }
    p = strstr(args_json ? args_json : "", "\"target\"");
    if (p) {
        p = strchr(p, ':');
        if (p) {
            p++;
            while (*p == ' ' || *p == '\"') p++;
            const char *end = strchr(p, '\"');
            if (end && (size_t)(end - p) < sizeof(target)) {
                memcpy(target, p, (size_t)(end - p));
                target[end - p] = '\0';
            }
        }
    }

    if (action[0] == '\0' || target[0] == '\0') {
        return snprintf(out, out_cap,
            "missing required argument: need action and target");
    }
    return snprintf(out, out_cap,
        "{\"target\":\"%s\",\"action\":\"%s\",\"ok\":true}",
        target, action);
}

static const mcp_tool_t k_tools[] = {
    {
        .name = "get_device_status",
        .description = "Return basic device state: online, battery, volume, light, firmware.",
        .input_schema_json = "{\"type\":\"object\",\"properties\":{},\"required\":[]}",
        .fn = tool_get_device_status,
    },
    {
        .name = "control_device",
        .description = "Send a simple on/off control to a named subsystem (light, fan, ...).",
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

static const mcp_tool_t *find_tool(const char *name)
{
    for (size_t i = 0; i < K_TOOLS_COUNT; i++) {
        if (strcmp(k_tools[i].name, name) == 0) return &k_tools[i];
    }
    return NULL;
}

/* -------------------------------------------------------------------------
 * Minimal JSON helpers
 *
 * The TAI payload is JSON-RPC 2.0; we only need to pull out a few scalar
 * fields and pass tool arguments through to handlers.  No full parser
 * needed -- the existing examples in this folder use the same approach.
 * ------------------------------------------------------------------------- */

static const char *json_find_value(const char *json, const char *key)
{
    if (!json || !key) return NULL;
    char search[128];
    snprintf(search, sizeof(search), "\"%s\"", key);
    const char *p = strstr(json, search);
    if (!p) return NULL;
    p += strlen(search);
    while (*p == ' ' || *p == ':' || *p == '\t') p++;
    return p;
}

static int json_get_string(const char *json, const char *key,
                           char *out, size_t cap)
{
    const char *p = json_find_value(json, key);
    if (!p || *p != '\"') return -1;
    p++;
    const char *end = strchr(p, '\"');
    if (!end) return -1;
    size_t len = (size_t)(end - p);
    if (len >= cap) len = cap - 1;
    memcpy(out, p, len);
    out[len] = '\0';
    return 0;
}

/* Find a sub-object's raw text so we can hand it to a handler.  Returns
 * a freshly allocated string the caller must free. */
static char *json_get_object_raw(const char *json, const char *key)
{
    const char *p = json_find_value(json, key);
    if (!p || *p != '{') return NULL;
    int depth = 0;
    const char *start = p, *q = p;
    while (*q) {
        if (*q == '{') depth++;
        else if (*q == '}' && --depth == 0) {
            size_t len = (size_t)(q - start + 1);
            char *obj = (char *)malloc(len + 1);
            if (obj) { memcpy(obj, start, len); obj[len] = '\0'; }
            return obj;
        }
        q++;
    }
    return NULL;
}

/* Copy the request's "id" token verbatim (number or quoted string). */
static int copy_id(const char *request, char *out, size_t cap)
{
    const char *p = json_find_value(request, "id");
    if (!p) {
        if (cap > 0) out[0] = '\0';
        return -1;
    }
    /* Skip the value: number (digits/-/./e/E) or quoted string. */
    size_t n = 0;
    if (*p == '\"') {
        const char *end = strchr(p + 1, '\"');
        if (!end) return -1;
        n = (size_t)(end - p + 1);
    } else {
        const char *end = p;
        while (*end && *end != ',' && *end != '}' &&
               *end != ' '  && *end != '\n' && *end != '\r') end++;
        n = (size_t)(end - p);
    }
    if (n + 1 > cap) n = cap - 1;
    memcpy(out, p, n);
    out[n] = '\0';
    return 0;
}

/* -------------------------------------------------------------------------
 * Base64 + token parsing (mirrors the other rtc-tcp-client examples)
 * ------------------------------------------------------------------------- */

static char *b64_decode(const char *encoded, size_t *out_len)
{
    size_t elen = strlen(encoded);
    size_t dlen = 0;
    if (mbedtls_base64_decode(NULL, 0, &dlen,
                               (const unsigned char *)encoded, elen)
            != MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL)
        return NULL;
    char *out = (char *)malloc(dlen + 1);
    if (!out) return NULL;
    if (mbedtls_base64_decode((unsigned char *)out, dlen, &dlen,
                               (const unsigned char *)encoded, elen) != 0) {
        free(out);
        return NULL;
    }
    out[dlen] = '\0';
    if (out_len) *out_len = dlen;
    return out;
}

typedef struct {
    char     host[256];
    char     tls_sni[256];
    char     derived_client_id[256];
    char     agent_token[256];
    uint16_t port;
    long     biz_code;
    long     biz_tag;
} tai_conn_params_t;

static int json_array_first_string(const char *json, const char *key,
                                   char *out, size_t cap)
{
    const char *p = json_find_value(json, key);
    if (!p || *p != '[') return -1;
    p++;
    while (*p == ' ') p++;
    if (*p != '\"') return -1;
    p++;
    const char *end = strchr(p, '\"');
    if (!end) return -1;
    size_t len = (size_t)(end - p);
    if (len >= cap) len = cap - 1;
    memcpy(out, p, len);
    out[len] = '\0';
    return 0;
}

static int parse_token(const char *raw_token, tai_conn_params_t *p)
{
    memset(p, 0, sizeof(*p));
    char *json = NULL;
    {
        size_t dl = 0;
        char *decoded = b64_decode(raw_token, &dl);
        if (decoded && dl > 0 && decoded[0] == '{') {
            json = decoded;
        } else {
            free(decoded);
            json = strdup(raw_token);
        }
    }
    if (!json) return -1;

    char *conn = json_get_object_raw(json, "connect_conf");
    if (!conn) { free(json); return -1; }
    json_array_first_string(conn, "hosts",  p->host, sizeof(p->host));
    if (json_array_first_string(conn, "domains", p->tls_sni, sizeof(p->tls_sni)) != 0)
        strncpy(p->tls_sni, p->host, sizeof(p->tls_sni) - 1);

    /* crude numeric scan: look for "ecc_tls_port" : <digits> */
    const char *pp = json_find_value(conn, "ecc_tls_port");
    long port = 0;
    if (pp) port = strtol(pp, NULL, 10);
    p->port = (port > 0) ? (uint16_t)port : 443;

    json_get_string(conn, "derived_client_id",
                    p->derived_client_id, sizeof(p->derived_client_id));
    free(conn);

    char *sess = json_get_object_raw(json, "session_conf");
    if (sess) {
        json_get_string(sess, "agentToken",
                        p->agent_token, sizeof(p->agent_token));
        char *biz = json_get_object_raw(sess, "bizConfig");
        if (biz) {
            const char *bc = json_find_value(biz, "bizCode");
            const char *bt = json_find_value(biz, "bizTag");
            if (bc) p->biz_code = strtol(bc, NULL, 10);
            if (bt) p->biz_tag  = strtol(bt, NULL, 10);
            free(biz);
        }
        free(sess);
    }
    free(json);

    if (p->host[0] == '\0') return -1;
    return 0;
}

/* -------------------------------------------------------------------------
 * MCP response builders
 * ------------------------------------------------------------------------- */

static int build_initialize_response(const char *id,
                                     char *out, size_t cap)
{
    return snprintf(out, cap,
        "{\"jsonrpc\":\"2.0\",\"id\":%s,"
        "\"result\":{"
            "\"protocolVersion\":\"%s\","
            "\"serverInfo\":{\"name\":\"%s\",\"version\":\"%s\"},"
            "\"capabilities\":{\"tools\":{}}"
        "}}",
        id, MCP_PROTOCOL_VERSION, MCP_SERVER_NAME, MCP_SERVER_VERSION);
}

static int build_tools_list_response(const char *id,
                                     char *out, size_t cap)
{
    /* Open envelope. */
    int n = snprintf(out, cap,
        "{\"jsonrpc\":\"2.0\",\"id\":%s,\"result\":{\"tools\":[",
        id);
    if (n < 0 || (size_t)n >= cap) return n;

    for (size_t i = 0; i < K_TOOLS_COUNT; i++) {
        int w = snprintf(out + n, cap - (size_t)n,
            "%s{"
                "\"name\":\"%s\","
                "\"description\":\"%s\","
                "\"inputSchema\":%s"
            "}",
            (i == 0) ? "" : ",",
            k_tools[i].name,
            k_tools[i].description,
            k_tools[i].input_schema_json);
        if (w < 0 || (size_t)(n + w) >= cap) return -1;
        n += w;
    }
    int w = snprintf(out + n, cap - (size_t)n, "]}}");
    if (w < 0 || (size_t)(n + w) >= cap) return -1;
    return n + w;
}

static int build_tools_call_response(const char *id, const char *name,
                                     const char *args_json,
                                     char *out, size_t cap)
{
    char result_text[MAX_TOOL_OUTPUT] = {0};
    int  is_error = 0;

    const mcp_tool_t *t = find_tool(name);
    if (!t) {
        snprintf(result_text, sizeof(result_text),
                 "unknown tool: %s", name ? name : "(null)");
        is_error = 1;
    } else {
        t->fn(args_json, result_text, sizeof(result_text));
    }

    /* Escape the bare minimum: backslashes and double quotes.  The
     * tool outputs above are JSON already and don't contain those, so
     * this loop is a no-op for the demo but kept as a safety net. */
    char escaped[MAX_TOOL_OUTPUT * 2] = {0};
    size_t j = 0;
    for (size_t i = 0; result_text[i] && j + 2 < sizeof(escaped); i++) {
        char c = result_text[i];
        if (c == '\"' || c == '\\') escaped[j++] = '\\';
        escaped[j++] = c;
    }
    escaped[j] = '\0';

    return snprintf(out, cap,
        "{\"jsonrpc\":\"2.0\",\"id\":%s,"
        "\"result\":{"
            "\"content\":[{\"type\":\"text\",\"text\":\"%s\"}],"
            "\"isError\":%s"
        "}}",
        id, escaped, is_error ? "true" : "false");
}

static int build_error_response(const char *id, int code, const char *message,
                                char *out, size_t cap)
{
    return snprintf(out, cap,
        "{\"jsonrpc\":\"2.0\",\"id\":%s,"
        "\"error\":{\"code\":%d,\"message\":\"%s\"}}",
        id, code, message);
}

/* -------------------------------------------------------------------------
 * MCP request dispatcher
 * ------------------------------------------------------------------------- */

static void handle_mcp_request(tai_ctx_t *ctx, const char *payload, size_t len)
{
    if (!payload || len == 0) return;

    /* Make a NUL-terminated working copy. */
    char *req = (char *)malloc(len + 1);
    if (!req) return;
    memcpy(req, payload, len);
    req[len] = '\0';

    char id[64] = "null";
    char method[64] = {0};
    copy_id(req, id, sizeof(id));
    json_get_string(req, "method", method, sizeof(method));

    fprintf(stderr, "[MCP] <- method=\"%s\" id=%s\n",
            method[0] ? method : "(none)", id);

    char resp[2048];
    int  resp_len = 0;

    if (strcmp(method, "initialize") == 0) {
        resp_len = build_initialize_response(id, resp, sizeof(resp));
    } else if (strcmp(method, "tools/list") == 0) {
        resp_len = build_tools_list_response(id, resp, sizeof(resp));
    } else if (strcmp(method, "tools/call") == 0) {
        char *params = json_get_object_raw(req, "params");
        char name[64] = {0};
        json_get_string(params ? params : "", "name", name, sizeof(name));
        char *args = json_get_object_raw(params ? params : "", "arguments");
        fprintf(stderr, "[MCP] tools/call: name=\"%s\" args=%s\n",
                name, args ? args : "{}");
        resp_len = build_tools_call_response(id, name, args, resp, sizeof(resp));
        free(args);
        free(params);
    } else {
        resp_len = build_error_response(id, -32601, "Method not found",
                                        resp, sizeof(resp));
    }

    if (resp_len < 0) {
        fprintf(stderr, "[MCP] response build overflow\n");
    } else {
        fprintf(stderr, "[MCP] -> %.*s\n\n",
                resp_len > 300 ? 300 : resp_len, resp);
        int rc = tai_send_mcp_response(ctx, resp);
        if (rc != TAI_OK)
            fprintf(stderr, "[MCP] tai_send_mcp_response failed: %d\n", rc);
    }
    free(req);
}

/* -------------------------------------------------------------------------
 * TAI callbacks
 * ------------------------------------------------------------------------- */

/* Extract data.content from an NLG JSON line.  Returns a pointer into
 * `text` (not a copy).  Returns NULL if the line is not NLG or
 * content is missing. */
static const char *nlg_extract_content(const char *text, size_t len,
                                       size_t *out_len)
{
    /* Quick guard: must contain "NLG" and "content" */
    if (!strstr(text, "\"NLG\"") || !strstr(text, "\"content\""))
        return NULL;

    const char *p = strstr(text, "\"content\"");
    if (!p) return NULL;
    p = strchr(p, ':');
    if (!p) return NULL;
    p++;
    while (*p == ' ' || *p == '\"') p++;

    const char *end = strchr(p, '\"');
    if (!end) return NULL;

    *out_len = (size_t)(end - p);
    return p;
}

static void on_text(tai_ctx_t *ctx, const tai_text_msg_t *msg, void *ud)
{
    (void)ctx; (void)ud;

    /* For NLG lines, print only the content field. */
    size_t clen = 0;
    const char *content = nlg_extract_content(msg->text, msg->len, &clen);
    if (content && clen > 0) {
        fwrite(content, 1, clen, stdout);
        fflush(stdout);
        return;
    }

    /* Non-NLG text: print raw. */
    fwrite(msg->text, 1, msg->len, stdout);
    fputc('\n', stdout);
    fflush(stdout);
}

static void on_audio(tai_ctx_t *ctx, const tai_audio_msg_t *msg, void *ud)
{
    (void)ctx; (void)msg; (void)ud;
}

static void on_event(tai_ctx_t *ctx, const tai_event_msg_t *msg, void *ud)
{
    demo_ctx_t *dc = (demo_ctx_t *)ud;
    if (msg->event_type == TAI_EVT_END) {
        dc->got_done = 1;
    } else if (msg->event_type == TAI_EVT_MCP_CMD) {
        handle_mcp_request(ctx, (const char *)msg->data, msg->len);
    }
}

static void on_disconnect(tai_ctx_t *ctx, const tai_disconnect_msg_t *msg, void *ud)
{
    (void)ctx;
    demo_ctx_t *dc = (demo_ctx_t *)ud;
    fprintf(stderr, "\n[disconnected: reason=%u close_code=%u]\n",
            (unsigned)msg->reason, (unsigned)msg->close_code);
    /* Runs on the worker thread: only flag — the main loop reconnects. */
    demo_reconnect_signal(&dc->reconn, msg->reason, msg->close_code);
}

/* -------------------------------------------------------------------------
 * main
 * ------------------------------------------------------------------------- */

int main(int argc, char *argv[])
{
    const char *devid      = (argc >= 2) ? argv[1] : DEFAULT_DEVID;
    const char *secret_key = (argc >= 3) ? argv[2] : DEFAULT_SECRET_KEY;
    const char *local_key  = (argc >= 4) ? argv[3] : DEFAULT_LOCAL_KEY;

    printf("=== tai_mcp_demo (device-side MCP server) ===\n");
    printf("Device ID : %s\n", devid);
    printf("Exposing %zu tools:\n", K_TOOLS_COUNT);
    for (size_t i = 0; i < K_TOOLS_COUNT; i++)
        printf("  - %s\n", k_tools[i].name);
    printf("\n");

    /* ---- 1. iot-sdk init ------------------------------------------------ */
    iot_init_default();
    iot_client_config_t iot_cfg = {
        .devid            = {0},
        .secret_key       = {0},
        .local_key        = {0},
        .region           = AY,
        .env              = PROD,
        .mqtt_disable_tls = false,
        .message_callback = NULL,
    };
    memcpy((char *)iot_cfg.devid,      devid,      strlen(devid));
    memcpy((char *)iot_cfg.secret_key, secret_key, strlen(secret_key));
    memcpy((char *)iot_cfg.local_key,  local_key,  strlen(local_key));

    iot_client_t *iot = iot_client_init(&iot_cfg);
    if (!iot) { fprintf(stderr, "iot_client_init failed\n"); return 1; }

    /* ---- 2. Fetch session token ---------------------------------------- */
    char *token = (char *)calloc(1, 4096);
    if (!token) { iot_client_deinit(iot); return 1; }
    if (iot_client_get_session_token(iot, NULL, token, 4096) != 0 || token[0] == '\0') {
        fprintf(stderr, "iot_client_get_session_token failed\n");
        free(token); iot_client_deinit(iot);
        return 1;
    }

    /* ---- 3. Parse token ------------------------------------------------ */
    tai_conn_params_t cp;
    if (parse_token(token, &cp) != 0) {
        fprintf(stderr, "Token parse failed\n");
        free(token); iot_client_deinit(iot);
        return 1;
    }
    if (cp.biz_code == 0) cp.biz_code = 65537;
    if (cp.biz_tag  == 0) cp.biz_tag  = 119;

    printf("[main] TAI server : %s:%u (SNI: %s)\n", cp.host, cp.port, cp.tls_sni);
    printf("[main] Client ID  : %s\n\n", cp.derived_client_id);

    free(token);
    iot_client_deinit(iot);   /* no longer needed once we have the token */

    /* ---- 4. Build TAI context ------------------------------------------ */
    demo_ctx_t dc;
    memset(&dc, 0, sizeof(dc));

    const pal_t *pal = tai_pal_posix();

    /* Tell the server this device speaks the custom-MCP dialect. */
    static const char SESSION_ATTRS[] =
        "{\"deviceMcp\":{\"supportCustomMCP\":true}}";
    static const char EVENT_USER_DATA[] =
        "{\"sys.workflow\":\"asr-llm-tts\"}";

    tai_config_t tai_cfg = {
        .host              = cp.host,
        .port              = cp.port,
        .tls_sni           = cp.tls_sni,
        .device_id         = cp.derived_client_id,
        .local_key         = local_key,
        .protocol_version  = TAI_VER_21,
        .client_type       = TAI_CLIENT_DEVICE,
        .sign_level        = TAI_SIGN_HMAC_SHA256,
        .biz_code          = (uint32_t)cp.biz_code,
        .biz_tag           = (uint64_t)cp.biz_tag,
        .agent_token       = cp.agent_token,
        .session_attrs_json   = SESSION_ATTRS,
        .event_user_data_json = EVENT_USER_DATA,
        .pal               = pal,
        .on_text           = on_text,
        .on_audio          = on_audio,
        .on_event          = on_event,
        .on_disconnect     = on_disconnect,
        .user_data         = &dc,
    };

    void *ctx_buf = pal->malloc(tai_ctx_size());
    if (!ctx_buf) { fprintf(stderr, "OOM\n"); return 1; }

    tai_ctx_t *ctx = tai_ctx_init(ctx_buf, &tai_cfg);
    if (!ctx) { fprintf(stderr, "tai_ctx_init failed\n"); pal->free(ctx_buf); return 1; }

    tai_set_log_level(TAI_LOG_WARN);

    /* ---- 5-7. Connect, serve MCP — with app-driven reconnect -----------
     *
     * on_disconnect runs on the worker thread and only flags dc.reconn (it must
     * not self-disconnect). This owning thread does tai_disconnect() +
     * tai_connect() with exponential backoff + a circuit breaker — the correct
     * response to the fail-fast model (see demo_reconnect.h). mcp_demo is
     * long-lived: after connecting it sends a query and then serves MCP commands
     * until the turn ends (got_done) or the overall MAX_WAIT_MS budget elapses;
     * a drop in the middle reconnects and resumes serving. */
    int done = 0;
    while (!done) {
        printf("[main] Connecting to TAI server...\n");
        int crc = tai_connect(ctx);
        if (crc != TAI_OK) {
            fprintf(stderr, "[main] tai_connect failed: %d\n", crc);
            if (demo_reconnect_tripped(&dc.reconn)) {
                fprintf(stderr, "[main] circuit breaker: giving up after %d attempts\n",
                        dc.reconn.attempt);
                goto cleanup;
            }
            uint32_t delay = demo_reconnect_delay_ms(&dc.reconn);
            fprintf(stderr, "[main] retry connect in %u ms (attempt %d)\n",
                    delay, dc.reconn.attempt + 1);
            usleep(delay * 1000);
            dc.reconn.attempt++;
            dc.reconn.need_reconnect = 0;
            continue;
        }
        demo_reconnect_ok(&dc.reconn);
        printf("[main] Connected. Server may now send MCP requests.\n\n");

        /* ---- 6. Send a text query --------------------------------------- */
        /* The server triggers MCP "tools/list" and "tools/call" when it needs
         * to call a device tool -- usually in response to a user question. */

        /* Give the server time to inject MCP tool definitions into the LLM
         * parameters before the first user query arrives. */
        printf("[main] Waiting 2 s for server-side MCP injection...\n");
        fflush(stdout);
        sleep(2);

        const char *question =
            "query the device status for me";
        printf("[main] Sending text: \"%s\"\nResponse: ", question);
        fflush(stdout);

        int rc = tai_send_text(ctx, question, strlen(question));
        if (rc != TAI_OK) {
            /* An app-thread send failure is reported synchronously here — the
             * SDK does NOT fire on_disconnect for it (only the worker's own
             * ping does). The TX stream may be desynced, so treat it as a
             * transport fault: request a reconnect so the teardown path below
             * rebuilds the link instead of exiting as a benign timeout. */
            fprintf(stderr, "tai_send_text failed: %d\n", rc);
            demo_reconnect_signal(&dc.reconn, TAI_DISCONNECT_TRANSPORT, 0);
        }

        /* ---- 7. Serve MCP requests / wait for the turn to end ----------- */
        int waited = 0;
        while (!dc.got_done && !dc.reconn.need_reconnect && waited < MAX_WAIT_MS) {
            usleep(100 * 1000);
            waited += 100;
        }

        if (dc.got_done) {
            done = 1;                                  /* turn ended cleanly */
        } else if (!dc.reconn.need_reconnect) {
            printf("\n[main] Timed out after %d s\n", MAX_WAIT_MS / 1000);
            done = 1;                                  /* budget spent, link up */
        } else {
            /* Dropped mid-serve: tear down on this (owning) thread, back off,
             * then loop to reconnect and resume serving. */
            fprintf(stderr, "\n[main] disconnected (reason=%u code=%u)\n",
                    dc.reconn.reason, dc.reconn.close_code);
            tai_disconnect(ctx);
            if (demo_reconnect_tripped(&dc.reconn)) {
                fprintf(stderr, "[main] circuit breaker: giving up after %d attempts\n",
                        dc.reconn.attempt);
                done = 1;
            } else {
                uint32_t delay = demo_reconnect_delay_ms(&dc.reconn);
                fprintf(stderr, "[main] reconnect in %u ms (attempt %d)\n",
                        delay, dc.reconn.attempt + 1);
                usleep(delay * 1000);
                dc.reconn.attempt++;
                dc.reconn.need_reconnect = 0;
            }
        }
    }

cleanup:
    /* ---- 8. Shutdown --------------------------------------------------- */
    tai_disconnect(ctx);
    tai_ctx_deinit(ctx);
    pal->free(ctx_buf);

    printf("\nDone.\n");
    return dc.got_done ? 0 : 1;
}
