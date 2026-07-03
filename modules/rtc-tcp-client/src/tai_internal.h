/*
 * tai_internal.h — Internal shared definitions for the Tuya AI library.
 *
 * NOT part of the public API.  Include only from src/ translation units.
 */

#ifndef TAI_INTERNAL_H
#define TAI_INTERNAL_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "tuya_ai.h"
#include "pal.h"
#include "log.h"

#include "tls.h"

/* =========================================================================
 * Logging macros — routed through the global log facade.
 *
 * TAI_LOG_LEVEL (compile-time) is the maximum level compiled in.  Messages
 * above this level are removed at build time with zero runtime cost.
 *   0 = none, 1 = error, 2 = +warn, 3 = +info, 4 = +debug (default)
 *
 * Runtime filtering happens inside log_emit() via log_set_level().
 * The tai_set_log_level()/tai_get_log_level() inlines in tuya_ai.h remain
 * available as thin wrappers for backwards compatibility.
 *
 * The leading `pal` argument is preserved for source-compatibility with
 * existing call sites; it is unused at the dispatch layer.  `tag` must be
 * a string literal — it is concatenated into the format string so the
 * log facade itself stays tag-agnostic.
 * ========================================================================= */
#ifndef TAI_LOG_LEVEL
#  define TAI_LOG_LEVEL 4
#endif

