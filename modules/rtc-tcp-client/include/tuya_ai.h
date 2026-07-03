/*
 * tuya_ai.h -- Public API for the Tuya AI Foundation 2.1 C library.
 *
 * Single include for library users.  Implement pal.h for your
 * platform, then:
 *
 *   #include "tuya_ai.h"
 *
 *   tai_config_t cfg = { ... };
 *   void *mem = malloc(tai_ctx_size());
 *   tai_ctx_t *ctx = tai_ctx_init(mem, &cfg);
 *   tai_connect(ctx);           // starts background receive thread
 *   tai_send_text(ctx, "Hello", 5);
 *   // ... wait for response callbacks ...
 *   tai_disconnect(ctx);        // stops background thread
 *   tai_ctx_deinit(ctx);
 *   free(mem);
 */

#ifndef TUYA_AI_H
#define TUYA_AI_H

#include <stddef.h>
#include <stdint.h>
#include "pal.h"
#include "log.h"
#include "tls.h"

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * Constants -- Packet types (Section 3)
 * ========================================================================= */
#define TAI_PKT_CLIENT_HELLO             1
#define TAI_PKT_AUTHENTICATE_RESPONSE    3   /* server's auth result for ClientHello */
#define TAI_PKT_PING                     4
#define TAI_PKT_PONG                     5
#define TAI_PKT_CONNECTION_CLOSE         6
#define TAI_PKT_SESSION_NEW              7
#define TAI_PKT_SESSION_CLOSE            8
#define TAI_PKT_CONNECTION_REFRESH_REQ   9
#define TAI_PKT_CONNECTION_REFRESH_RESP  10
#define TAI_PKT_VIDEO                    30
#define TAI_PKT_AUDIO                    31
#define TAI_PKT_IMAGE                    32
#define TAI_PKT_FILE                     33
#define TAI_PKT_TEXT                     34
#define TAI_PKT_EVENT                    35

/* =========================================================================
 * Constants -- Stream flags (Section 5.5)
 * ========================================================================= */
#define TAI_STREAM_ONE_SHOT  0x00
#define TAI_STREAM_START     0x01
#define TAI_STREAM_MIDDLE    0x02
#define TAI_STREAM_END       0x03

/* =========================================================================
 * Constants -- Event types (Section 5.1)
 * ========================================================================= */
#define TAI_EVT_START            0
#define TAI_EVT_PAYLOADS_END     1
#define TAI_EVT_END              2
#define TAI_EVT_ONE_SHOT         3
#define TAI_EVT_CHAT_BREAK       4
#define TAI_EVT_SERVER_VAD       5
#define TAI_EVT_MCP_CMD          1000
#define TAI_EVT_SERVER_TIMEOVER  1001
#define TAI_EVT_UPDATE_CONTEXT   1002

/* =========================================================================
 * Constants -- Client types
 * ========================================================================= */
#define TAI_CLIENT_DEVICE  1
#define TAI_CLIENT_APP     2

/* =========================================================================
 * Constants -- Audio codecs (Section 9.1)
 * ========================================================================= */
#define TAI_AUDIO_PCM   101
#define TAI_AUDIO_OPUS  111

/* =========================================================================
 * Constants -- Image formats (Section 9.3)
 * ========================================================================= */
#define TAI_IMG_JPEG  1
#define TAI_IMG_PNG   2

/* =========================================================================
 * Constants -- Image payload types (Section 9.3)
 * ========================================================================= */
#define TAI_IMG_PAYLOAD_RAW     0   /* raw binary in payload */
#define TAI_IMG_PAYLOAD_BASE64  1   /* base64-encoded in payload */
#define TAI_IMG_PAYLOAD_URL     2   /* URL string in payload */

/* =========================================================================
 * Constants -- Sign levels (Section 4.6)
 * ========================================================================= */
