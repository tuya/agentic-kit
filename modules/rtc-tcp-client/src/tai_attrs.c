/*
 * tai_attrs.c — Attribute block encoding and decoding.
 *
 * Implements Section 4 of the Tuya AI Foundation 2.1 protocol spec.
 *
 * Attribute entry layout:  [type:2BE][len:2BE][value:len]
 *
 * An attribute block is prefixed by a 4-byte total-length field (BE) that
 * covers all entries but NOT the 4-byte prefix itself.
 */

#include "tai_internal.h"

/* =========================================================================
 * tai_attrs_encode_block
 *
 * Serialise `count` attrs into buf[0..].
 * Layout:  [block_total_len:4BE] followed by attribute entries.
 * Returns total bytes written, or TAI_ERR_MEM if buf_size is too small.
 * ========================================================================= */
int tai_attrs_encode_block(uint8_t proto_ver,
                            const tai_attr_t *attrs, int count,
                            uint8_t *buf, size_t buf_size)
{
    (void)proto_ver;  /* reserved for future protocol versions */
    if (!attrs || count <= 0 || !buf) return TAI_ERR_ARGS;

    /* Calculate total bytes needed for entries */
    size_t entries_len = 0;
    for (int i = 0; i < count; i++) {
        entries_len += 2 + 2 + attrs[i].len;       /* type + len16 + val */
    }

    size_t total = 4 + entries_len;  /* 4-byte prefix + entries */
    if (total > buf_size) return TAI_ERR_MEM;

    /* Write 4-byte total-length prefix */
    tai_w32(buf, (uint32_t)entries_len);
    size_t pos = 4;

    for (int i = 0; i < count; i++) {
        const tai_attr_t *a = &attrs[i];
        tai_w16(buf + pos, a->type);
        pos += 2;

        tai_w16(buf + pos, a->len);
        pos += 2;

        if (a->len > 0 && a->value) {
            memcpy(buf + pos, a->value, a->len);
            pos += a->len;
        }
    }

    return (int)total;
}

/* =========================================================================
 * tai_attrs_decode_block
 *
 * Parse the attribute block at buf[0..buf_len-1].
 * buf[0..3] = 4-byte total-length prefix (BE).
 * Fills out[0..max_count-1] and sets *count_out.
 * Returns total bytes consumed (4 + block_len), or TAI_ERR_*.
 * ========================================================================= */
int tai_attrs_decode_block(uint8_t proto_ver,
                            const uint8_t *buf, size_t buf_len,
                            tai_attr_t *out, int max_count,
                            int *count_out)
{
    if (!buf || buf_len < 4 || !out || !count_out) return TAI_ERR_ARGS;
    (void)proto_ver;  /* reserved for future protocol versions */

    uint32_t block_len = tai_r32(buf);
    if (4 + block_len > buf_len) return TAI_ERR_PROTO;

    const uint8_t *p   = buf + 4;
    const uint8_t *end = buf + 4 + block_len;
    int n = 0;

    while (p < end) {
        /* Minimum entry header: type(2) + len(2) = 4 */
        if ((size_t)(end - p) < 4) break;

        uint16_t type = tai_r16(p);
        p += 2;

        uint32_t val_len = tai_r16(p);
        p += 2;

        if ((size_t)(end - p) < val_len) break;

        if (n < max_count) {
            out[n].type  = type;
            out[n].len   = (uint16_t)val_len;
            out[n].value = p;
            n++;
        }
        p += val_len;
    }

    *count_out = n;
    return (int)(4 + block_len);
}

/* =========================================================================
 * tai_attr_find — linear scan for an attr by type code
 * ========================================================================= */
const tai_attr_t *tai_attr_find(const tai_attr_t *attrs, int count,
                                 uint16_t type)
{
    for (int i = 0; i < count; i++) {
        if (attrs[i].type == type) return &attrs[i];
    }
    return NULL;
}

/* =========================================================================
 * Typed value accessors
 * ========================================================================= */
uint8_t tai_attr_u8(const tai_attr_t *a)
{
    if (!a || !a->value || a->len < 1) return 0;
    return a->value[0];
}

uint16_t tai_attr_u16(const tai_attr_t *a)
{
    if (!a || !a->value || a->len < 2) return 0;
    return tai_r16(a->value);
}

uint32_t tai_attr_u32(const tai_attr_t *a)
{
    if (!a || !a->value || a->len < 4) return 0;
    return tai_r32(a->value);
}

uint64_t tai_attr_u64(const tai_attr_t *a)
{
    if (!a || !a->value || a->len < 8) return 0;
    return tai_r64(a->value);
}

/* For string attrs the value is NOT NUL-terminated in the wire buffer.
 * Return a cast pointer — callers must respect a->len; they should not
 * use this past the len boundary.  Using snprintf with len is safe. */
const char *tai_attr_str(const tai_attr_t *a)
{
    if (!a || !a->value) return "";
    return (const char *)a->value;
}