#define TAI_LOG_(pal, lvl, tag, fmt, ...) \
    do { (void)(pal); log_emit((lvl), "[" tag "] " fmt, ##__VA_ARGS__); } while (0)

#if TAI_LOG_LEVEL >= 1
#  define TAI_LOGE(pal, tag, fmt, ...) TAI_LOG_(pal, LOG_ERROR, tag, fmt, ##__VA_ARGS__)
#else
#  define TAI_LOGE(pal, tag, ...) ((void)(pal))
#endif

#if TAI_LOG_LEVEL >= 2
#  define TAI_LOGW(pal, tag, fmt, ...) TAI_LOG_(pal, LOG_WARN, tag, fmt, ##__VA_ARGS__)
#else
#  define TAI_LOGW(pal, tag, ...) ((void)(pal))
#endif

#if TAI_LOG_LEVEL >= 3
#  define TAI_LOGI(pal, tag, fmt, ...) TAI_LOG_(pal, LOG_INFO, tag, fmt, ##__VA_ARGS__)
#else
#  define TAI_LOGI(pal, tag, ...) ((void)(pal))
#endif

#if TAI_LOG_LEVEL >= 4
#  define TAI_LOGD(pal, tag, fmt, ...) TAI_LOG_(pal, LOG_DEBUG, tag, fmt, ##__VA_ARGS__)
#else
#  define TAI_LOGD(pal, tag, ...) ((void)(pal))
#endif

/* =========================================================================
 * Buffer-size compile-time knobs
 * Reduce these for memory-constrained targets (e.g. ESP32 without PSRAM).
 *
 * Send path (§6): media packets (audio / image / large text / MCP JSON) are
 * streamed scatter-gather — only a small header is built (tx_hdr_buf) and the
 * caller's payload is signed + sent zero-copy, so there is NO large TX buffer.
 * Control packets (hello, session, event start/end, ping) are still assembled
 * contiguously in tx_ctrl_buf because their attribute block can carry the user
 * session/event JSON (escaped) — which must fit, hence the kilobyte sizing.
 * ========================================================================= */

/* Maximum bytes per transport fragment payload. Used both to fragment OUTBOUND
 * packets and — crucially — advertised to the server in ClientHello as
 * TAI_ATTR_MAX_FRAGMENT_LEN, so the server must not send an inbound fragment
 * whose frame exceeds rx_buf (see TAI_RX_BUF_SIZE). Smaller = less RX RAM but
 * more frames (per-frame 5+sig overhead) for large payloads. */
#ifndef TAI_MAX_FRAGMENT_PAYLOAD
#  define TAI_MAX_FRAGMENT_PAYLOAD  4096U
#endif

/* RX sliding-window buffer. Sized to EXACTLY one maximum wire frame —
 * 5 (frame header) + TAI_MAX_FRAGMENT_PAYLOAD (max fragment) + 32 (max HMAC) —
 * so it is derived, not independently tunable. NOTE: zero headroom. This relies
 * on the server honouring the TAI_ATTR_MAX_FRAGMENT_LEN we advertise: an inbound
 * frame larger than this cannot be assembled, so the receive loop stalls and the
 * connection is torn down by the liveness timeout. There is also no batching —
 * the worker processes at most one max-size frame per recv pass. */
#define TAI_RX_BUF_SIZE   (TAI_MAX_FRAGMENT_PAYLOAD + 37U)

/* Fragment reassembly buffer. A transport-fragmented packet (FRAG_FIRST..LAST)
 * is reassembled here before the whole packet is decoded, so this bounds the
 * largest INBOUND application packet (not fragment): it must be >= the largest
 * downstream packet the server may send (a big Event / MCP-command / context
 * JSON). A packet that reassembles larger is fail-fast (TAI_PROTO_ERR_FRAG).
 * 32000 ≈ 7 max fragments. */
#ifndef TAI_FRAG_BUF_SIZE
#  define TAI_FRAG_BUF_SIZE     32000U
#endif
/* Control-packet assembly buffer. Must hold the largest control application
 * packet — dominated by the session/event JSON escaped into attr 111. The
 * SessionNew / EventStart packet is roughly 2*strlen(JSON) + ~115 bytes of
 * framing/attrs, so the session/event JSON must satisfy that bound or
 * SessionNew/EventStart returns TAI_ERR_MEM. Default 1024 ≈ 4x the largest
 * packet the bundled examples build (~260 B) and fits JSON up to ~700 chars;
 * raise it (e.g. 2048/4096) for richer session configs. */
#ifndef TAI_TX_CTRL_BUF_SIZE
#  define TAI_TX_CTRL_BUF_SIZE   1024U
#endif
/* Scatter-gather header buffer: [5-byte frame header][app header] for one
 * frame. Bounds the application header (pkt byte + attr block + media/text
 * header); the streamed payload is never copied here. */
#ifndef TAI_TX_HDR_BUF_SIZE
#  define TAI_TX_HDR_BUF_SIZE    256U
#endif

/* Maximum attributes decoded from a single packet */
#ifndef TAI_MAX_ATTRS
#  define TAI_MAX_ATTRS  32
#endif

/* Max wall-clock the receive worker spends draining buffered frames before
 * yielding to periodic ping / pong-timeout / shutdown checks. Bounds keepalive
 * and shutdown latency under a sustained downstream flood; any leftover bytes
 * stay buffered and are processed on the next loop iteration. */
#ifndef TAI_DRAIN_BUDGET_MS
#  define TAI_DRAIN_BUDGET_MS  150U
#endif

/* Upper bound on a single idle receive-block in the worker loop. The worker
 * would otherwise block until the next ping is due (up to ping_interval_ms,
 * default 60 s); capping it bounds how long tai_disconnect (running=0) waits for
 * the worker to notice and exit, without depending on the PAL to cap its own
 * recv timeout. Idle cost: the worker wakes ~1000/cap times per second to
 * re-check; it does not affect inbound-data latency (recv returns as soon as
 * bytes arrive). */
#ifndef TAI_WORKER_POLL_CAP_MS
#  define TAI_WORKER_POLL_CAP_MS  2000U
#endif


/* =========================================================================
 * Attribute type codes  (Appendix A)
 * ========================================================================= */
/* Connection */
#define TAI_ATTR_SECURITY_SUIT           10
#define TAI_ATTR_CLIENT_TYPE             11
#define TAI_ATTR_CLIENT_ID               12
#define TAI_ATTR_MAX_FRAGMENT_LEN        15
#define TAI_ATTR_READ_BUFFER_SIZE        16
#define TAI_ATTR_WRITE_BUFFER_SIZE       17
#define TAI_ATTR_PING_INTERVAL           20
#define TAI_ATTR_USERNAME                21
#define TAI_ATTR_PASSWORD                22
#define TAI_ATTR_CONNECTION_ID           23
#define TAI_ATTR_CONNECTION_STATUS_CODE  24
#define TAI_ATTR_LATEST_EXPIRE_TS        25
#define TAI_ATTR_CONNECTION_CLOSE_CODE   31
/* Session */
#define TAI_ATTR_BIZ_CODE                41
#define TAI_ATTR_BIZ_TAG                 42
#define TAI_ATTR_SESSION_ID              43
#define TAI_ATTR_SESSION_STATUS_CODE     44
#define TAI_ATTR_AGENT_TOKEN             45
#define TAI_ATTR_SESSION_CLOSE_CODE      51
/* Event */
#define TAI_ATTR_EVENT_ID                61
#define TAI_ATTR_EVENT_TIMESTAMP         62
#define TAI_ATTR_STREAM_START_TS         63
#define TAI_ATTR_DATA_IDS                64
#define TAI_ATTR_CMD_DATA                65
/* Media metadata */
#define TAI_ATTR_VIDEO_PARAMS            70   /* v2.1 consolidated */
#define TAI_ATTR_VIDEO_CODEC_TYPE        71
#define TAI_ATTR_VIDEO_SAMPLE_RATE       72
#define TAI_ATTR_VIDEO_WIDTH             73
#define TAI_ATTR_VIDEO_HEIGHT            74
#define TAI_ATTR_VIDEO_FPS               75
#define TAI_ATTR_AUDIO_PARAMS            80   /* v2.1 consolidated */
#define TAI_ATTR_AUDIO_CODEC_TYPE        81
#define TAI_ATTR_AUDIO_SAMPLE_RATE       82
#define TAI_ATTR_AUDIO_CHANNELS          83
#define TAI_ATTR_AUDIO_BIT_DEPTH         84
#define TAI_ATTR_IMAGE_PARAMS            90   /* v2.1 consolidated */
#define TAI_ATTR_IMAGE_FORMAT            91
#define TAI_ATTR_IMAGE_WIDTH             92
#define TAI_ATTR_IMAGE_HEIGHT            93
#define TAI_ATTR_IMAGE_PAYLOAD_TYPE      94
#define TAI_ATTR_FILE_PARAMS            100   /* v2.1 consolidated */
#define TAI_ATTR_FILE_FORMAT            101
#define TAI_ATTR_FILE_NAME              102
#define TAI_ATTR_FILE_PAYLOAD_TYPE      103
/* Common */
#define TAI_ATTR_USER_DATA              111
#define TAI_ATTR_CLIENT_TIMESTAMP       113
#define TAI_ATTR_SERVER_TIMESTAMP       114
/* Extended (v2.1 top-level; v2.0 inside user-data) */
#define TAI_ATTR_LANGUAGE              1001
#define TAI_ATTR_PAYLOADS_END_DATA_ID  1002
#define TAI_ATTR_AI_CHAT_USER_DATA     1003
#define TAI_ATTR_SESSION_ATTRIBUTES    1004
#define TAI_ATTR_SUPPORTED_VIDEOS      1005
#define TAI_ATTR_ERR_CODE              1006
#define TAI_ATTR_ERR_MESSAGE           1007

/* Transport fragmentation flags */
#define TAI_FRAG_NONE    0x00
#define TAI_FRAG_FIRST   0x01
#define TAI_FRAG_MIDDLE  0x02
#define TAI_FRAG_LAST    0x03

/* =========================================================================
 * tai_ctx_t — full connection context  (opaque outside src/)
 * ========================================================================= */
struct tai_ctx {
    /* PAL */
    const pal_t *pal;

    /* Connection parameters (pointers into caller-owned storage) */
    const char *host;
    uint16_t    port;
    const char *tls_sni;
    const char *client_id;               /* device_id used as client_id */
    const char *local_key;               /* HKDF IKM */
    const char *device_id;
    const char *session_attrs_json;
    const char *event_user_data_json;
    const char *agent_token;

    /* Protocol configuration */
    uint8_t  proto_ver;                  /* TAI_VER_21 (reserved for future) */
    uint8_t  client_type;                /* TAI_CLIENT_DEVICE / TAI_CLIENT_APP */
    uint32_t biz_code;
    uint64_t biz_tag;
    uint8_t  sign_level;                 /* TAI_SIGN_* */
    uint8_t  sig_len;                    /* 0, 20, or 32 */
    uint8_t  disable_tls;                /* 1 = raw TCP (testing only) */
    tls_cert_bundle_attach_fn cert_bundle_attach; /* platform cert-bundle callback */

    /* Session state */
    uint8_t  connected;
    uint8_t  session_open;
    uint8_t  event_open;
    uint8_t  disconnect_emitted;         /* single-point: terminal on_disconnect fired this connection */
    uint8_t  connecting;                 /* 1 during tai_connect's ack wait: suppress on_disconnect */
    uint16_t seq;                        /* outbound sequence counter */
    uint32_t event_seq;                  /* per-event text sequence counter */
    uint64_t last_pong_ms;               /* updated by dispatch on PONG */
    uint64_t last_rx_ms;                 /* updated on any successful recv  */
    int      session_ack;                /* confirmed-connect: -1=pending, 0=OK, >0=err */
    uint32_t connect_timeout_ms;         /* SessionNew-ack wait in tai_connect */
    char     session_id[64];
    char     event_id[64];



    /* Stored audio params (set by tai_send_audio_start, reused by _chunk) */
    uint8_t  audio_codec;
    uint8_t  audio_channels;
    uint8_t  audio_bit_depth;
    uint8_t  audio_started;
    uint32_t audio_sample_rate;

    /* Received (downstream) audio params — parsed from TTS audio START */
    uint32_t rx_audio_sample_rate;
    uint16_t rx_audio_frame_duration;  /* ms */
    uint16_t rx_audio_frame_size;      /* bytes per Opus packet */
    uint8_t  rx_audio_codec;           /* TAI_AUDIO_* from audio-params f[0]; 0=unknown */

    /* Received turn id (attr 61) backing callback msg->event_id; "" if none */
    char     rx_event_id[64];

    /* Crypto material */
    uint8_t encrypt_random[32];
    uint8_t encrypt_key[32];
    uint8_t sign_key[32];

    /* TLS handle (owned by the shared common/tls module) */
    tls_t *tls;

    /* Raw TCP handle (used when disable_tls=1, testing only) */
    void *raw_tcp;

    /* Mutex protecting ctx state (tx buffers, seq, event flags),
     * serialises sender threads across multi-packet sequences. */
    void *mutex;

    /* Receive callbacks (struct-based; see tuya_ai.h) */
    void (*on_audio)     (tai_ctx_t *, const tai_audio_msg_t      *, void *);
    void (*on_text)      (tai_ctx_t *, const tai_text_msg_t       *, void *);
    void (*on_event)     (tai_ctx_t *, const tai_event_msg_t      *, void *);
    void (*on_disconnect)(tai_ctx_t *, const tai_disconnect_msg_t *, void *);
    void *user_data;

    /* RX linear buffer (sliding-window: bytes always at buf[0]) */
    uint8_t rx_buf[TAI_RX_BUF_SIZE];
    size_t  rx_len;

    /* Fragment reassembly */
    uint8_t frag_buf[TAI_FRAG_BUF_SIZE];
    size_t  frag_len;
    uint8_t frag_state;                  /* 0=idle, 1=assembling */

    /* TX buffers (§6). Every packet — control and media — streams scatter-
     * gather through send_app_sg: tx_hdr_buf holds the [5-byte frame header]
     * [optional app header] for one merged write; the payload is signed and
     * sent zero-copy from the caller's buffer; tx_sig holds the per-frame
     * signature computed before any byte goes on the wire. tx_ctrl_buf
     * assembles a control packet (its attribute block can carry the user
     * session/event JSON) which is then sent as that zero-copy payload. */
    uint8_t tx_ctrl_buf[TAI_TX_CTRL_BUF_SIZE];
    uint8_t tx_hdr_buf[TAI_TX_HDR_BUF_SIZE];
    uint8_t tx_sig[32];

    /* Background worker thread (auto-started by tai_connect) */
    void           *thread_handle;
    volatile int    running;
    uint64_t        last_ping_ms;
    uint32_t        ping_interval_ms;
    uint32_t        ping_timeout_ms;
};

/* =========================================================================
 * Big-endian read/write helpers (inline, no overhead)
 * ========================================================================= */
static inline uint16_t tai_r16(const uint8_t *p) {
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}
static inline uint32_t tai_r32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16)
         | ((uint32_t)p[2] <<  8) |  (uint32_t)p[3];
}
static inline uint64_t tai_r64(const uint8_t *p) {
    return ((uint64_t)p[0] << 56) | ((uint64_t)p[1] << 48)
         | ((uint64_t)p[2] << 40) | ((uint64_t)p[3] << 32)
         | ((uint64_t)p[4] << 24) | ((uint64_t)p[5] << 16)
         | ((uint64_t)p[6] <<  8) |  (uint64_t)p[7];
}
static inline void tai_w16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v;
}
static inline void tai_w32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >>  8); p[3] = (uint8_t)v;
}
static inline void tai_w64(uint8_t *p, uint64_t v) {
    p[0] = (uint8_t)(v >> 56); p[1] = (uint8_t)(v >> 48);
    p[2] = (uint8_t)(v >> 40); p[3] = (uint8_t)(v >> 32);
    p[4] = (uint8_t)(v >> 24); p[5] = (uint8_t)(v >> 16);
    p[6] = (uint8_t)(v >>  8); p[7] = (uint8_t)v;
}

