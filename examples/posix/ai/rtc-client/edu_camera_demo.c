/**
 * @file image_understanding.c
 * @brief 图片理解 demo：读取本地图片发送到 AI 基座，可附带文本 prompt，接收理解文本和 TTS 音频
 *
 * 功能:
 *  1. 读取图片文件（JPEG/PNG），按 10KB 分包发送
 *  2. 可选附带文本 prompt（如 "请描述这张图片"）
 *  3. 接收到的文本实时打印到控制台，TTS 音频保存到文件
 *  4. 统计耗时：发送完毕 -> 首包文字、发送完毕 -> 首包音频
 *  5. session_id 格式为 open_img_{UUID}
 */

#include "stm_errno.h"
#include "stm_open.h"
#include "stm_typedef.h"
#include "edu_camera_demo.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#define IMG_CHUNK_SIZE   (10 * 1024)
#define OUTPUT_FILE      "output_tts.pcm"
#define MAX_WAIT_SEC     60
#define DEBUG_PREFIX     "[DEBUG-edu-crash]"

/* ────────── 音频格式判定 ────────── */

/**
 * 根据 audio_params 中的 codec_type 和 container 确定输出文件后缀
 *   codec_type: 101-PCM, 108-SPEEX, 109-MP3
 *   container:  0-裸流, 1-ogg
 */
static const char *audio_output_ext(uint16_t codec_type, uint16_t container) {
    if (container == 1) {
        return ".ogg";
    }
    switch (codec_type) {
    case 109: return ".mp3";
    case 101: return ".pcm";
    case 108: return ".spx";
    default:  return ".pcm";
    }
}

/* ────────── 时间工具 ────────── */

static int64_t now_us(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (int64_t)tv.tv_sec * 1000000LL + (int64_t)tv.tv_usec;
}

/* ────────── 生成随机 event_id ────────── */

static void generate_event_id(char *buf, size_t buf_size) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    srand((unsigned int)(tv.tv_sec ^ tv.tv_usec ^ getpid()));
    snprintf(buf, buf_size, "evt_%ld_%06ld_%04x%04x",
             (long)tv.tv_sec, (long)tv.tv_usec,
             (unsigned int)(rand() & 0xFFFF),
             (unsigned int)(rand() & 0xFFFF));
}

/* ────────── 接收上下文 ────────── */

typedef struct {
    FILE *audio_fp;
    char output_path[256];

    int got_first_text;
    int got_first_audio;
    int got_done;
    int audio_format_detected;

    uint8_t connect_done;

    int64_t send_end_us;
    int64_t first_text_us;
    int64_t first_audio_us;
    int64_t audio_end_us;
} img_ctx_t;

/* ────────── 回调 ────────── */

static void on_log(stm_log_level_e level, const char *log, uint32_t log_len) {
    (void)level;
    printf("[STM] %.*s\n", (int)log_len, log);
}

static void on_state(stm_open_session_t *session, uint16_t state, void *user_data) {
    (void)session;
    img_ctx_t *ctx = (img_ctx_t *)user_data;

    if (state == STM_SESSION_STATE_NEW) {
        if (ctx) {
            ctx->connect_done = 1;
        }
    }
    printf("-------------[Session] state=%u\n", state);
}

static void on_data_recv(stm_open_session_t *session, stm_open_data_t *data, int8_t fin, void *user_data) {
    (void)session;
    img_ctx_t *ctx = (img_ctx_t *)user_data;
    if (!ctx || !data) return;

    switch (data->data_type) {
    case STM_DATA_TYPE_TEXT:
        if (!ctx->got_first_text) {
            ctx->first_text_us = now_us();
            ctx->got_first_text = 1;
        }
        if (data->payload && data->payload_length > 0) {
            printf("[Text] %.*s\n", (int)data->payload_length, (const char *)data->payload);
        }
        break;
    case STM_DATA_TYPE_AUDIO:
        if (!ctx->got_first_audio) {
            ctx->first_audio_us = now_us();
            ctx->got_first_audio = 1;

            /* 根据首包 audio_params 判断格式，创建对应后缀的输出文件 */
            if (!ctx->audio_format_detected) {
                ctx->audio_format_detected = 1;
                const char *ext = audio_output_ext(data->audio_params.codec_type,
                                                   data->audio_params.container);
                if (ctx->audio_fp) {
                    fclose(ctx->audio_fp);
                    ctx->audio_fp = NULL;
                }
                char filename[512] = {0};
                snprintf(filename, sizeof(filename), "output_tts%s", ext);
                ctx->audio_fp = fopen(filename, "wb");
                if (!ctx->audio_fp) {
                    printf("[Error] 无法创建输出文件: %s\n", filename);
                } else {
                    printf("[Audio] codec_type=%u container=%u -> 输出文件: %s\n",
                           data->audio_params.codec_type, data->audio_params.container, filename);
                }
            }
        }
        printf("[Audio] len=%u fin=%d\n", data->payload_length, fin);
        if (data->payload && data->payload_length > 0 && ctx->audio_fp) {
            fwrite(data->payload, 1, data->payload_length, ctx->audio_fp);
        }
        if (fin) {
            ctx->audio_end_us = now_us();
        }
        break;
    case STM_DATA_TYPE_CMD:
        printf("[CMD] len=%u fin=%d\n", data->payload_length, fin);
        break;
    default:
        break;
    }

    if (fin == 1) {
        printf(DEBUG_PREFIX " callback fin=1 data_type=%u got_done=1\n", data->data_type);
        ctx->got_done = 1;
    }
}

