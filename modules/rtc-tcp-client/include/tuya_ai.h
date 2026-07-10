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

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * Constants -- Packet types (Section 3)
 * ========================================================================= */
#define TAI_PKT_CLIENT_HELLO             1
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

    /* --- Platform --- */
    const pal_t *pal;

    /* --- Receive callbacks ---
     * All callbacks are invoked from the background receive thread.
     * on_audio: called for each decoded audio chunk (PCM or Opus bytes).
     * on_text:  called for each text fragment; stream_flag is TAI_STREAM_*.
     * on_image: called for each fragment of a received image (e.g. a
     *           cloud-generated picture). data/len is one chunk of the raw
     *           encoded image (typically JPEG); stream_flag is TAI_STREAM_*
     *           (START carries the first bytes, END terminates, ONE_SHOT is a
     *           complete image in a single call). The caller accumulates the
     *           chunks and decodes once the stream ends.
     * on_event: called for all other events (MCPCmd, ChatBreak, ServerVAD...).
     * on_disconnect: called when the server closes the connection.
     * user_data: passed unchanged to every callback.
     */
    void (*on_audio)(tai_ctx_t *ctx,
                     const uint8_t *data, size_t len,
                     uint32_t sample_rate, uint16_t frame_duration,
                     void *user_data);
    void (*on_text) (tai_ctx_t *ctx,
                     const char *text, size_t len,
                     uint8_t stream_flag,
                     void *user_data);
    void (*on_image)(tai_ctx_t *ctx,
                     const uint8_t *data, size_t len,
                     uint8_t stream_flag,
                     void *user_data);
    void (*on_event)(tai_ctx_t *ctx,
                     uint16_t event_type,
                     const uint8_t *data, size_t len,
                     void *user_data);
    void (*on_disconnect)(tai_ctx_t *ctx,
                          uint16_t error_code,
                          void *user_data);
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

/* Disconnect: stop background thread, send SessionClose + ConnectionClose,
 * then close TLS. */
void        tai_disconnect(tai_ctx_t *ctx);

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

/* Send an image + streamed audio in ONE event (multimodal query):
 *   EventStart -> Image(OneShot) -> Audio(START..MIDDLE..END)
 *   -> EventPayloadsEnd -> EventEnd
 * Usage: _start(img+audio params) -> _chunk(pcm) x N -> _end().
 * chunk/end share tai_send_audio_chunk/_end semantics. */
int tai_send_image_audio_start(tai_ctx_t *ctx,
                               const uint8_t *img_data, size_t img_len,
                               uint8_t img_format, uint16_t width, uint16_t height,
                               uint8_t codec, uint8_t channels,
                               uint8_t bit_depth, uint32_t sample_rate);
int tai_send_image_audio_chunk(tai_ctx_t *ctx, const uint8_t *pcm, size_t len);
int tai_send_image_audio_end  (tai_ctx_t *ctx);

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