/* =========================================================================
 * Attribute builder helpers (inline convenience for protocol.c)
 * ========================================================================= */
static inline tai_attr_t tai_attr_u8v(uint16_t type, uint8_t *s, uint8_t v) {
    s[0] = v;
    return (tai_attr_t){ type, 1, s };
}
static inline tai_attr_t tai_attr_u16v(uint16_t type, uint8_t *s, uint16_t v) {
    tai_w16(s, v);
    return (tai_attr_t){ type, 2, s };
}
static inline tai_attr_t tai_attr_u32v(uint16_t type, uint8_t *s, uint32_t v) {
    tai_w32(s, v);
    return (tai_attr_t){ type, 4, s };
}
static inline tai_attr_t tai_attr_u64v(uint16_t type, uint8_t *s, uint64_t v) {
    tai_w64(s, v);
    return (tai_attr_t){ type, 8, s };
}
static inline tai_attr_t tai_attr_strv(uint16_t type, const char *str) {
    return (tai_attr_t){ type, (uint16_t)strlen(str), (const uint8_t *)str };
}
static inline tai_attr_t tai_attr_bytesv(uint16_t type,
                                          const uint8_t *b, uint16_t len) {
    return (tai_attr_t){ type, len, b };
}

/* =========================================================================
 * Internal function declarations
 * =========================================================================
 * tai_attrs.c
 */