/* ────────── 判断图片格式 ────────── */

static uint8_t detect_image_format(const uint8_t *data, size_t len) {
    if (len >= 8 && data[0] == 0x89 && data[1] == 0x50 &&
        data[2] == 0x4E && data[3] == 0x47) {
        return 2; /* PNG */
    }
    return 1; /* 默认 JPEG */
}

static stm_ret send_image(stm_open_session_t *session, const char *event_id,
                          const uint8_t *img_data, size_t img_len,
                                   uint8_t format, int has_more) {
    stm_open_data_t data = {0};
    data.data_type = STM_DATA_TYPE_IMAGE;
    data.image_params.payload_type = 0; /* raw */
    data.image_params.format = format;
    data.image_params.width = 2048;
    data.image_params.height = 2730;
    data.timestamp = now_us() / 1000;
    data.event_id = (char *)event_id;
    data.payload = (uint8_t *)img_data;
    data.payload_length = (uint32_t)img_len;

    printf("发送图片 size=%zu fin=%d\n", img_len, 1);

    stm_ret ret = stm_open_session_send(session, &data, 1);
    if (ret != STM_OK) {
        printf("[Error] 发送图片失败 ret=%d\n", ret);
        return ret;
    }
    return STM_OK;
}

/* ────────── 发送文本 prompt ────────── */

static stm_ret send_text_prompt(stm_open_session_t *session, const char *event_id,
                                 const char *text, int8_t fin) {
    stm_open_data_t data = {0};
    data.data_type = STM_DATA_TYPE_TEXT;
    data.event_id = (char *)event_id;
    data.payload = (uint8_t *)text;
    data.payload_length = (uint32_t)strlen(text);

    return stm_open_session_send(session, &data, fin);
}

/* ────────── demo_image_understand_run ────────── */

