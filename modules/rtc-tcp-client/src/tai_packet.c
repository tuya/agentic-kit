/*
 * tai_packet.c — Application packet encoding and decoding.
 *
 * Implements Section 3 and 5 of the Tuya AI Foundation 2.1 spec.
 *
 * Application packet layout:
 *   [header_byte:1]
 *   [attr_block (if attr_flag set)]
 *   [payload (remaining bytes to end)]
 *
 * Header byte: bits [7:1] = packet_type, bit 0 = attrs_present
 */

#include "tai_internal.h"

/* =========================================================================
 * tai_packet_encode
 *
 * Write a complete application packet into buf[0..buf_size-1].
 * Returns bytes written, or TAI_ERR_*.
 * ========================================================================= */
int tai_packet_encode(uint8_t proto_ver, uint8_t pkt_type,
                       const tai_attr_t *attrs, int attr_count,
                       const uint8_t *payload, size_t payload_len,
                       uint8_t *buf, size_t buf_size)
{
    if (!buf) return TAI_ERR_ARGS;
    (void)proto_ver;  /* reserved for future protocol versions */

    int has_attrs = (attrs && attr_count > 0) ? 1 : 0;
    size_t pos = 0;

    /* 1. Header byte */
    if (pos + 1 > buf_size) return TAI_ERR_MEM;
    buf[pos++] = (uint8_t)((pkt_type << 1) | has_attrs);

    /* 2. Attribute block */
    if (has_attrs) {
        int ab = tai_attrs_encode_block(proto_ver, attrs, attr_count,
                                         buf + pos, buf_size - pos);
        if (ab < 0) return ab;
        pos += (size_t)ab;
    }

    /* 3. Payload */
    if (payload_len > 0) {
        if (pos + payload_len > buf_size) return TAI_ERR_MEM;
        if (payload) memcpy(buf + pos, payload, payload_len);
        pos += payload_len;
    }

    return (int)pos;
}

/* =========================================================================
 * tai_packet_decode
 *
 * Parse a complete application packet from buf[0..buf_len-1].
 * *payload_out points into buf (zero-copy).
 * Returns TAI_OK or TAI_ERR_*.
 * ========================================================================= */
int tai_packet_decode(uint8_t proto_ver,
                       const uint8_t *buf, size_t buf_len,
                       uint8_t *pkt_type_out,
                       tai_attr_t *attrs_out, int max_attrs, int *attr_count_out,
                       const uint8_t **payload_out, size_t *payload_len_out)
{
    if (!buf || buf_len < 1) return TAI_ERR_ARGS;
    (void)proto_ver;  /* reserved for future protocol versions */

    size_t pos = 0;

    /* 1. Header byte */
    uint8_t hdr     = buf[pos++];
    uint8_t pkt_type = hdr >> 1;
    int     has_attrs = hdr & 0x01;

    if (pkt_type_out)    *pkt_type_out    = pkt_type;
    if (attr_count_out)  *attr_count_out  = 0;

    /* 2. Attribute block */
    if (has_attrs) {
        if (pos + 4 > buf_len) return TAI_ERR_PROTO;
        int consumed = tai_attrs_decode_block(proto_ver,
                                               buf + pos, buf_len - pos,
                                               attrs_out, max_attrs,
                                               attr_count_out);
        if (consumed < 0) return consumed;
        pos += (size_t)consumed;
    }

    /* 3. Payload (remaining bytes to end) */
    if (payload_out)     *payload_out     = buf + pos;
    if (payload_len_out) *payload_len_out = (pos <= buf_len) ? buf_len - pos : 0;

    return TAI_OK;
}

/* =========================================================================
 * tai_pack_event
 *
 * Encode an event payload: [event_type:2BE][data...]
 * Returns bytes written.
 * ========================================================================= */
int tai_pack_event(uint8_t proto_ver, uint16_t event_type,
                    const uint8_t *data, size_t data_len,
                    uint8_t *buf, size_t buf_size)
{
    (void)proto_ver;  /* reserved for future protocol versions */
    size_t needed = 2 + data_len;
    if (needed > buf_size) return TAI_ERR_MEM;

    tai_w16(buf, event_type);
    size_t pos = 2;

    if (data_len > 0 && data) {
        memcpy(buf + pos, data, data_len);
        pos += data_len;
    }
    return (int)pos;
}