#define TAI_SIGN_NONE        0
#define TAI_SIGN_HMAC_SHA1   1
#define TAI_SIGN_HMAC_SHA256 2

/* =========================================================================
 * Constants -- Protocol versions
 * ========================================================================= */
#define TAI_VER_21  21

/* =========================================================================
 * Constants -- Well-known data IDs (Section 5.6)
 * ========================================================================= */
#define TAI_DATA_ID_AUDIO_UP    1
#define TAI_DATA_ID_AUDIO_DOWN  2
#define TAI_DATA_ID_TEXT_UP     3
#define TAI_DATA_ID_TEXT_DOWN   4
#define TAI_DATA_ID_IMAGE_UP    5
#define TAI_DATA_ID_AUDIO_AUX  7

/* =========================================================================
 * Return codes
 * ========================================================================= */
#define TAI_OK         0
#define TAI_ERR_ARGS  -1
#define TAI_ERR_MEM   -2
#define TAI_ERR_NET   -3
#define TAI_ERR_TLS   -4
#define TAI_ERR_PROTO -5
#define TAI_ERR_HMAC  -6
#define TAI_ERR_AGAIN -7
#define TAI_ERR_CRYPTO -8

/* =========================================================================
 * Attribute
 * ========================================================================= */
typedef struct tai_attr {
    uint16_t       type;
    uint16_t       len;
    const uint8_t *value;
} tai_attr_t;

/* =========================================================================
 * Context forward declaration (full definition in src/tai_internal.h)
 * ========================================================================= */
typedef struct tai_ctx tai_ctx_t;

/* =========================================================================
 * Receive-message structs  (SDK-filled, app read-only)
 *
 * Every receive callback takes ONE const message pointer + user_data. The
 * struct AND every pointer inside it (data/text/event_id) are valid ONLY for
 * the callback's duration — copy to retain. The SDK never heap-allocates these.
 *
 * ABI: the SDK zero-fills the struct before populating; new fields are appended
 * before _reserved. Forward-safe only when the SDK is at least as new as the
 * app; otherwise statically link or version-lock.
 * ========================================================================= */

/* --- Audio --------------------------------------------------------------- */
typedef struct tai_audio_msg {
    const uint8_t *data;            /* Opus frame / PCM bytes; callback-lifetime */
    size_t         len;
    uint8_t        codec;           /* TAI_AUDIO_OPUS / TAI_AUDIO_PCM / 0=unknown */
    uint32_t       sample_rate;     /* Hz, 0 if unknown                          */
    uint16_t       frame_duration;  /* ms per Opus frame                         */
    uint8_t        stream_flag;     /* TAI_STREAM_* (from the media header)       */
    uint16_t       data_id;         /* Data ID: AUDIO_DOWN(2) / AUDIO_AUX(7)      */
    const char    *event_id;        /* turn id, borrowed; "" if none             */
    uint64_t       timestamp_ms;    /* stream-start ts (media header)            */
    uint8_t        _reserved[8];
} tai_audio_msg_t;

/* --- Text ---------------------------------------------------------------- */
typedef struct tai_text_msg {
    const char    *text;            /* UTF-8, NOT NUL-terminated; callback-lifetime */
    size_t         len;
    uint8_t        stream_flag;     /* TAI_STREAM_*                              */
    uint16_t       data_id;         /* Data ID: TAI_DATA_ID_TEXT_DOWN(4)         */
    uint32_t       seq;             /* per-event text seq (varint)               */
    const char    *event_id;        /* turn id, borrowed; "" if none             */
    uint8_t        _reserved[8];
} tai_text_msg_t;

/* --- Image --------------------------------------------------------------- */
/* A received image (e.g. a cloud-generated picture) arrives as a stream of
 * chunks: START (or ONE_SHOT) carries the first bytes and the image-params,
 * MIDDLE continues, END terminates (len may be 0). The caller accumulates the
 * chunks by stream_flag and decodes once the stream ends. format/width/height
 * are populated from image-params on START/ONE_SHOT and 0 on MIDDLE/END. */
