/**
 * @file demo_chat.c
 * @brief Audio + text chat demo bridging iot-sdk token into steam-sdk Open session.
 *
 * Flow:
 *  1. stm_open_init() + set log level
 *  2. Read PCM from file (16kHz mono 16-bit); send text greeting if no PCM path given
 *  3. stm_open_session_create() with token, session_id, local_key
 *  4. Wait for STM_CONNECTION_STATE_CONNECTED (5 s timeout)
 *  5. Send audio in 120 ms chunks (or text)
 *  6. Poll for got_done up to 60 s
 *  7. Print latency stats
 *  8. Save received TTS audio to output_chat.pcm
 *  9. stm_open_session_close() + stm_open_deinit()
 */

#include "chat_demo.h"
#include "stm_open.h"
#include "stm_typedef.h"
#include "stm_errno.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

/* ── Audio constants (16kHz / mono / 16-bit, 120 ms frames) ──────────────── */
#define SAMPLE_RATE    16000
#define CHANNELS       1
#define BIT_DEPTH      16
#define FRAME_MS       120
#define CHUNK_SIZE     (SAMPLE_RATE * CHANNELS * (BIT_DEPTH / 8) * FRAME_MS / 1000)

#define OUTPUT_FILE    "output_chat.pcm"
#define MAX_WAIT_SEC   60
#define CONNECT_TIMEOUT_SEC 5

/* ── Timing helper ──────────────────────────────────────────────────────── */
static int64_t now_us(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (int64_t)tv.tv_sec * 1000000LL + (int64_t)tv.tv_usec;
}

/* ── Receive context ────────────────────────────────────────────────────── */
typedef struct {
    FILE    *audio_fp;

    int      got_first_text;
    int      got_first_audio;
    int      got_done;

    volatile uint8_t connect_done;

    int64_t  send_start_us;
    int64_t  send_end_us;
    int64_t  first_text_us;
    int64_t  first_audio_us;
    int64_t  audio_end_us;
} chat_ctx_t;

/* ── Callbacks ──────────────────────────────────────────────────────────── */
static void stm_log_cb(stm_log_level_e level, const char *log, uint32_t log_len)
{
    (void)level;
    printf("[STM] %.*s\n", (int)log_len, log);
}

static void on_state(stm_open_session_t *session, uint16_t state, void *user_data)
{
    (void)session;
    chat_ctx_t *ctx = (chat_ctx_t *)user_data;

    printf("[Session] state=%u\n", state);

    if (state == STM_CONNECTION_STATE_CONNECTED && ctx) {
        ctx->connect_done = 1;
    }
}

static void on_data_recv(stm_open_session_t *session, stm_open_data_t *data,
                         int8_t fin, void *user_data)
{
    (void)session;
    chat_ctx_t *ctx = (chat_ctx_t *)user_data;
    if (!ctx || !data) return;

    switch (data->data_type) {
    case STM_DATA_TYPE_TEXT:
        if (!ctx->got_first_text) {
            ctx->first_text_us  = now_us();
            ctx->got_first_text = 1;
        }
        if (data->payload && data->payload_length > 0) {
            printf("[Text] event_id=%s %.*s\n", data->event_id, (int)data->payload_length,
                   (const char *)data->payload);
        }
        break;

    case STM_DATA_TYPE_AUDIO:
        if (!ctx->got_first_audio) {
            ctx->first_audio_us  = now_us();
            ctx->got_first_audio = 1;
        }
        printf("[Audio] event_id=%s len=%u fin=%d\n", data->event_id, data->payload_length, fin);
        if (data->payload && data->payload_length > 0 && ctx->audio_fp) {
            fwrite(data->payload, 1, data->payload_length, ctx->audio_fp);
        }
        if (fin) {
            ctx->audio_end_us = now_us();
        }
        break;

    case STM_DATA_TYPE_CMD:
        printf("[CMD] event_id=%s len=%u fin=%d\n", data->event_id, data->payload_length, fin);
        break;

    default:
        break;
    }

    if (fin == 1) {
        ctx->got_done = 1;
    }
}