int               tai_attrs_encode_block(uint8_t proto_ver,
                                          const tai_attr_t *attrs, int count,
                                          uint8_t *buf, size_t buf_size);
int               tai_attrs_decode_block(uint8_t proto_ver,
                                          const uint8_t *buf, size_t buf_len,
                                          tai_attr_t *out, int max_count,
                                          int *count_out);
const tai_attr_t *tai_attr_find(const tai_attr_t *attrs, int count,
                                 uint16_t type);
uint8_t           tai_attr_u8 (const tai_attr_t *a);
uint16_t          tai_attr_u16(const tai_attr_t *a);
uint32_t          tai_attr_u32(const tai_attr_t *a);
uint64_t          tai_attr_u64(const tai_attr_t *a);
const char       *tai_attr_str(const tai_attr_t *a); /* NUL-terminated in value */

/*
 * tai_packet.c
 */
int tai_packet_encode(uint8_t proto_ver, uint8_t pkt_type,
                       const tai_attr_t *attrs, int attr_count,
                       const uint8_t *payload, size_t payload_len,
                       uint8_t *buf, size_t buf_size);
int tai_packet_decode(uint8_t proto_ver,
                       const uint8_t *buf, size_t buf_len,
                       uint8_t *pkt_type_out,
                       tai_attr_t *attrs_out, int max_attrs, int *attr_count_out,
                       const uint8_t **payload_out, size_t *payload_len_out);
