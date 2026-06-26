/*
 * tai_transport.c — Transport frame encoding, decoding, HMAC, fragmentation.
 *
 * Implements Section 2 and 6.4 of the Tuya AI Foundation 2.1 spec.
 *
 * v2.1 frame layout (5-byte header):
 *   [flags:1][seq:2BE][length:2BE][payload:N][signature:0/20/32]
 *
 *   flags byte: [FF:2][RR:2][VVVV:4]
 *     FF   = frag_flag  (bits 7-6)
 *     RR   = reserved   (bits 5-4), always 0x02 when encoding
 *     VVVV = version    (bits 3-0), value 0x02 for v2.1
 *
 *   length = sizeof(payload) + sizeof(signature)
 *
 * HMAC input (Section 6.4):
 *   if (header + payload) <= 64 bytes: sign all
 *   else: sign first 32 bytes (header+start of payload) + last 32 bytes of payload
 */

#include "tai_internal.h"

#define TAG "transport"

/* =========================================================================
 * tai_frame_detect_version
 *
 * Inspect the first byte of a received buffer.
 * Returns TAI_VER_21 or TAI_ERR_PROTO (unsupported/unknown).
 * ========================================================================= */
int tai_frame_detect_version(uint8_t first_byte)
{
    if ((first_byte & 0x0F) == 0x02) return TAI_VER_21;
    return TAI_ERR_PROTO;
}

/* =========================================================================
 * tai_frame_total_size
 *
 * Return the expected total frame size (header + payload + signature) given
 * at least a partial buffer.  Returns 0 if < minimum header bytes available.
 * ========================================================================= */
size_t tai_frame_total_size(const uint8_t *buf, size_t available)
{
    if (available < 5) return 0;            /* need 5 bytes for v2.1 header */

    /* v2.1: length field at bytes [3..4] = payload+sig length */
    uint16_t length = tai_r16(buf + 3);
    return (size_t)(5 + length);
}

/* =========================================================================
 * HMAC over a frame's logical byte stream  (Section 6.4)
 *
 * tai_frame_hmac_sg signs the logical concatenation of segs[0..nseg) without
 * assembling it contiguously, so the streaming send path can sign a frame whose
 * header and payload live in separate buffers. It is byte-for-byte identical to
 * signing that concatenation: total <= 64 signs the whole stream; otherwise the
 * spec samples the first 32 and last 32 bytes of the WHOLE logical stream — the
 * cut never distinguishes header from payload, which is what keeps it correct
 * when an application header is >= 27 bytes or a payload slice is < 32 bytes.
 * out_sig must point to sig_len writable bytes.
 * ========================================================================= */

/* Copy the logical byte range [start, start+count) of the segment list into
 * dst[0..count). Callers always request a range fully covered by the segments. */
static void sg_copy_range(const tai_seg_t *segs, int nseg,
                          size_t start, size_t count, uint8_t *dst)
{
    size_t end = start + count;
    size_t lo  = 0;                       /* logical offset of segs[i] start */
    for (int i = 0; i < nseg; i++) {
        size_t hi = lo + segs[i].len;     /* logical offset of segs[i] end   */
        size_t a  = lo > start ? lo : start;
        size_t b  = hi < end   ? hi : end;
        if (a < b)
            memcpy(dst + (a - start), segs[i].p + (a - lo), b - a);
        lo = hi;
    }
}

int tai_frame_hmac_sg(const tai_seg_t *segs, int nseg,
                      const uint8_t sign_key[32], uint8_t sig_len,
                      uint8_t *out_sig)
{
    size_t total = 0;
    for (int i = 0; i < nseg; i++) total += segs[i].len;

    uint8_t data[64];
    size_t  data_len;

    if (total <= 64) {
        sg_copy_range(segs, nseg, 0, total, data);
        data_len = total;
    } else {
        sg_copy_range(segs, nseg, 0,          32, data);        /* first 32 */
        sg_copy_range(segs, nseg, total - 32, 32, data + 32);   /* last  32 */
        data_len = 64;
    }

    uint8_t full[32];
    int rc = tai_hmac_sha256(sign_key, 32, data, data_len, full);
    if (rc != 0) return TAI_ERR_CRYPTO;
    memcpy(out_sig, full, sig_len);
    return TAI_OK;
}

/* Two-segment [header, payload] wrapper — the contiguous encode/verify paths
 * sign through this, so every path shares one algorithm.
 * header_len must be 5 for v2.1; out_sig must hold sig_len bytes. */
static int frame_hmac(const uint8_t *header,  size_t header_len,
                       const uint8_t *payload, size_t payload_len,
                       const uint8_t sign_key[32], uint8_t sig_len,
                       uint8_t *out_sig)
{
    tai_seg_t segs[2] = { { header, header_len }, { payload, payload_len } };
    return tai_frame_hmac_sg(segs, 2, sign_key, sig_len, out_sig);
}

