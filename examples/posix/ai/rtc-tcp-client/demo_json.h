/*
 * demo_json.h — shared JSON / base64 / session-token helpers for the
 * rtc-tcp-client POSIX demos (header-only).
 *
 * Deliberately partial — enough to walk a known response shape, with no DOM —
 * but string-aware: a '{', '}', '[', ']' or '"' inside a JSON string literal
 * never counts as structure, and \" \\ \/ \uXXXX escapes are decoded. Real
 * payloads need both (a song title containing a bracket, a server that emits
 * "https:\/\/…").
 *
 * IMPORTANT: every function here takes a NUL-terminated buffer. Receive-callback
 * payloads (tai_text_msg_t.text, tai_event_msg_t.data) are NOT NUL-terminated —
 * copy them out first (see demo_text.h).
 */
#ifndef DEMO_JSON_H
#define DEMO_JSON_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mbedtls/base64.h"

/* -- Scanning primitives -------------------------------------------------- */

static inline int json_is_space(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

/* `p` points just past a string literal's opening quote. Returns the closing
 * quote, honouring backslash escapes, or NULL if the literal is unterminated. */
static inline const char *json_str_end(const char *p)
{
    while (*p) {
        if (*p == '\\') {
            if (!p[1]) return NULL;
            p += 2;
            continue;
        }
        if (*p == '"') return p;
        p++;
    }
    return NULL;
}

/* End of the balanced `open`…`close` span starting at `p` — the byte just past
 * the closing delimiter — or NULL if it is unbalanced. String literals are
 * skipped whole, so a delimiter inside a JSON string never closes the span. */
static inline const char *json_span_end(const char *p, char open, char close)
{
    if (!p || *p != open) return NULL;
    int depth = 0;
    while (*p) {
        if (*p == '"') {
            const char *e = json_str_end(p + 1);
            if (!e) return NULL;
            p = e + 1;
            continue;
        }
        if (*p == open) {
            depth++;
        } else if (*p == close && --depth == 0) {
            return p + 1;
        }
        p++;
    }
    return NULL;
}

/* Skip one whole JSON value at `p` (string, object, array, number, literal).
 * Returns the byte just past it, or NULL if it is malformed. */
static inline const char *json_skip_value(const char *p)
{
    if (!p) return NULL;
    if (*p == '"') {
        const char *e = json_str_end(p + 1);
        return e ? e + 1 : NULL;
    }
    if (*p == '{') return json_span_end(p, '{', '}');
    if (*p == '[') return json_span_end(p, '[', ']');

    const char *s = p;   /* number / true / false / null */
    while (*p && *p != ',' && *p != '}' && *p != ']' && !json_is_space(*p)) p++;
    return (p > s) ? p : NULL;
}

/* Value that follows the first `"key":` in `json`, at any depth; NULL if
 * absent. String literals are skipped whole, so a key name inside a *value* is
 * not mistaken for the key. The match is first-in-document-order: for
 * {"code":0,"data":{"code":"music"}} this returns the outer 0 — drill into the
 * enclosing object (json_get_object) when you want a nested key, or use
 * json_object_find() when only a top-level member will do. */
static inline const char *json_find_value(const char *json, const char *key)
{
    if (!json || !key) return NULL;
    size_t klen = strlen(key);
    const char *p = json;
    while (*p) {
        if (*p != '"') { p++; continue; }
        const char *s = p + 1;
        const char *e = json_str_end(s);
        if (!e) return NULL;
        if ((size_t)(e - s) == klen && memcmp(s, key, klen) == 0) {
            const char *v = e + 1;
            while (json_is_space(*v)) v++;
            if (*v == ':') {
                v++;
                while (json_is_space(*v)) v++;
                return v;
            }
        }
        p = e + 1;  /* not our key (or not used as a key): skip the literal */
    }
    return NULL;
}

/* Value of `key` among the TOP-LEVEL members of the object at `json` (leading
 * whitespace then '{'); NULL if absent or if the text is not an object.
 *
 * Unlike json_find_value(), a same-named key nested inside another member's
 * value is never returned. Protocol fields need this: a JSON-RPC request may
 * legitimately carry an `id` or `method` of its own inside params.arguments,
 * and echoing that one back breaks request/response correlation. */
static inline const char *json_object_find(const char *json, const char *key)
{
    if (!json || !key) return NULL;
    const char *p = json;
    while (json_is_space(*p)) p++;
    if (*p != '{') return NULL;
    p++;

    size_t klen = strlen(key);
    for (;;) {
        while (json_is_space(*p)) p++;
        if (*p != '"') return NULL;              /* '}' , '\0' or malformed */

        const char *s = p + 1;
        const char *e = json_str_end(s);
        if (!e) return NULL;
        int match = ((size_t)(e - s) == klen && memcmp(s, key, klen) == 0);

        p = e + 1;
        while (json_is_space(*p)) p++;
        if (*p != ':') return NULL;
        p++;
        while (json_is_space(*p)) p++;
        if (match) return p;

        p = json_skip_value(p);
        if (!p) return NULL;
        while (json_is_space(*p)) p++;
        if (*p != ',') return NULL;              /* '}' or malformed */
        p++;
    }
}

/* -- String values -------------------------------------------------------- */

static inline int json_hex4(const char *p, uint32_t *out)
{
    uint32_t v = 0;
    for (int i = 0; i < 4; i++) {
        char c = p[i];
        v <<= 4;
        if      (c >= '0' && c <= '9') v |= (uint32_t)(c - '0');
        else if (c >= 'a' && c <= 'f') v |= (uint32_t)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') v |= (uint32_t)(c - 'A' + 10);
        else return -1;
    }
    *out = v;
    return 0;
}

static inline int json_utf8_put(char *out, size_t cap, size_t *n, uint32_t cp)
{
    char   tmp[4];
    size_t k = 0;
    if (cp < 0x80) {
        tmp[k++] = (char)cp;
    } else if (cp < 0x800) {
        tmp[k++] = (char)(0xC0 | (cp >> 6));
        tmp[k++] = (char)(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        tmp[k++] = (char)(0xE0 | (cp >> 12));
        tmp[k++] = (char)(0x80 | ((cp >> 6) & 0x3F));
        tmp[k++] = (char)(0x80 | (cp & 0x3F));
    } else {
        tmp[k++] = (char)(0xF0 | (cp >> 18));
        tmp[k++] = (char)(0x80 | ((cp >> 12) & 0x3F));
        tmp[k++] = (char)(0x80 | ((cp >> 6) & 0x3F));
        tmp[k++] = (char)(0x80 | (cp & 0x3F));
    }
    if (*n + k >= cap) return -1;
    memcpy(out + *n, tmp, k);
    *n += k;
    return 0;
}

/* Decode the raw body of a JSON string literal — `p`/`len` delimit the bytes
 * BETWEEN the quotes, so this works on a slice that carries no terminator —
 * into NUL-terminated `out`. Returns the decoded length, or -1 if the text is
 * malformed.
 *
 * `truncate` decides what "does not fit" means. 0: empty `out` and return -1,
 * so ignoring the return value yields an obviously empty field rather than a
 * half-copied credential. 1: keep the prefix that fits and return its length —
 * the rest is still decoded, so a malformed tail is still reported. */
static inline int json_unescape_ex(const char *p, size_t len, char *out,
                                   size_t cap, int truncate)
{
    if (!out || cap == 0) return -1;
    out[0] = '\0';
    if (!p) return len ? -1 : 0;

    const char *end  = p + len;
    size_t      n    = 0;
    int         full = 0;   /* out is up to capacity; keep validating only */

    while (p < end) {
        char     lit   = 0;
        uint32_t cp    = 0;
        int      is_cp = 0;

        if (*p != '\\') {
            lit = *p++;
        } else {
            if (++p >= end) goto fail;
            switch (*p) {
            case '"': case '\\': case '/': lit = *p++;  break;
            case 'b':                      lit = '\b'; p++; break;
            case 'f':                      lit = '\f'; p++; break;
            case 'n':                      lit = '\n'; p++; break;
            case 'r':                      lit = '\r'; p++; break;
            case 't':                      lit = '\t'; p++; break;
            case 'u':
                if (end - p < 5 || json_hex4(p + 1, &cp) != 0) goto fail;
                p += 5;
                if (cp >= 0xD800 && cp <= 0xDBFF && end - p >= 6 &&
                    p[0] == '\\' && p[1] == 'u') {
                    uint32_t lo = 0;
                    if (json_hex4(p + 2, &lo) == 0 && lo >= 0xDC00 && lo <= 0xDFFF) {
                        cp = 0x10000u + ((cp - 0xD800u) << 10) + (lo - 0xDC00u);
                        p += 6;
                    }
                }
                /* An embedded NUL truncates the value for every strlen
                 * consumer, and a lone surrogate is not a character. */
                if (cp == 0 || (cp >= 0xD800 && cp <= 0xDFFF)) goto fail;
                is_cp = 1;
                break;
            default:
                goto fail;
            }
        }

        if (full) continue;
        if (is_cp) {
            if (json_utf8_put(out, cap, &n, cp) != 0) {
                if (!truncate) goto fail;
                full = 1;
            }
        } else if (n + 1 >= cap) {
            if (!truncate) goto fail;
            full = 1;
        } else {
            out[n++] = lit;
        }
    }
    out[n] = '\0';
    return (int)n;

fail:
    out[0] = '\0';
    return -1;
}

/* Decode the raw body of a string literal, rejecting a value that does not fit
 * (see json_unescape_ex). Returns the decoded length, or -1. */
static inline int json_unescape(const char *p, size_t len, char *out, size_t cap)
{
    return json_unescape_ex(p, len, out, cap, 0);
}

/* Locate the string literal at `p` (its opening quote) and hand its raw body
 * back as `body`/`body_len`. Returns 0, or -1 if `p` is not a terminated
 * string literal. */
static inline int json_string_body(const char *p, const char **body,
                                   size_t *body_len)
{
    if (!p || *p != '"') return -1;
    const char *e = json_str_end(p + 1);
    if (!e) return -1;
    *body     = p + 1;
    *body_len = (size_t)(e - p - 1);
    return 0;
}

/* Decode the string literal at `p` (its opening quote) into `out`. Returns 0,
 * or -1 if it is malformed or does not fit — `out` is emptied rather than
 * truncated, so ignoring the return value yields an obviously empty field. */
static inline int json_copy_string(const char *p, char *out, size_t cap)
{
    if (!out || cap == 0) return -1;
    out[0] = '\0';

    const char *body;
    size_t      body_len;
    if (json_string_body(p, &body, &body_len) != 0) return -1;
    return json_unescape(body, body_len, out, cap) < 0 ? -1 : 0;
}

static inline int json_get_string(const char *json, const char *key,
                                  char *out, size_t cap)
{
    return json_copy_string(json_find_value(json, key), out, cap);
}

/* json_get_string() restricted to a top-level member (see json_object_find). */
static inline int json_object_get_string(const char *json, const char *key,
                                         char *out, size_t cap)
{
    return json_copy_string(json_object_find(json, key), out, cap);
}

/* Like json_get_string(), but for a value that is only ever printed: one too
 * long for `out` is truncated rather than dropped, since a shortened song title
 * still tells the reader what was found where a shortened credential does not.
 * Returns 0 when `out` holds something, -1 when the key is absent, is not a
 * string, or is malformed. */
static inline int json_get_display_string(const char *json, const char *key,
                                          char *out, size_t cap)
{
    if (!out || cap == 0) return -1;
    out[0] = '\0';

    const char *body;
    size_t      body_len;
    if (json_string_body(json_find_value(json, key), &body, &body_len) != 0)
        return -1;
    return json_unescape_ex(body, body_len, out, cap, 1) < 0 ? -1 : 0;
}

/* json_get_string() empties the field on ANY failure, so the caller cannot tell
 * capacity from a wrong type or a bad escape. Print which it was; silent when
 * the key is simply absent. */
static inline void json_explain_string_failure(const char *who, const char *json,
                                               const char *key, size_t cap)
{
    const char *p = json_find_value(json, key);
    if (!p) return;

    const char *body;
    size_t      body_len;
    if (json_string_body(p, &body, &body_len) != 0) {
        fprintf(stderr, "[%s] \"%s\" is not a terminated string\n", who, key);
        return;
    }
    /* A truncating decode fails only on malformed text, never on capacity. */
    char probe[1];
    if (json_unescape_ex(body, body_len, probe, sizeof(probe), 1) < 0)
        fprintf(stderr, "[%s] \"%s\" contains a malformed escape\n", who, key);
    else
        fprintf(stderr, "[%s] \"%s\" does not fit (%zu raw bytes into a "
                        "%zu-byte field)\n", who, key, body_len, cap - 1);
}

/* -- Number values -------------------------------------------------------- */

static inline int json_get_long(const char *json, const char *key, long *out)
{
    const char *p = json_find_value(json, key);
    if (!p) return -1;
    if (*p != '-' && (*p < '0' || *p > '9')) return -1;
    *out = strtol(p, NULL, 10);
    return 0;
}

/* -- Container values ----------------------------------------------------- */

/* Copy the balanced `open`…`close` span starting at `p` into a fresh
 * NUL-terminated buffer (caller frees). String literals are skipped whole, so
 * a delimiter inside a JSON string never closes the span. */
static inline char *json_copy_span(const char *p, char open, char close)
{
    const char *e = json_span_end(p, open, close);
    if (!e) return NULL;
    size_t len = (size_t)(e - p);
    char  *dup = (char *)malloc(len + 1);
    if (dup) { memcpy(dup, p, len); dup[len] = '\0'; }
    return dup;
}

/* The `key` object / array as a fresh NUL-terminated buffer (caller frees). */
static inline char *json_get_object(const char *json, const char *key)
{
    return json_copy_span(json_find_value(json, key), '{', '}');
}

static inline char *json_get_array(const char *json, const char *key)
{
    return json_copy_span(json_find_value(json, key), '[', ']');
}

/* json_get_object() restricted to a top-level member (see json_object_find). */
static inline char *json_object_get_object(const char *json, const char *key)
{
    return json_copy_span(json_object_find(json, key), '{', '}');
}

/* First element of the `key` array, when that element is a string. */
static inline int json_array_first_string(const char *json, const char *key,
                                          char *out, size_t cap)
{
    const char *p = json_find_value(json, key);
    if (!p || *p != '[') { if (out && cap) out[0] = '\0'; return -1; }
    p++;
    while (json_is_space(*p)) p++;
    return json_copy_string(p, out, cap);
}

/* First element of the array text `arr` ("[{…},…]"), when that element is an
 * object. Returns a fresh NUL-terminated buffer (caller frees). */
static inline char *json_array_first_object(const char *arr)
{
    if (!arr || *arr != '[') return NULL;
    const char *p = arr + 1;
    while (json_is_space(*p)) p++;
    return json_copy_span(p, '{', '}');
}

/* -- Base64 --------------------------------------------------------------- */

static inline char *b64_decode(const char *encoded, size_t *out_len)
{
    /* Size from the RFC 4648 4:3 ratio instead of the NULL-buffer probe:
     * its "buffer too small" return is MBEDTLS_ERR_BASE64_* in mbedtls 3.x
     * but a PSA status in 4.x, and the header a target compiles against may
     * not be the library it links (a Homebrew /opt/homebrew/include on the
     * include path — e.g. via find_path(opus) — shadows the vendored copy,
     * and the constant then never matches the vendored library's return). */
    size_t elen = strlen(encoded);
    if (elen == 0 || elen % 4 != 0) return NULL;
    size_t dlen = elen / 4 * 3 + 1;
    char *out = (char *)malloc(dlen);
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

/* -- Fixed-size config fields ---------------------------------------------- */

/* Bounded copy into a fixed-size field (e.g. iot_client_config_t's char[32]
 * credentials): a value that does not fit is rejected, not truncated. */
static inline int demo_copy_field(char *dst, size_t cap, const char *src,
                                  const char *what)
{
    size_t n = strlen(src);
    if (n >= cap) {
        fprintf(stderr, "%s is too long (%zu bytes; max %zu)\n", what, n, cap - 1);
        return -1;
    }
    memcpy(dst, src, n);
    dst[n] = '\0';
    return 0;
}

/* -- Session token -> TAI connection params ------------------------------- */

typedef struct {
    char     host[256];
    char     tls_sni[256];
    char     derived_client_id[256];
    char     agent_token[256];
    uint16_t port;
    long     biz_code;
    long     biz_tag;
} tai_conn_params_t;

/* The session token from iot_client_get_session_token() is base64-wrapped JSON
 * (some deployments hand back the JSON directly). Pull out the connect address
 * and the session config. */
static inline int parse_token(const char *raw_token, tai_conn_params_t *p)
{
    memset(p, 0, sizeof(*p));

    char *json = NULL;
    {
        size_t dl = 0;
        char  *decoded = b64_decode(raw_token, &dl);
        if (decoded && dl > 0 && decoded[0] == '{') {
            json = decoded;
        } else {
            free(decoded);
            json = strdup(raw_token);
        }
    }
    if (!json) return -1;

    char *conn = json_get_object(json, "connect_conf");
    if (!conn) {
        fprintf(stderr, "[parse_token] 'connect_conf' not found\n");
        free(json);
        return -1;
    }

    json_array_first_string(conn, "hosts", p->host, sizeof(p->host));
    if (json_array_first_string(conn, "domains", p->tls_sni, sizeof(p->tls_sni)) != 0)
        strncpy(p->tls_sni, p->host, sizeof(p->tls_sni) - 1);

    long port = 0;
    if (json_get_long(conn, "ecc_tls_port", &port) != 0)
        json_get_long(conn, "tcpport", &port);
    if (port > 65535) {
        fprintf(stderr, "[parse_token] port %ld out of range; using 443\n", port);
        port = 0;
    }
    p->port = (port > 0) ? (uint16_t)port : 443;

    /* json_get_string leaves the field EMPTY on any failure; connecting with an
     * empty client id / token fails with nothing pointing back at the token, so
     * name the actual cause here — capacity, wrong type, or a bad escape. */
    if (json_get_string(conn, "derived_client_id",
                        p->derived_client_id, sizeof(p->derived_client_id)) != 0)
        json_explain_string_failure("parse_token", conn, "derived_client_id",
                                    sizeof(p->derived_client_id));
    free(conn);

    char *sess = json_get_object(json, "session_conf");
    if (sess) {
        if (json_get_string(sess, "agentToken",
                            p->agent_token, sizeof(p->agent_token)) != 0)
            json_explain_string_failure("parse_token", sess, "agentToken",
                                        sizeof(p->agent_token));
        char *biz = json_get_object(sess, "bizConfig");
        if (biz) {
            json_get_long(biz, "bizCode", &p->biz_code);
            json_get_long(biz, "bizTag",  &p->biz_tag);
            free(biz);
        }
        free(sess);
    }
    free(json);

    if (p->host[0] == '\0') {
        fprintf(stderr, "[parse_token] Could not extract host\n");
        return -1;
    }
    return 0;
}

#endif /* DEMO_JSON_H */