int tai_pack_event(uint8_t proto_ver, uint16_t event_type,
                    const uint8_t *data, size_t data_len,
                    uint8_t *buf, size_t buf_size);
int tai_unpack_event(uint8_t proto_ver,
                      const uint8_t *buf, size_t buf_len,
                      uint16_t *evt_type_out,
                      const uint8_t **data_out, size_t *data_len_out);
int tai_pack_media_hdr(uint8_t proto_ver, uint16_t data_id,
                        uint8_t stream_flag, uint64_t ts_ms,
                        uint8_t *buf, size_t buf_size);
int tai_unpack_media_hdr(const uint8_t *buf, size_t len,
                         uint16_t *data_id, uint8_t *stream_flag, uint64_t *ts_ms);

/* Build a tai_disconnect_msg_t (copying ctx->session_id, setting
 * connection_alive for SESSION_CLOSE) and fire ctx->on_disconnect once.
 * Shared by the worker (single-point) and dispatch (SESSION_CLOSE, inline). */
void tai_emit_disconnect(tai_ctx_t *ctx, uint8_t reason,
                         uint8_t detail, uint16_t close_code);

/* Fail-fast cause, RETURNED up the receive chain (dispatch -> process_app_packet
 * -> tai_process_rx) to the worker, which owns the run loop and fires a single
 * on_disconnect. Lower layers never touch connection-lifecycle state:
 *   TAI_OK (0)                     no fatal — keep going
 *   1 .. 0xFFFF (TAI_PROTO_ERR_*)  PROTOCOL fail-fast detail
 *   TAI_RX_PEER_CLOSE | code       server CONNECTION_CLOSE; low 16 bits = close code
 */