/* =========================================================================
 * tai_frame_encode
 *
 * Encode a v2.1 transport frame into out_buf.
 * If sig_len == 0 (e.g. ClientHello), no HMAC is appended.
 * Returns total frame bytes written, or TAI_ERR_*.
 * ========================================================================= */
int tai_frame_encode(uint8_t frag_flag, uint16_t sequence,
                     const uint8_t *payload, size_t payload_len,
                     const uint8_t sign_key[32], uint8_t sig_len,
                     const pal_t *pal,
                     uint8_t *out_buf, size_t out_size)
{
    if (!out_buf || !payload) return TAI_ERR_ARGS;

    size_t frame_len = 5 + payload_len + sig_len;
    if (frame_len > out_size)             return TAI_ERR_MEM;
    if (payload_len + sig_len > 0xFFFFU)  return TAI_ERR_ARGS;

    /* Flags byte: [frag_flag:2][0x02:2][0x02:4] */
    out_buf[0] = (uint8_t)(((frag_flag & 0x03) << 6) | (0x02 << 4) | 0x02);
    tai_w16(out_buf + 1, sequence);
    tai_w16(out_buf + 3, (uint16_t)(payload_len + sig_len));
    memcpy(out_buf + 5, payload, payload_len);

    if (sig_len > 0) {
        int rc = frame_hmac(out_buf, 5, payload, payload_len,
                             sign_key, sig_len,
                             out_buf + 5 + payload_len);
        if (rc != TAI_OK) {
            TAI_LOGE(pal, TAG, "frame_encode: HMAC compute failed");
            return rc;
        }
    }

    TAI_LOGD(pal, TAG, "frame_encode: frag=%s seq=%u payload=%zu total=%zu",
             tai_frag_flag_name(frag_flag), sequence, payload_len, frame_len);
    return (int)frame_len;
}

/* =========================================================================
 * tai_frame_decode
 *
 * Parse a complete v2.1 frame from buf (must be exactly tai_frame_total_size()).
 * *payload_out and *payload_len_out describe the payload portion (excl. sig).
 * sig_len is from the connection context (not encoded in the v2.1 header).
 * Returns TAI_OK or TAI_ERR_*.
 * ========================================================================= */
int tai_frame_decode(const uint8_t *buf, size_t buf_len,
                     uint8_t sig_len,
                     uint8_t *frag_flag_out, uint16_t *seq_out,
                     const uint8_t **payload_out, size_t *payload_len_out)
{
    if (!buf || buf_len < 5) return TAI_ERR_PROTO;

    uint16_t length = tai_r16(buf + 3);     /* payload + sig */
    if ((size_t)(5 + length) > buf_len)    return TAI_ERR_PROTO;
    if (length < sig_len)                  return TAI_ERR_PROTO;

    if (frag_flag_out)   *frag_flag_out   = (buf[0] >> 6) & 0x03;
    if (seq_out)         *seq_out         = tai_r16(buf + 1);
    if (payload_out)     *payload_out     = buf + 5;
    if (payload_len_out) *payload_len_out = (size_t)(length - sig_len);

    return TAI_OK;
}

/* =========================================================================
 * tai_frame_verify
 *
 * Verify the HMAC signature of a fully-received frame.
 * If sig_len == 0, returns TAI_OK immediately.
 * Returns TAI_OK on success, TAI_ERR_HMAC on mismatch.
 * ========================================================================= */
int tai_frame_verify(const uint8_t *raw_frame, size_t frame_len,
                     uint8_t sig_len,
                     const uint8_t sign_key[32],
                     const pal_t *pal)
{
    if (sig_len == 0) return TAI_OK;
    if (!raw_frame || frame_len < (size_t)(5 + sig_len)) return TAI_ERR_PROTO;

    uint16_t length     = tai_r16(raw_frame + 3);
    size_t   payload_len = (size_t)(length - sig_len);
    const uint8_t *sig   = raw_frame + 5 + payload_len;

    uint8_t computed[32];
    int rc = frame_hmac(raw_frame, 5,
                        raw_frame + 5, payload_len,
                        sign_key, sig_len, computed);
    if (rc != TAI_OK) return rc;

    /* Constant-time comparison */
    uint8_t diff = 0;
    for (uint8_t i = 0; i < sig_len; i++)
        diff |= computed[i] ^ sig[i];

    if (diff != 0) {
        TAI_LOGW(pal, TAG, "HMAC verify failed (frame_len=%zu sig_len=%u)", frame_len, sig_len);
    }
    return (diff == 0) ? TAI_OK : TAI_ERR_HMAC;
}

