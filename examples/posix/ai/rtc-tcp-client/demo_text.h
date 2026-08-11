/*
 * demo_text.h — safe handling of tai_text_msg_t for the rtc-tcp-client POSIX
 * demos (header-only).
 *
 * Two hazards it removes:
 *
 * 1. tai_text_msg_t.text is a borrowed slice of the SDK receive buffer and is
 *    NOT NUL-terminated (see tuya_ai.h), so strstr/strchr on it run past
 *    msg->len and eventually past the end of the tai_ctx_t allocation.
 *    Everything here is length-bounded, or copies the bytes out first.
 *
 * 2. Text arrives chunked — TAI_STREAM_START / MIDDLE / END, or a single
 *    ONE_SHOT. Printing can take each chunk as it comes; parsing JSON must
 *    reassemble first, since the fields may straddle a chunk boundary.
 *
 * The accumulator holds ONE stream. It cannot demux two that interleave inside
 * a turn — they share event_id, and data_id is the constant
 * TAI_DATA_ID_TEXT_DOWN — so every loss it detects is reported rather than
 * papered over: tb->dropped counts the streams it had to give up on, and a
 * caller that exits with a status should consult it.
 */
#ifndef DEMO_TEXT_H
#define DEMO_TEXT_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tuya_ai.h"
#include "demo_json.h"

/* Ceiling on a reassembled text stream; a server that never sends END cannot
 * grow the buffer without bound. */
#ifndef DEMO_TEXTBUF_MAX
#define DEMO_TEXTBUF_MAX (256u * 1024u)
#endif

/* Largest NLG chunk nlg_print_content() decodes; a longer one is printed with
 * its escapes still in place rather than dropped. This is a stack buffer in the
 * receive callback, so shrink it when porting to a thread with a small stack —
 * one NLG chunk is a fragment of a sentence, not a whole reply. */
#ifndef DEMO_NLG_CHUNK_MAX
#define DEMO_NLG_CHUNK_MAX 2048
#endif

/* What to do when a chunk's seq does not follow the previous one:
 *
 *   0 — no check.
 *   1 — warn and keep accumulating (default).
 *   2 — drop the stream.
 *
 * A gap is ambiguous. It does mean chunks the app never saw consumed a seq, but
 * the SDK itself swallows zero-length text frames (tai_protocol.c media_text
 * only emits when payload_len > off), and those carry no bytes: continuing past
 * a gap they caused reassembles exactly the right document, while dropping
 * loses a healthy stream. Only when the missing chunks belonged to a second,
 * interleaved stream does continuing splice two documents together — and the
 * JSON parse then rejects the mixture anyway. Build with
 * -DDEMO_TEXT_SEQ_CHECK=2 for a deployment where interleaving is the likelier
 * cause. */
#ifndef DEMO_TEXT_SEQ_CHECK
#define DEMO_TEXT_SEQ_CHECK 1
#endif

/* -- Bounded search ------------------------------------------------------- */

/* strstr() over a slice that carries no NUL terminator. */
static inline const char *demo_memfind(const char *hay, size_t hlen,
                                       const char *needle)
{
    size_t nlen = strlen(needle);
    if (nlen == 0 || hlen < nlen) return NULL;
    for (size_t i = 0; i + nlen <= hlen; i++) {
        if (hay[i] == needle[0] && memcmp(hay + i, needle, nlen) == 0)
            return hay + i;
    }
    return NULL;
}

/* -- NLG prose ------------------------------------------------------------ */

/* Point at the raw (still JSON-escaped) content field of an NLG line. Reads
 * only within [text, text+len), so it is safe on the callback slice. NULL if
 * this chunk is not NLG, or the value does not terminate inside it. */
static inline const char *nlg_extract_content(const char *text, size_t len,
                                              size_t *out_len)
{
    if (!text || len == 0 || !out_len) return NULL;
    if (!demo_memfind(text, len, "\"NLG\"")) return NULL;

    const char *end = text + len;
    const char *p   = demo_memfind(text, len, "\"content\"");
    if (!p) return NULL;
    p += sizeof("\"content\"") - 1;

    while (p < end && json_is_space(*p)) p++;
    if (p >= end || *p != ':') return NULL;
    p++;
    while (p < end && json_is_space(*p)) p++;
    if (p >= end || *p != '"') return NULL;
    p++;

    for (const char *q = p; q < end; ) {
        if (*q == '\\') { q += 2; continue; }
        if (*q == '"') { *out_len = (size_t)(q - p); return p; }
        q++;
    }
    return NULL;
}