#define TAI_RX_PEER_CLOSE  0x10000
int tai_pack_text_hdr(uint8_t proto_ver, uint16_t data_id,
                       uint8_t stream_flag, uint32_t seq,
                       uint8_t *buf, size_t buf_size);
int tai_varint_encode(uint32_t v, uint8_t *buf, size_t buf_size);
int tai_varint_decode(const uint8_t *buf, size_t len,
                       uint32_t *v_out, size_t *consumed_out);

/*
 * tai_transport.c
 */
int    tai_frame_detect_version(uint8_t first_byte);
size_t tai_frame_total_size(const uint8_t *buf, size_t available);
int    tai_frame_encode(uint8_t frag_flag, uint16_t sequence,
                         const uint8_t *payload, size_t payload_len,
                         const uint8_t sign_key[32], uint8_t sig_len,
                         const pal_t *pal,
                         uint8_t *out_buf, size_t out_size);
int    tai_frame_decode(const uint8_t *buf, size_t buf_len,
                         uint8_t sig_len,
                         uint8_t *frag_flag_out, uint16_t *seq_out,
                         const uint8_t **payload_out, size_t *payload_len_out);
int    tai_frame_verify(const uint8_t *raw_frame, size_t frame_len,
                         uint8_t sig_len,
                         const uint8_t sign_key[32],
                         const pal_t *pal);