typedef struct tai_image_msg {
    const uint8_t *data;            /* encoded image bytes (JPEG/PNG); callback-lifetime */
    size_t         len;
    uint8_t        format;          /* TAI_IMG_JPEG / TAI_IMG_PNG / 0=unknown     */
    uint16_t       width;           /* px, 0 if unknown / not on this chunk       */
    uint16_t       height;          /* px, 0 if unknown                          */
    uint8_t        stream_flag;     /* TAI_STREAM_*                              */
    uint16_t       data_id;         /* Data ID                                   */
    const char    *event_id;        /* turn id, borrowed; "" if none             */
    uint64_t       timestamp_ms;    /* stream-start ts (media header)            */
    uint8_t        _reserved[8];
} tai_image_msg_t;

/* --- Event (generic) ----------------------------------------------------- */
typedef struct tai_event_msg {
    uint16_t       event_type;      /* TAI_EVT_*                                 */
    const uint8_t *data;            /* event payload (often JSON); callback-life */
    size_t         len;
    const char    *event_id;        /* attr 61, borrowed; "" if absent           */
    uint8_t        _reserved[8];
} tai_event_msg_t;

/* --- Disconnect ---------------------------------------------------------- */
#define TAI_DISCONNECT_SESSION_CLOSE     0  /* server SessionClose; link may persist */
#define TAI_DISCONNECT_CONNECTION_CLOSE  1  /* server ConnectionClose; worker stops  */
#define TAI_DISCONNECT_TRANSPORT         2  /* worker-detected transport fault       */
#define TAI_DISCONNECT_PROTOCOL          3  /* fail-fast: parse/behaviour error       */

/* `detail` sub-reason when reason==TRANSPORT */
#define TAI_TRANSPORT_PING_TIMEOUT  1
#define TAI_TRANSPORT_EOF           2
#define TAI_TRANSPORT_NET_ERROR     3
/* `detail` sub-reason when reason==PROTOCOL (which fail-fast check tripped) */
#define TAI_PROTO_ERR_BAD_VERSION   1  /* unknown frame leading byte (desync)       */
#define TAI_PROTO_ERR_HMAC          2  /* frame HMAC mismatch                       */
#define TAI_PROTO_ERR_FRAME_DECODE  3  /* frame header decode failed                */
#define TAI_PROTO_ERR_FRAG          4  /* orphan MIDDLE/LAST, overflow, oversized   */
#define TAI_PROTO_ERR_PKT_DECODE    5  /* application packet / attr block malformed */
#define TAI_PROTO_ERR_UNKNOWN_PKT   6  /* unknown packet type (strict)              */
#define TAI_PROTO_ERR_EVENT         7  /* event unpack failed / unknown event type  */
#define TAI_PROTO_ERR_MEDIA_HDR     8  /* media/text header truncated               */
#define TAI_PROTO_ERR_UNEXPECTED    9  /* valid packet, wrong state (behaviour)     */
#define TAI_PROTO_ERR_OVERSIZED    10  /* inbound frame exceeds rx_buf (see note)   */

typedef struct tai_disconnect_msg {
    uint8_t  reason;                /* TAI_DISCONNECT_*                          */
    uint16_t close_code;            /* server close code (SESSION/CONNECTION); 0 else */
    uint8_t  detail;                /* TAI_TRANSPORT_* / TAI_PROTO_ERR_*; else 0 */
    uint8_t  connection_alive;      /* 1 only for SESSION_CLOSE                   */
    char     session_id[64];        /* value copy; "" if N/A                     */
    uint8_t  _reserved[8];
} tai_disconnect_msg_t;

/* =========================================================================
 * Configuration
 * =========================================================================
 * Fill this struct before calling tai_ctx_init().
 * All pointer fields must remain valid for the lifetime of the context.
 */