/* Print one chunk's NLG prose with its JSON escapes decoded — \n, \", and the
 * \uXXXX the server uses for CJK all reach the terminal as the characters they
 * stand for, which the raw slice from nlg_extract_content() does not.
 *
 * Returns 1 if this chunk was NLG and has been handled, so the caller must not
 * also print it raw. That includes the empty terminator line ({"content":""}),
 * which prints nothing — it is still NLG. Returns 0 otherwise. */
static inline int nlg_print_content(const char *text, size_t len)
{
    size_t      raw_len = 0;
    const char *raw     = nlg_extract_content(text, len, &raw_len);
    if (!raw) return 0;

    char buf[DEMO_NLG_CHUNK_MAX];
    int  n = json_unescape(raw, raw_len, buf, sizeof(buf));
    if (n < 0)
        fwrite(raw, 1, raw_len, stdout);   /* too long, or a bad escape */
    else if (n > 0)
        fwrite(buf, 1, (size_t)n, stdout);
    fflush(stdout);
    return 1;
}

/* -- Stream reassembly ---------------------------------------------------- */

typedef struct {
    char    *buf;      /* NUL-terminated once accum()/flush() returns 1       */
    size_t   len;
    size_t   cap;
    uint32_t seq;      /* seq of the last chunk taken in                      */
    int      have_seq; /* seq is meaningful (a stream is in progress)         */
    int      done;     /* buf holds an already-delivered stream               */
    int      dropping; /* current stream was dropped; swallow it through END  */
    unsigned dropped;  /* streams lost so far — survives reset(); see below   */
} demo_textbuf_t;

/* Discard any buffered or delivered stream, keeping the allocation. Call when
 * the connection drops: a half-received stream from the old connection must
 * not prefix the first stream of the new one.
 *
 * tb->dropped is deliberately NOT cleared: it is the session's tally of lost
 * streams, and a caller that reports success or failure needs it to survive
 * every reset in between. */
static inline void demo_textbuf_reset(demo_textbuf_t *tb)
{
    tb->len      = 0;
    tb->have_seq = 0;
    tb->done     = 0;
    tb->dropping = 0;
}

static inline void demo_textbuf_free(demo_textbuf_t *tb)
{
    free(tb->buf);
    tb->buf = NULL;
    tb->cap = 0;
    demo_textbuf_reset(tb);
}

/* Give up on the stream being accumulated: name the reason once, count it, and
 * swallow its remaining chunks so the caller sees one -1 per stream rather than
 * one per chunk. The allocation is kept — reset(), not free() — since the next
 * stream would otherwise have to grow it from 1 KB all over again. */
static inline int demo_textbuf_drop(demo_textbuf_t *tb, int ends, const char *why)
{
    fprintf(stderr, "[demo_text] dropping a text stream: %s\n", why);
    tb->dropped++;
    demo_textbuf_reset(tb);
    tb->dropping = !ends;
    return -1;
}

/* Append one chunk. START and ONE_SHOT begin a fresh stream; so does any chunk
 * after a delivered one, so a duplicated END cannot re-deliver it and a
 * lost-START continuation cannot extend it.
 *
 * Returns 1 when the stream has ended and tb->buf holds the whole text,
 * NUL-terminated; 0 while more chunks are expected (or the stream was empty);
 * -1 when it had to be dropped — too large, out of memory, or (under
 * DEMO_TEXT_SEQ_CHECK=2) its chunks did not arrive consecutively.
 *
 * A stream displaced by a new START is also lost, but the displacing stream is
 * accumulated normally and that chunk still returns 0 or 1. Every loss, however
 * it is reported, increments tb->dropped — check that, not the return value, to
 * decide whether a session lost data. */