int demo_image_understand_run(const char *token, const char *device_local_key,
                              const char *session_id, const char *img_path,
                              const char *prompt, const char *audio_path) {
    if (!img_path) img_path = "res/test.jpg";
    if (!prompt) prompt = "请描述这张图片的内容";
    if (!audio_path) audio_path = OUTPUT_FILE;

    /* 1. 读取图片文件 */
    FILE *fp = fopen(img_path, "rb");
    if (!fp) {
        printf("无法打开图片: %s\n", img_path);
        return -1;
    }
    fseek(fp, 0, SEEK_END);
    long fsize = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (fsize <= 0 || fsize > 10 * 1024 * 1024) {
        fclose(fp);
        printf("图片大小异常 (%ld bytes)，限制 10MB\n", fsize);
        return -1;
    }

    uint8_t *img_data = (uint8_t *)malloc((size_t)fsize);
    if (!img_data) {
        fclose(fp);
        return -1;
    }
    size_t nread = fread(img_data, 1, (size_t)fsize, fp);
    fclose(fp);
    if (nread != (size_t)fsize) {
        free(img_data);
        printf("读取图片不完整\n");
        return -1;
    }

    uint8_t img_format = detect_image_format(img_data, nread);
    printf("图片文件: %s  大小: %zu bytes  格式: %s\n",
           img_path, nread, img_format == 2 ? "PNG" : "JPEG");
    printf("Prompt: %s\n", prompt);

    /* 2. 初始化 SDK */
    stm_open_config_t sdk_cfg = {0};
    sdk_cfg.on_log = on_log;
    if (stm_open_init(&sdk_cfg) != STM_OK) {
        printf("SDK 初始化失败\n");
        free(img_data);
        return -1;
    }
    stm_open_set_log_level(STM_LOG_LEVEL_INFO);

    /* 3. 准备接收上下文 */
    img_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    snprintf(ctx.output_path, sizeof(ctx.output_path), "%s", audio_path);
    ctx.audio_fp = fopen(ctx.output_path, "wb");
    if (!ctx.audio_fp) {
        printf("无法创建输出文件: %s\n", ctx.output_path);
        free(img_data);
        stm_open_deinit();
        return -1;
    }

    /* 4. 创建 session */
    printf("Session ID: %s\n", session_id);

    stm_open_session_config_t sess_cfg = {0};
    sess_cfg.client_type = STM_CLIENT_TYPE_DEVICE;
    sess_cfg.session_token = (char *)token;
    sess_cfg.session_id = (char *)session_id;
    sess_cfg.encrypt_key = (char *)device_local_key;
    sess_cfg.on_state = on_state;
    sess_cfg.on_data_recv = on_data_recv;
    sess_cfg.user_data = &ctx;

    stm_open_session_t *session = stm_open_session_create(&sess_cfg);
    if (!session) {
        printf("创建会话失败\n");
        fclose(ctx.audio_fp);
        free(img_data);
        stm_open_deinit();
        return -1;
    }

    /* 5. 等待连接建立 */
    printf("等待连接建立...\n");
    int wait_conn_ms = 0;
    while (!ctx.connect_done && wait_conn_ms < 10000) {
        usleep(50000);
        wait_conn_ms += 50;
    }
    if (!ctx.connect_done) {
        printf("连接超时，尝试继续发送...\n");
    } else {
        printf("连接已建立 (耗时 %d ms)\n", wait_conn_ms);
    }

    //usleep(10 * 1000 * 1000);

    /* 6. 生成随机 event_id，用于 prompt 和图片发送 */
    char event_id[64];
    generate_event_id(event_id, sizeof(event_id));
    printf("Event ID: %s\n", event_id);

    /* 7. 发送数据：先发文本 prompt (fin=0)，再发图片 (fin=1) */
    printf("发送 prompt: %s\n", prompt);
    stm_ret ret = send_text_prompt(session, event_id, prompt, 0);
    if (ret != STM_OK) {
        printf("发送 prompt 失败: %d\n", ret);
        stm_open_session_close(session);
        fclose(ctx.audio_fp);
        free(img_data);
        stm_open_deinit();
        return -1;
    }

    printf("发送图片 (%zu bytes, %d 个分包)...\n", nread,
           (int)((nread + IMG_CHUNK_SIZE - 1) / IMG_CHUNK_SIZE));
    ret = send_image(session, event_id, img_data, nread, img_format, 1);
    ctx.send_end_us = now_us();
    free(img_data);

    if (ret != STM_OK) {
        printf("发送图片失败: %d\n", ret);
        stm_open_session_close(session);
        fclose(ctx.audio_fp);
        stm_open_deinit();
        return -1;
    }
    printf("数据发送完毕\n");

    /* 8. 等待 AI 响应 */
    printf("等待 AI 响应...\n");
    int wait_ms = 0;
    while (wait_ms < MAX_WAIT_SEC * 1000) {
        if (ctx.got_done) break;
        usleep(50000);
        wait_ms += 50;
    }

    if (ctx.got_done) {
        printf(DEBUG_PREFIX " main saw got_done, sleeping 500ms before cleanup\n");
        usleep(500000);
    }

    /* 9. 关闭输出文件 */
    if (ctx.audio_fp) {
        fclose(ctx.audio_fp);
        ctx.audio_fp = NULL;
    }

    /* 10. 打印耗时统计 */
    printf("\n========== 耗时统计 ==========\n");
    if (ctx.got_first_text) {
        double t1 = (double)(ctx.first_text_us - ctx.send_end_us) / 1000.0;
        printf("发送完毕 -> 首包文字:  %.1f ms\n", t1);
    } else {
        printf("发送完毕 -> 首包文字:  未收到文字\n");
    }

    if (ctx.got_first_audio) {
        double t2 = (double)(ctx.first_audio_us - ctx.send_end_us) / 1000.0;
        printf("发送完毕 -> 首包音频:  %.1f ms\n", t2);
    } else {
        printf("发送完毕 -> 首包音频:  未收到音频\n");
    }

    if (ctx.got_first_audio && ctx.got_done) {
        double t3 = (double)(ctx.audio_end_us - ctx.first_audio_us) / 1000.0;
        printf("首包音频 -> 音频接收完毕: %.1f ms\n", t3);
    } else {
        printf("首包音频 -> 音频接收完毕: 未完成\n");
    }
    printf("==============================\n");

    if (ctx.got_done) {
        printf("TTS 音频已保存至: %s\n", ctx.output_path);
    } else {
        printf("等待响应超时\n");
    }

    /* 11. 清理 */
    printf(DEBUG_PREFIX " closing session\n");
    stm_open_session_close(session);
    printf(DEBUG_PREFIX " deinit sdk\n");
    stm_open_deinit();
    printf(DEBUG_PREFIX " cleanup done\n");
    return 0;
}