/* A scatter-gather segment. The logical byte stream of a (fragment of a) frame
 * is the concatenation segs[0] || segs[1] || … — used by the streaming send
 * path to sign and emit a frame without first assembling it contiguously. */
typedef struct { const uint8_t *p; size_t len; } tai_seg_t;

/* HMAC (Section 6.4) over the logical concatenation of segs[0..nseg). Produces
 * byte-for-byte the same signature as signing that concatenation contiguously:
 * total <= 64 signs the whole stream, else signs logical [0,32) + [total-32,
 * total). out_sig must have room for sig_len bytes. */
int    tai_frame_hmac_sg(const tai_seg_t *segs, int nseg,
                         const uint8_t sign_key[32], uint8_t sig_len,
                         uint8_t *out_sig);

/*
 * tai_crypto.c
 */
int tai_hmac_sha256(const uint8_t *key, size_t key_len,
                    const uint8_t *data, size_t data_len,
                    uint8_t out[32]);
int tai_hkdf_sha256(const uint8_t *ikm, size_t ikm_len,
                    const uint8_t *salt, size_t salt_len,
                    uint8_t *out, size_t out_len);
int tai_random_bytes(const pal_t *pal, uint8_t *buf, size_t len);
int tai_crypto_derive_keys(uint8_t proto_ver,
                            const uint8_t *ikm,  size_t ikm_len,
                            const uint8_t *encrypt_random, size_t rand_len,
                            uint8_t out_encrypt_key[32],
                            uint8_t out_sign_key[32],
                            const pal_t *pal);

/*
 * tai_pkt_log.c
 */
void tai_log_packet(const pal_t *pal, uint8_t proto_ver,
                    int is_send,
                    uint8_t pkt_type,
                    const tai_attr_t *attrs, int attr_count,
                    const uint8_t *payload, size_t payload_len);

/*
 * tai_protocol.c
 */
int tai_proto_build_client_hello(tai_ctx_t *ctx,
                                  uint8_t *buf, size_t buf_size);
int tai_proto_build_session_new  (tai_ctx_t *ctx,
                                  uint8_t *buf, size_t buf_size);
int tai_proto_build_session_close(tai_ctx_t *ctx,
                                  uint8_t *buf, size_t buf_size);
int tai_proto_build_event_start  (tai_ctx_t *ctx,
                                  uint8_t *buf, size_t buf_size);
int tai_proto_build_event_payloads_end(tai_ctx_t *ctx, uint16_t data_id,
                                       uint8_t *buf, size_t buf_size);
int tai_proto_build_event_end    (tai_ctx_t *ctx,
                                  uint8_t *buf, size_t buf_size);
int tai_proto_build_event_chat_break(tai_ctx_t *ctx,
                                      uint8_t *buf, size_t buf_size);
/* Header-only builders for the scatter-gather send path (§6): each writes only
 * [pkt byte][attr block][media/text header] into buf; the large payload (pcm /
 * text / image / json) stays in the caller's buffer and is streamed separately
 * by send_app_sg — no payload copy, no malloc. */
int tai_proto_build_audio_hdr(tai_ctx_t *ctx,
                              uint16_t data_id, uint8_t stream_flag,
                              uint8_t codec, uint8_t channels,
                              uint8_t bit_depth, uint32_t sample_rate,
                              size_t pcm_len,
                              uint8_t *buf, size_t buf_size);
int tai_proto_build_text_hdr(tai_ctx_t *ctx,
                             uint16_t data_id, uint8_t stream_flag, uint32_t seq,
                             uint8_t *buf, size_t buf_size);
int tai_proto_build_image_hdr(tai_ctx_t *ctx,
                              uint16_t data_id, uint8_t stream_flag,
                              uint8_t format, uint16_t width, uint16_t height,
                              uint8_t *buf, size_t buf_size);