static inline int demo_textbuf_accum(demo_textbuf_t *tb, const tai_text_msg_t *msg)
{
    int starts = (msg->stream_flag == TAI_STREAM_START ||
                  msg->stream_flag == TAI_STREAM_ONE_SHOT);
    int ends   = (msg->stream_flag == TAI_STREAM_END ||
                  msg->stream_flag == TAI_STREAM_ONE_SHOT);

    /* A fresh stream displaces whatever is still buffered. When that was a
     * stream in progress it is gone — say so. This is how an interleaved
     * ONE_SHOT or START used to vanish silently: the reset below runs before
     * the seq check ever sees the gap. */
    if (starts && !tb->done && !tb->dropping && tb->len > 0) {
        fprintf(stderr,
                "[demo_text] a new stream started while %zu bytes of the "
                "previous one were still buffered: dropping those — this "
                "buffer holds one stream and cannot demux interleaved ones\n",
                tb->len);
        tb->dropped++;
    }

    if (starts || tb->done)
        demo_textbuf_reset(tb);

    if (tb->dropping) {
        if (ends) tb->dropping = 0;
        return 0;
    }

#if DEMO_TEXT_SEQ_CHECK
    /* Chunks the app never saw consumed a seq: either the SDK swallowed empty
     * frames, or another stream's chunks landed in between. See
     * DEMO_TEXT_SEQ_CHECK above for why the default is to continue. */
    if (!starts && tb->have_seq && msg->seq != tb->seq + 1) {
#if DEMO_TEXT_SEQ_CHECK >= 2
        char why[128];
        snprintf(why, sizeof(why),
                 "seq gap (%u -> %u), so its chunks did not arrive consecutively",
                 (unsigned)tb->seq, (unsigned)msg->seq);
        return demo_textbuf_drop(tb, ends, why);
#else
        fprintf(stderr,
                "\n[demo_text] text seq gap (%u -> %u): continuing — chunks the "
                "SDK dropped for being empty look exactly like this. Build with "
                "-DDEMO_TEXT_SEQ_CHECK=2 to drop the stream instead\n",
                (unsigned)tb->seq, (unsigned)msg->seq);
#endif
    }
#endif
    tb->seq      = msg->seq;
    tb->have_seq = 1;

    if (msg->len > 0) {
        size_t need = tb->len + msg->len + 1;
        if (need > DEMO_TEXTBUF_MAX) {
            char why[128];
            snprintf(why, sizeof(why),
                     "it reached %zu bytes, past the %u-byte DEMO_TEXTBUF_MAX",
                     need, (unsigned)DEMO_TEXTBUF_MAX);
            return demo_textbuf_drop(tb, ends, why);
        }
        if (need > tb->cap) {
            size_t cap = tb->cap ? tb->cap : 1024;
            while (cap < need) cap *= 2;
            char *nb = (char *)realloc(tb->buf, cap);
            if (!nb) {
                /* realloc left the old block intact, but the heap is tight:
                 * hand it back rather than hold it for the next stream. */
                fprintf(stderr, "[demo_text] dropping a text stream: out of "
                                "memory growing the buffer to %zu bytes\n", cap);
                tb->dropped++;
                demo_textbuf_free(tb);
                tb->dropping = !ends;
                return -1;
            }
            tb->buf = nb;
            tb->cap = cap;
        }
        memcpy(tb->buf + tb->len, msg->text, msg->len);
        tb->len += msg->len;
    }

    if (!ends || tb->len == 0) return 0;

    tb->buf[tb->len] = '\0';
    tb->done = 1;
    return 1;
}

/* Deliver whatever is buffered even though no END chunk arrived: the SDK drops
 * empty text frames (tai_protocol.c media_text), so a stream ended by a bare
 * zero-length END never completes through accum(). Call at TAI_EVT_END.
 * Returns 1 with tb->buf NUL-terminated if a stream was pending, else 0. */
static inline int demo_textbuf_flush(demo_textbuf_t *tb)
{
    tb->dropping = 0;
    if (tb->done || tb->len == 0) return 0;
    tb->buf[tb->len] = '\0';
    tb->done = 1;
    return 1;
}

#endif /* DEMO_TEXT_H */
