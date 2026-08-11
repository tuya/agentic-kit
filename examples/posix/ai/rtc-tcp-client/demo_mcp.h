/*
 * demo_mcp.h — device-side MCP request handling shared by the rtc-tcp-client
 * POSIX demos (header-only).
 *
 * The server pushes MCP JSON-RPC 2.0 requests as TAI_EVT_MCP_CMD events to any
 * device declaring deviceMcp.supportCustomMCP — which the SDK's default
 * session attributes do, so passing no session_attrs_json opts you in.
 *
 * Answering properly, even with an empty tool catalog, means:
 *
 *   - echo the request `id` — JSON-RPC correlates on it;
 *   - use the result shape the method expects (an `initialize` handshake
 *     answered with a `tools/call` body is not an answer);
 *   - stay silent for an id-less request, which is a notification.
 *
 * demo_mcp_reply_no_tools() does that. mcp_demo.c shows the real thing: a tool
 * registry, dispatch and results.
 */
#ifndef DEMO_MCP_H
#define DEMO_MCP_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tuya_ai.h"
#include "demo_json.h"

#ifndef DEMO_MCP_PROTOCOL_VERSION
#define DEMO_MCP_PROTOCOL_VERSION "2024-11-05"
#endif
#ifndef DEMO_MCP_SERVER_NAME
#define DEMO_MCP_SERVER_NAME "tuya-agentic-kit-demo"
#endif
#ifndef DEMO_MCP_SERVER_VERSION
#define DEMO_MCP_SERVER_VERSION "1.0.0"
#endif

/* Copy the request's `id` token verbatim (a string, a number or null, kept as
 * written so it can be spliced straight back into the response).
 *
 * The id is read from the TOP-LEVEL members only. A `tools/call` may carry an
 * "id" of its own inside params.arguments — a lamp id, a track id — and a
 * document-order search finds that one first, which would echo the wrong value
 * and leave the server unable to match the response to its request.
 *
 * Returns 0 on success; -1 when the request carries no id — a notification,
 * which takes no response — and when the token cannot be echoed verbatim:
 * a truncated quoted id, or an object/array id (not a JSON-RPC id type, and
 * splicing part of one back would produce unbalanced JSON). `out` is untouched
 * on -1, so a caller may pre-seed it with a default. */
static inline int demo_mcp_copy_id(const char *request, char *out, size_t cap)
{
    const char *p = json_object_find(request, "id");
    if (!p || cap == 0) return -1;

    size_t n;
    if (*p == '"') {
        const char *end = json_str_end(p + 1);   /* escape-aware */
        if (!end) return -1;
        n = (size_t)(end - p + 1);
    } else {
        const char *end = json_skip_value(p);
        if (!end || *p == '{' || *p == '[') return -1;
        n = (size_t)(end - p);
        /* JSON-RPC 2.0 ids are String, Number or Null; anything else that got
         * this far is not one, so do not quote it back at the server. */
        if (*p != '-' && (*p < '0' || *p > '9') &&
            !(n == 4 && memcmp(p, "null", 4) == 0)) return -1;
    }
    if (n == 0 || n >= cap) return -1;

    memcpy(out, p, n);
    out[n] = '\0';
    return 0;
}

/* Answer one TAI_EVT_MCP_CMD event as a device that exposes no tools. Call it
 * straight from on_event with the borrowed msg — the payload is copied before
 * parsing, since tai_event_msg_t.data is not NUL-terminated. */
static inline void demo_mcp_reply_no_tools(tai_ctx_t *ctx,
                                           const tai_event_msg_t *msg)
{
    if (!msg->data || msg->len == 0) return;

    char *req = (char *)malloc(msg->len + 1);
    if (!req) {
        fprintf(stderr, "[demo_mcp] out of memory copying a %zu-byte request\n",
                msg->len);
        return;
    }
    memcpy(req, msg->data, msg->len);
    req[msg->len] = '\0';

    char id[64];
    char method[64] = {0};
    int  have_id = (demo_mcp_copy_id(req, id, sizeof(id)) == 0);
    /* Top-level only, for the same reason as the id: params.arguments may hold
     * a "method" of its own, and answering that one is a wrong answer. */
    json_object_get_string(req, "method", method, sizeof(method));
    free(req);

    if (!have_id) return;   /* notification (or an unusable id): no response */

    char resp[512];
    int  n;
    if (strcmp(method, "initialize") == 0) {
        n = snprintf(resp, sizeof(resp),
            "{\"jsonrpc\":\"2.0\",\"id\":%s,\"result\":{"
                "\"protocolVersion\":\"%s\","
                "\"serverInfo\":{\"name\":\"%s\",\"version\":\"%s\"},"
                "\"capabilities\":{\"tools\":{}}}}",
            id, DEMO_MCP_PROTOCOL_VERSION,
            DEMO_MCP_SERVER_NAME, DEMO_MCP_SERVER_VERSION);
    } else if (strcmp(method, "tools/list") == 0) {
        n = snprintf(resp, sizeof(resp),
            "{\"jsonrpc\":\"2.0\",\"id\":%s,\"result\":{\"tools\":[]}}", id);
    } else if (strcmp(method, "tools/call") == 0) {
        /* The advertised catalog is empty, so every tool name is invalid. */
        n = snprintf(resp, sizeof(resp),
            "{\"jsonrpc\":\"2.0\",\"id\":%s,\"error\":"
            "{\"code\":-32602,\"message\":\"this device exposes no tools\"}}", id);
    } else {
        n = snprintf(resp, sizeof(resp),
            "{\"jsonrpc\":\"2.0\",\"id\":%s,\"error\":"
            "{\"code\":-32601,\"message\":\"Method not found\"}}", id);
    }

    if (n < 0 || (size_t)n >= sizeof(resp)) {
        fprintf(stderr, "[demo_mcp] response overflow (method=\"%s\")\n", method);
        return;
    }

    int rc = tai_send_mcp_response(ctx, resp);
    if (rc != TAI_OK)
        fprintf(stderr, "[demo_mcp] tai_send_mcp_response failed: %d\n", rc);
}

#endif /* DEMO_MCP_H */
