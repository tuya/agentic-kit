/*
 * tai_config_defaults.h -- rtc-tcp-client (TAI 2.1) build-time knobs:
 * buffer sizing and worker scheduling. Lives beside the module's public
 * headers; module sources include it directly (src/tai_internal.h).
 *
 * It pulls common/log.h FIRST: that header is where integrator overrides
 * (agentic_kit_config.h on the include path, -D, AGENTIC_KIT_USER_CONFIG)
 * are applied -- so overrides win over every default below. Not a public
 * API header.
 */

#ifndef AGENTIC_KIT_TAI_CONFIG_DEFAULTS_H
#define AGENTIC_KIT_TAI_CONFIG_DEFAULTS_H

#include "log.h"

/* =========================================================================
 * rtc-tcp-client / TAI 2.1 (src/tai_internal.h, src/tai_pkt_log.c)
 * =========================================================================
 * Buffer-size compile-time knobs: reduce these for memory-constrained
 * targets (e.g. ESP32 without PSRAM).
 *
 * Send path (§6): media packets (audio / image / large text / MCP JSON) are
 * streamed scatter-gather -- only a small header is built (tx_hdr_buf) and
 * the caller's payload is signed + sent zero-copy, so there is NO large TX
 * buffer. Control packets (hello, session, event start/end, ping) are still
 * assembled contiguously in tx_ctrl_buf because their attribute block can
 * carry the user session/event JSON (escaped) -- which must fit, hence the
 * kilobyte sizing.
 */

/* Sample 1-in-N media MIDDLE frames to INFO (tai_pkt_log.c). All
 * non-sampled MIDDLE frames are dropped (no log line). Set to 0 to disable
 * sampling, in which case every MIDDLE frame logs at DEBUG instead. */
#ifndef AGENTIC_KIT_TAI_LOG_MEDIA_SAMPLE_N
#define AGENTIC_KIT_TAI_LOG_MEDIA_SAMPLE_N 50
#endif

/* Maximum bytes per transport fragment payload. Used both to fragment
 * OUTBOUND packets and -- crucially -- advertised to the server in
 * ClientHello as TAI_ATTR_MAX_FRAGMENT_LEN, so the server must not send an
 * inbound fragment whose frame exceeds rx_buf (see TAI_RX_BUF_SIZE, derived
 * in tai_internal.h). Smaller = less RX RAM but more frames (per-frame
 * 5+sig overhead) for large payloads. */
#ifndef AGENTIC_KIT_TAI_MAX_FRAGMENT_PAYLOAD
#define AGENTIC_KIT_TAI_MAX_FRAGMENT_PAYLOAD  4096U
#endif

/* Fragment reassembly buffer. A transport-fragmented packet (FRAG_FIRST..LAST)
 * is reassembled here before the whole packet is decoded, so this bounds the
 * largest INBOUND application packet (not fragment): it must be >= the
 * largest downstream packet the server may send (a big Event / MCP-command /
 * context JSON). A packet that reassembles larger is fail-fast
 * (TAI_PROTO_ERR_FRAG). 32000 ~= 7 max fragments. */
#ifndef AGENTIC_KIT_TAI_FRAG_BUF_SIZE
#define AGENTIC_KIT_TAI_FRAG_BUF_SIZE     32000U
#endif

/* Scatter-gather header buffer: [5-byte frame header][app header] for one
 * frame. Bounds the application header (pkt byte + attr block + media/text
 * header); the streamed payload is never copied here. */
#ifndef AGENTIC_KIT_TAI_TX_HDR_BUF_SIZE
#define AGENTIC_KIT_TAI_TX_HDR_BUF_SIZE    256U
#endif

/* Small-frame coalesce threshold: a whole frame (frame hdr + app hdr +
 * payload + signature) STRICTLY smaller than this is copied into
 * tx_ctrl_buf and sent as one transport write (one TLS record instead of
 * 2-3); at or above it the frame keeps the zero-copy scatter-gather path.
 * The send path also caps coalescing at AGENTIC_KIT_TAI_TX_CTRL_BUF_SIZE, so shrinking
 * either knob is safe -- it only narrows the size window that gets
 * coalesced. */
#ifndef AGENTIC_KIT_TAI_FRAME_COALESCE_LIMIT
#define AGENTIC_KIT_TAI_FRAME_COALESCE_LIMIT 512U
#endif

/* Control-packet assembly buffer. Must hold the largest control application
 * packet -- dominated by the session/event JSON escaped into attr 111. The
 * SessionNew / EventStart packet is roughly 2*strlen(JSON) + ~115 bytes of
 * framing/attrs, so the session/event JSON must satisfy that bound or
 * SessionNew/EventStart returns TAI_ERR_MEM. Default 1024 ~= 4x the largest
 * packet the bundled examples build (~260 B) and fits JSON up to ~700 chars;
 * raise it (e.g. 2048/4096) for richer session configs. It doubles as the
 * small-frame coalesce scratch (see AGENTIC_KIT_TAI_FRAME_COALESCE_LIMIT) -- a control
 * packet is shifted in place inside the same buffer, never copied out. */
#ifndef AGENTIC_KIT_TAI_TX_CTRL_BUF_SIZE
#define AGENTIC_KIT_TAI_TX_CTRL_BUF_SIZE   1024U
#endif

/* Maximum attributes decoded from a single packet. */
#ifndef AGENTIC_KIT_TAI_MAX_ATTRS
#define AGENTIC_KIT_TAI_MAX_ATTRS  32
#endif

/* Max wall-clock the receive worker spends draining buffered frames before
 * yielding to periodic ping / pong-timeout / shutdown checks. Bounds
 * keepalive and shutdown latency under a sustained downstream flood; any
 * leftover bytes stay buffered and are processed on the next loop
 * iteration. */
#ifndef AGENTIC_KIT_TAI_DRAIN_BUDGET_MS
#define AGENTIC_KIT_TAI_DRAIN_BUDGET_MS  150U
#endif

/* Upper bound on a single idle receive-block in the worker loop. The worker
 * would otherwise block until the next ping is due (up to ping_interval_ms,
 * default 60 s); capping it bounds how long tai_disconnect (running=0)
 * waits for the worker to notice and exit, without depending on the PAL to
 * cap its own recv timeout. Idle cost: the worker wakes ~1000/cap times per
 * second to re-check; it does not affect inbound-data latency (recv returns
 * as soon as bytes arrive). */
#ifndef AGENTIC_KIT_TAI_WORKER_POLL_CAP_MS
#define AGENTIC_KIT_TAI_WORKER_POLL_CAP_MS  2000U
#endif

#endif /* AGENTIC_KIT_TAI_CONFIG_DEFAULTS_H */