/* =========================================================================
 * tai_unpack_event
 * ========================================================================= */
int tai_unpack_event(uint8_t proto_ver,
                      const uint8_t *buf, size_t buf_len,
                      uint16_t *evt_type_out,
                      const uint8_t **data_out, size_t *data_len_out)
{
    (void)proto_ver;  /* reserved for future protocol versions */
    if (!buf || buf_len < 2) return TAI_ERR_PROTO;

    *evt_type_out = tai_r16(buf);
    size_t pos = 2;

    if (data_out)     *data_out     = buf + pos;
    if (data_len_out) *data_len_out = (pos <= buf_len) ? buf_len - pos : 0;
    return TAI_OK;
}

/* =========================================================================
 * tai_pack_media_hdr
 *
 * Encode the 8-byte audio/video/image payload header.
 * Layout: [data_id:2BE][packed_48:6]
 *
 * packed_48 (big-endian 48-bit):
 *   bits 47-46: stream_flag
 *   bits 45-42: reserved (0)
 *   bits 41-0:  timestamp_ms & 0x3FFFFFFFFFF
 *
 * Returns bytes written.
 * ========================================================================= */
int tai_pack_media_hdr(uint8_t proto_ver, uint16_t data_id,
                        uint8_t stream_flag, uint64_t ts_ms,
                        uint8_t *buf, size_t buf_size)
{
    (void)proto_ver;  /* reserved for future protocol versions */
    if (buf_size < 8) return TAI_ERR_MEM;

    uint64_t packed = ((uint64_t)(stream_flag & 0x03) << 46)
                    | (ts_ms & UINT64_C(0x3FFFFFFFFFF));

    /* Write packed as big-endian 48-bit: bytes [2..7] of 8-byte BE */
    uint8_t tmp[8];
    tai_w64(tmp, packed);

    tai_w16(buf, data_id);
    memcpy(buf + 2, tmp + 2, 6);   /* take the lower 6 bytes of the 8-byte BE */
    return 8;
}

/* =========================================================================
 * tai_pack_text_hdr
 *
 * Encode the text/file payload header.
 * Layout: [data_id:2BE][flags:1][seq:varint]
 * Returns bytes written.
 * ========================================================================= */
int tai_pack_text_hdr(uint8_t proto_ver, uint16_t data_id,
                       uint8_t stream_flag, uint32_t seq,
                       uint8_t *buf, size_t buf_size)
{
    (void)proto_ver;  /* reserved for future protocol versions */
    if (buf_size < 4) return TAI_ERR_MEM;
    tai_w16(buf, data_id);
    buf[2] = (uint8_t)(stream_flag << 6);

    int varint_len = tai_varint_encode(seq, buf + 3, buf_size - 3);
    if (varint_len < 0) return varint_len;
    return 3 + varint_len;
}

/* =========================================================================
 * tai_varint_encode — protobuf-style unsigned varint
 * Returns bytes written (1..5 for uint32).
 * ========================================================================= */
int tai_varint_encode(uint32_t v, uint8_t *buf, size_t buf_size)
{
    int len = 0;
    do {
        if ((size_t)len >= buf_size) return TAI_ERR_MEM;
        uint8_t b = (uint8_t)(v & 0x7F);
        v >>= 7;
        if (v > 0) b |= 0x80;
        buf[len++] = b;
    } while (v > 0);
    return len;
}

/* =========================================================================
 * tai_varint_decode
 * Returns TAI_OK; sets *v_out and *consumed_out.
 * ========================================================================= */
int tai_varint_decode(const uint8_t *buf, size_t len,
                       uint32_t *v_out, size_t *consumed_out)
{
    uint32_t val    = 0;
    size_t   shift  = 0;
    size_t   i      = 0;

    while (i < len && i < 5) {
        uint8_t b = buf[i++];
        val |= ((uint32_t)(b & 0x7F)) << shift;
        shift += 7;
        if (!(b & 0x80)) {
            if (v_out)        *v_out        = val;
            if (consumed_out) *consumed_out = i;
            return TAI_OK;
        }
    }
    return TAI_ERR_PROTO;
}