typedef struct tai_config {
    /* --- Server (from MQTT protocol-9000 or gateway API) --- */
    const char *host;
    uint16_t    port;
    const char *tls_sni;

    /* --- Identity --- */
    const char *device_id;
    const char *local_key;
    uint8_t     client_type;
    uint8_t     protocol_version;

    /* --- Session / event JSON options (NULL = built-in defaults) --- */
    const char *session_attrs_json;
    const char *event_user_data_json;
    const char *agent_token;

    /* --- Business identifiers --- */
    uint32_t biz_code;
    uint64_t biz_tag;

    /* --- Cryptography --- */
    uint8_t sign_level;

    /* --- Keepalive (0 = default) --- */
    uint32_t ping_interval_ms;    /* 0 = default 60000 */
    uint32_t ping_timeout_ms;     /* 0 = default 90000 */

    /* --- Testing ---
     * When non-zero, skip TLS handshake and send/recv raw bytes over the
     * PAL's tcp_connect/send/recv/close.  For integration tests only.
     */
    uint8_t disable_tls;

    /* --- TLS ---
     * Platform cert-bundle attach callback for peer verification.  NULL -> the
     * TLS layer uses its default verify mode (optional on non-ESP platforms).
     * ESP-IDF users set this to (tls_cert_bundle_attach_fn)esp_crt_bundle_attach.
     */
    tls_cert_bundle_attach_fn cert_bundle_attach;

    /* --- Platform --- */
    const pal_t *pal;

    /* --- Confirmed connect (0 = default 5000 ms) ---
     * Bounds each of tai_connect's two sequential waits: first connection
     * establishment (TCP connect + TLS handshake, sharing one budget), then the
     * server's SessionNew acknowledgement. Either phase failing to complete within
     * this budget fails the connect. Worst-case tai_connect wall time is therefore
     * up to ~2x this value (establishment + ack). */
    uint32_t connect_timeout_ms;

    /* --- Auto-reconnect (connect-on-send), 0 = off (default) ---
     * When non-zero, a stream-STARTING tai_send_*() (tai_send_text, audio_start,
     * image, image_with_text, chat_break, mcp_response) called while the link is
     * down first (re)establishes the connection, then sends; the establish is
     * bounded by connect_timeout_ms and the send returns an error if it can't
     * connect in time (no on_disconnect is fired for connect-on-send failures --
     * the send's return value is the signal). Terminal disconnects are otherwise
     * healed silently. Off keeps the fail-fast model (a send on a down link
     * returns an error and the app reconnects).
     * Mid-stream continuations tai_send_audio_chunk / tai_send_audio_end do NOT
     * auto-reconnect: a fresh session has no open audio event, so they fail-fast
     * on a down link and the app must restart the stream with tai_send_audio_start.
     * Concurrent reconnects are serialised internally, but because the reconnect
     * joins the worker thread an auto-reconnecting send must run on an app thread,
     * never inside a receive callback (see below). */
    uint8_t auto_reconnect;

    /* --- Receive callbacks ---
     * All fire on the background worker thread. Each takes ONE const message
     * pointer + user_data; the msg and every pointer inside it are valid ONLY
     * for the call — copy to retain (see the *_msg_t structs above).
     *
     * A callback MAY call tai_send_*() (no lock is held), but MUST NOT call
     * tai_connect / tai_disconnect / tai_ctx_deinit — those join this very
     * (worker) thread and deadlock. Note: with auto_reconnect enabled a
     * tai_send_*() on a down link ALSO reconnects (join + connect), so a callback
     * must only tai_send_*() on the live connection, never to drive a reconnect.
     * To end the session from a callback, call tai_request_disconnect() and let
     * the owning thread call tai_disconnect().
     * Callbacks run synchronously in the receive loop, so they must return
     * promptly — a blocking callback stalls receiving and keepalive.
     */
    void (*on_audio)     (tai_ctx_t *ctx, const tai_audio_msg_t      *msg, void *user_data);
    void (*on_text)      (tai_ctx_t *ctx, const tai_text_msg_t       *msg, void *user_data);
    void (*on_image)     (tai_ctx_t *ctx, const tai_image_msg_t      *msg, void *user_data);
    void (*on_event)     (tai_ctx_t *ctx, const tai_event_msg_t      *msg, void *user_data);
    void (*on_disconnect)(tai_ctx_t *ctx, const tai_disconnect_msg_t *msg, void *user_data);
    void *user_data;

} tai_config_t;