/* ── Send helpers ───────────────────────────────────────────────────────── */
static stm_ret send_text(stm_open_session_t *session,
                         const char *event_id, const char *text)
{
    stm_open_data_t d = {0};
    d.data_type      = STM_DATA_TYPE_TEXT;
    d.event_id       = (char *)event_id;
    d.payload        = (uint8_t *)text;
    d.payload_length = (uint32_t)strlen(text);
    return stm_open_session_send(session, &d, 1);
}

static stm_ret send_audio_chunked(stm_open_session_t *session,
                                  const char *event_id,
                                  const uint8_t *pcm, size_t pcm_len)
{
    stm_open_data_t d = {0};
    d.data_type                   = STM_DATA_TYPE_AUDIO;
    d.audio_params.codec_type     = 101;
    d.audio_params.sample_rate    = SAMPLE_RATE;
    d.audio_params.channels       = CHANNELS;
    d.audio_params.bit_depth      = BIT_DEPTH;
    d.audio_params.container      = 0;
    d.audio_params.bitrate        = SAMPLE_RATE * CHANNELS * BIT_DEPTH / 8;
    d.audio_params.frame_duration = FRAME_MS;
    d.audio_params.frame_size     = CHUNK_SIZE;

    size_t offset = 0;
    while (offset < pcm_len) {
        size_t chunk = pcm_len - offset;
        if (chunk > CHUNK_SIZE) chunk = CHUNK_SIZE;

        d.event_id       = (offset == 0) ? (char *)event_id : NULL;
        d.payload        = (uint8_t *)(pcm + offset);
        d.payload_length = (uint32_t)chunk;
        d.app_data       = NULL;
        int8_t is_last   = (offset + chunk >= pcm_len) ? 1 : 0;

        printf("Sending audio chunk offset=%zu size=%zu fin=%d\n",
               offset, chunk, is_last);

        stm_ret ret = stm_open_session_send(session, &d, is_last);
        if (ret != STM_OK) {
            printf("[Error] send failed at offset %zu, ret=%d\n", offset, ret);
            return ret;
        }
        offset += chunk;
    }
    return STM_OK;
}