int tai_proto_build_mcp_hdr(tai_ctx_t *ctx, uint8_t *buf, size_t buf_size);
int tai_proto_build_ping (tai_ctx_t *ctx, uint8_t *buf, size_t buf_size);
int tai_proto_dispatch   (tai_ctx_t *ctx,
                           uint8_t pkt_type,
                           const tai_attr_t *attrs, int attr_count,
                           const uint8_t *payload, size_t payload_len);

/* Internal sequence helper */
static inline uint16_t tai_next_seq(tai_ctx_t *ctx) {
    ctx->seq++;
    if (ctx->seq == 0) ctx->seq = 1;
    return ctx->seq;
}

/* =========================================================================
 * Enum -> name helpers (for log readability).
 *
 * Returned strings are static; never return NULL.  Unknown values fall back
 * to "?" so callers can still log the numeric value alongside.
 * ========================================================================= */
static inline const char *tai_pkt_type_name(uint8_t t) {
    switch (t) {
    case TAI_PKT_CLIENT_HELLO:             return "ClientHello";
    case TAI_PKT_AUTHENTICATE_RESPONSE:    return "AuthResp";
    case TAI_PKT_PING:                     return "Ping";
    case TAI_PKT_PONG:                     return "Pong";
    case TAI_PKT_CONNECTION_CLOSE:         return "ConnClose";
    case TAI_PKT_SESSION_NEW:              return "SessionNew";
    case TAI_PKT_SESSION_CLOSE:            return "SessionClose";
    case TAI_PKT_CONNECTION_REFRESH_REQ:   return "ConnRefreshReq";
    case TAI_PKT_CONNECTION_REFRESH_RESP:  return "ConnRefreshResp";
    case TAI_PKT_VIDEO:                    return "Video";
    case TAI_PKT_AUDIO:                    return "Audio";
    case TAI_PKT_IMAGE:                    return "Image";
    case TAI_PKT_FILE:                     return "File";
    case TAI_PKT_TEXT:                     return "Text";
    case TAI_PKT_EVENT:                    return "Event";
    default:                               return "?";
    }
}

static inline const char *tai_event_type_name(uint16_t t) {
    switch (t) {
    case TAI_EVT_START:            return "Start";
    case TAI_EVT_PAYLOADS_END:     return "PayloadsEnd";
    case TAI_EVT_END:              return "End";
    case TAI_EVT_ONE_SHOT:         return "OneShot";
    case TAI_EVT_CHAT_BREAK:       return "ChatBreak";
    case TAI_EVT_SERVER_VAD:       return "ServerVAD";
    case TAI_EVT_MCP_CMD:          return "MCPCmd";
    case TAI_EVT_SERVER_TIMEOVER:  return "ServerTimeover";
    case TAI_EVT_UPDATE_CONTEXT:   return "UpdateContext";
    default:                       return "?";
    }
}

static inline const char *tai_stream_flag_name(uint8_t f) {
    switch (f) {
    case TAI_STREAM_ONE_SHOT: return "OneShot";
    case TAI_STREAM_START:    return "Start";
    case TAI_STREAM_MIDDLE:   return "Middle";
    case TAI_STREAM_END:      return "End";
    default:                  return "?";
    }
}

static inline const char *tai_client_type_name(uint8_t t) {
    switch (t) {
    case TAI_CLIENT_DEVICE: return "Device";
    case TAI_CLIENT_APP:    return "App";
    default:                return "?";
    }
}

static inline const char *tai_sign_level_name(uint8_t s) {
    switch (s) {
    case TAI_SIGN_NONE:        return "none";
    case TAI_SIGN_HMAC_SHA1:   return "HMAC-SHA1";
    case TAI_SIGN_HMAC_SHA256: return "HMAC-SHA256";
    default:                   return "?";
    }
}

static inline const char *tai_frag_flag_name(uint8_t f) {
    switch (f) {
    case TAI_FRAG_NONE:   return "none";
    case TAI_FRAG_FIRST:  return "first";
    case TAI_FRAG_MIDDLE: return "middle";
    case TAI_FRAG_LAST:   return "last";
    default:              return "?";
    }
}

#endif /* TAI_INTERNAL_H */