/* =========================================================================
 * Lifecycle
 * ========================================================================= */

size_t      tai_ctx_size(void);
tai_ctx_t  *tai_ctx_init(void *mem, const tai_config_t *cfg);
void        tai_ctx_deinit(tai_ctx_t *ctx);

/* Connect: TLS handshake + ClientHello + SessionNew + start background thread.
 * The background thread handles receive polling and keepalive pings.
 * Returns TAI_OK or TAI_ERR_*. */
int         tai_connect(tai_ctx_t *ctx);

/* Disconnect: stop and JOIN the background thread, send SessionClose, close
 * TLS, release the connection.
 * MUST be called from a thread OTHER than the receive callbacks: it joins the
 * worker thread, so calling it from inside a callback (which runs on that
 * worker) self-deadlocks. To end the session from a callback, use
 * tai_request_disconnect() and let the owning thread call this. */
void        tai_disconnect(tai_ctx_t *ctx);

/* Request the background worker to stop, WITHOUT joining it. Safe to call from
 * ANY thread, including from inside a receive callback (unlike tai_disconnect).
 * The worker winds down on its next loop; the connection is NOT torn down here.
 * The owning thread must still call tai_disconnect() afterwards to join the
 * worker, send SessionClose, and release resources. */
void        tai_request_disconnect(tai_ctx_t *ctx);

/* =========================================================================
 * Sending data
 * ========================================================================= */

int tai_send_text(tai_ctx_t *ctx, const char *text, size_t len);

int tai_send_audio_start(tai_ctx_t *ctx,
                         uint8_t  codec,
                         uint8_t  channels,
                         uint8_t  bit_depth,
                         uint32_t sample_rate);
int tai_send_audio_chunk(tai_ctx_t *ctx, const uint8_t *pcm, size_t len);
int tai_send_audio_end  (tai_ctx_t *ctx);

int tai_send_image(tai_ctx_t *ctx,
                   const uint8_t *data, size_t len,
                   uint8_t format, uint16_t width, uint16_t height);

int tai_send_image_with_text(tai_ctx_t *ctx,
                             const char *text, size_t text_len,
                             const uint8_t *img_data, size_t img_len,
                             uint8_t format,
                             uint16_t width, uint16_t height);

int tai_chat_break(tai_ctx_t *ctx);

int tai_send_mcp_response(tai_ctx_t *ctx, const char *json_rpc_response);

/* =========================================================================
 * Logging
 *
 * The SDK emits log messages through the global log facade.  Two
 * filters apply:
 *
 *   1. Compile-time maximum (TAI_LOG_LEVEL, default 4 = DEBUG).
 *      Messages above this level are optimised away at build time.
 *   2. Runtime level, set via tai_set_log_level().  Default: TAI_LOG_DEBUG.
 *
 * These thin inlines exist for source-compatibility with callers; new
 * code should prefer log_set_level() / log_get_level() directly.
 *
 * Valid level values are TAI_LOG_ERROR (1) through TAI_LOG_DEBUG (4).
 * Use 0 to disable all logging at runtime.
 * ========================================================================= */
static inline void tai_set_log_level(int level) { log_set_level(level); }
static inline int  tai_get_log_level(void)      { return log_get_level(); }

#ifdef __cplusplus
}
#endif

#endif /* TUYA_AI_H */