/* ── Public entry point ─────────────────────────────────────────────────── */
int demo_chat_run(const char *token, const char *local_key,
                  const char *session_id, const char *input_pcm)
{
    /* ── 1. Load PCM (optional) ─────────────────────────────────────────── */
    uint8_t *pcm_data  = NULL;
    size_t   pcm_len   = 0;

    printf("input_pcm: %s\n", input_pcm);

    if (input_pcm) {
        FILE *fp = fopen(input_pcm, "rb");
        if (!fp) {
            fprintf(stderr, "[demo_chat] Cannot open PCM file: %s\n", input_pcm);
            return -1;
        }
        fseek(fp, 0, SEEK_END);
        long fsize = ftell(fp);
        fseek(fp, 0, SEEK_SET);
        if (fsize <= 0) {
            fclose(fp);
            fprintf(stderr, "[demo_chat] PCM file is empty: %s\n", input_pcm);
            return -1;
        }
        pcm_data = (uint8_t *)malloc((size_t)fsize);
        if (!pcm_data) { fclose(fp); return -1; }
        size_t nr = fread(pcm_data, 1, (size_t)fsize, fp);
        fclose(fp);
        if ((long)nr != fsize) {
            free(pcm_data);
            fprintf(stderr, "[demo_chat] Partial PCM read\n");
            return -1;
        }
        pcm_len = (size_t)fsize;
        double dur = (double)pcm_len / (SAMPLE_RATE * CHANNELS * (BIT_DEPTH / 8));
        printf("[demo_chat] PCM: %s  size=%zu bytes  duration=%.2f s\n",
               input_pcm, pcm_len, dur);
    } else {
        printf("[demo_chat] No PCM file: will send text greeting\n");
    }

    /* ── 2. Init steam-sdk ──────────────────────────────────────────────── */
    stm_open_config_t sdk_cfg = {0};
    sdk_cfg.on_log = stm_log_cb;
    if (stm_open_init(&sdk_cfg) != STM_OK) {
        fprintf(stderr, "[demo_chat] stm_open_init failed\n");
        free(pcm_data);
        return -1;
    }
    stm_open_set_log_level(STM_LOG_LEVEL_INFO);

    /* ── 3. Prepare receive context ────────────────────────────────────── */
    chat_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.audio_fp = fopen(OUTPUT_FILE, "wb");
    if (!ctx.audio_fp) {
        fprintf(stderr, "[demo_chat] Cannot create output file: %s\n", OUTPUT_FILE);
        free(pcm_data);
        stm_open_deinit();
        return -1;
    }

    /* ── 4. Create session ──────────────────────────────────────────────── */
    stm_open_session_config_t sess_cfg = {0};
    sess_cfg.session_token = (char *)token;
    sess_cfg.session_id    = (char *)session_id;
    sess_cfg.client_type   = STM_CLIENT_TYPE_DEVICE;
    sess_cfg.encrypt_key   = (char *)local_key;
    sess_cfg.on_state      = on_state;
    sess_cfg.on_data_recv  = on_data_recv;
    sess_cfg.user_data     = &ctx;

    printf("[demo_chat] Session ID: %s\n", session_id);

    stm_open_session_t *session = stm_open_session_create(&sess_cfg);
    if (!session) {
        fprintf(stderr, "[demo_chat] stm_open_session_create failed\n");
        fclose(ctx.audio_fp);
        free(pcm_data);
        stm_open_deinit();
        return -1;
    }

    /* ── 5. Wait for connected ──────────────────────────────────────────── */
    printf("[demo_chat] Waiting %d s for connection...\n", CONNECT_TIMEOUT_SEC);
    usleep((useconds_t)CONNECT_TIMEOUT_SEC * 1000000U);
    printf("[demo_chat] Connected (ctx.connect_done=%d)\n", ctx.connect_done);

    /* ── 6. Send audio or text ──────────────────────────────────────────── */
    ctx.send_start_us = now_us();
    stm_ret send_ret;

    if (pcm_data) {
        send_ret = send_audio_chunked(session, "chat_event_001", pcm_data, pcm_len);
    } else {
        const char *greeting = "Hello, AI! This is an integrated iot-sdk + steam-sdk demo.";
        send_ret = send_text(session, "chat_event_001", greeting);
    }

    ctx.send_end_us = now_us();
    free(pcm_data);
    pcm_data = NULL;

    if (send_ret != STM_OK) {
        fprintf(stderr, "[demo_chat] Send failed: %d\n", send_ret);
        stm_open_session_close(session);
        fclose(ctx.audio_fp);
        stm_open_deinit();
        return -1;
    }
    printf("[demo_chat] Send complete (%.1f ms)\n",
           (double)(ctx.send_end_us - ctx.send_start_us) / 1000.0);

    /* ── 7. Wait for response (60 s) ────────────────────────────────────── */
    printf("[demo_chat] Waiting for AI response...\n");
    int wait_ms = 0;
    while (wait_ms < MAX_WAIT_SEC * 1000) {
        if (ctx.got_done) break;
        usleep(50000);
        wait_ms += 50;
    }

    /* ── 8. Close output file ───────────────────────────────────────────── */
    fclose(ctx.audio_fp);
    ctx.audio_fp = NULL;

    /* ── 9. Print latency stats ─────────────────────────────────────────── */
    printf("\n========== Latency Stats ==========\n");
    if (ctx.got_first_text) {
        printf("Send start  -> first text:  %.1f ms\n",
               (double)(ctx.first_text_us - ctx.send_start_us) / 1000.0);
    } else {
        printf("Send start  -> first text:  (none received)\n");
    }
    if (ctx.got_first_audio) {
        printf("Send end    -> first audio: %.1f ms\n",
               (double)(ctx.first_audio_us - ctx.send_end_us) / 1000.0);
    } else {
        printf("Send end    -> first audio: (none received)\n");
    }
    if (ctx.got_first_audio && ctx.got_done) {
        printf("First audio -> audio done:  %.1f ms\n",
               (double)(ctx.audio_end_us - ctx.first_audio_us) / 1000.0);
    } else {
        printf("First audio -> audio done:  (incomplete)\n");
    }
    printf("====================================\n");

    if (ctx.got_done) {
        printf("[demo_chat] TTS audio saved to: %s\n", OUTPUT_FILE);
    } else {
        printf("[demo_chat] Response timed out after %d s\n", MAX_WAIT_SEC);
    }

    /* ── 10. Cleanup ────────────────────────────────────────────────────── */
    stm_open_session_close(session);
    stm_open_deinit();
    return ctx.got_done ? 0 : -1;
}
