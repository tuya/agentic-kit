/* Typed workflow inputs on successive Events in one TAI Session. */
#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "cJSON.h"
#include "iot_client.h"
#include "tuya_ai.h"
#include "demo_json.h"
#include "demo_mcp.h"
#include "demo_text.h"

extern const pal_t *tai_pal_posix(void);

#define DEFAULT_TEXT "Please run the configured workflow."
#define DEFAULT_TIMEOUT 60
#define MAX_TIMEOUT 3600
#define MAX_IMAGE_BYTES (16u * 1024u * 1024u)

typedef struct {
    const char *action;
    int32_t count;
    char *json;
} workflow_case_t;

typedef struct {
    const pal_t *pal;
    void *mutex;
    demo_textbuf_t text;
    demo_textbuf_t output;
    uint64_t deadline;
    const char *action;
    uint8_t *image;
    size_t image_len;
    size_t image_cap;
    uint8_t image_format;
    int image_started;
    int image_done;
    int active;
    int done;
    int meaningful;
    int failed;
} demo_ctx_t;

static volatile sig_atomic_t interrupted;

static void on_signal(int signo)
{
    (void)signo;
    interrupted = 1;
}

static void usage(const char *program)
{
    printf("Usage: %s [options]\n"
           "  --case ACTION COUNT  Repeatable; overrides chat 1, generate-img 1, decide 1\n"
           "                       COUNT is a signed 32-bit decimal integer\n"
           "  --text TEXT          Nonempty prompt reused for every case\n"
           "                       Default: an action-specific prompt\n"
           "  --agent-code CODE    Optional Agent code (default: product-bound Agent)\n"
           "  --region REGION      AY (default), AZ, UEAZ, EU, WEAZ, IN, SG; PROD only\n"
           "  --timeout SECONDS    Per-response deadline, 1..3600 (default: 60)\n"
           "  --help               Show help without credentials or networking\n"
           "Required environment: TUYA_DEVICE_ID, TUYA_SECRET_KEY, TUYA_LOCAL_KEY\n"
           "Device ID: 1..31 bytes; keys: 16..31 bytes. No automatic replay.\n"
           "Images are saved as workflow-image-XXXXXX in the current directory.\n"
           "Exit 0 means expected response types received, NOT branch verification.\n",
           program);
}

static int parse_int32(const char *s, int32_t *value)
{
    const char *p = s;
    if (*p == '-' || *p == '+') p++;
    if (!*p) return -1;
    for (; *p; p++)
        if (*p < '0' || *p > '9') return -1;
    errno = 0;
    char *end;
    long long n = strtoll(s, &end, 10);
    if (errno == ERANGE || *end || n < INT32_MIN || n > INT32_MAX) return -1;
    *value = (int32_t)n;
    return 0;
}

static char *build_workflow_params(const char *action, int32_t count)
{
    cJSON *obj = cJSON_CreateObject();
    if (!obj) return NULL;

    cJSON *action_param = cJSON_AddObjectToObject(obj, "action");
    cJSON *count_param = cJSON_AddObjectToObject(obj, "count");
    char *json = NULL;
    if (action_param && count_param &&
        cJSON_AddStringToObject(action_param, "value", action) &&
        cJSON_AddNumberToObject(count_param, "value", count))
        json = cJSON_PrintUnformatted(obj);
    cJSON_Delete(obj);
    return json;
}

static const char *default_prompt(const char *action)
{
    if (strcmp(action, "chat") == 0)
        return "Describe a small orange cat sitting beside a sunny window.";
    if (strcmp(action, "generate-img") == 0)
        return "Create an image of a small orange cat sitting beside a sunny window.";
    if (strcmp(action, "decide") == 0)
        return "Is a cat an animal? Return the decision in the boolean result field.";
    return DEFAULT_TEXT;
}

static int parse_region(const char *s, iot_region_t *region)
{
    static const struct { const char *name; iot_region_t value; } regions[] = {
        {"AY", AY}, {"AZ", AZ}, {"UEAZ", UEAZ}, {"EU", EU},
        {"WEAZ", WEAZ}, {"IN", IN}, {"SG", SG}
    };
    for (size_t i = 0; i < sizeof(regions) / sizeof(regions[0]); i++) {
        if (strcmp(s, regions[i].name) == 0) {
            *region = regions[i].value;
            return 0;
        }
    }
    return -1;
}

static int read_credential(const char *name, char *out, size_t cap, size_t min)
{
    const char *value = getenv(name);
    if (!value) value = out;
    if (strlen(value) < min) {
        fprintf(stderr, "%s is required (at least %zu bytes)\n", name, min);
        return -1;
    }
    return value == out ? 0 : demo_copy_field(out, cap, value, name);
}

static int has_text(const char *text, size_t len)
{
    for (size_t i = 0; i < len; i++)
        if ((unsigned char)text[i] > ' ') return 1;
    return 0;
}

static void accumulate_output(demo_ctx_t *dc, const char *text, size_t len)
{
    tai_text_msg_t chunk = {
        .text = text, .len = len, .stream_flag = TAI_STREAM_MIDDLE,
        .seq = dc->output.seq + 1
    };
    if (demo_textbuf_accum(&dc->output, &chunk) < 0) dc->failed = 1;
}

static int print_workflow_result(demo_textbuf_t *output)
{
    demo_textbuf_flush(output);
    if (!output->len || output->dropped ||
        memchr(output->buf, '\0', output->len)) {
        fprintf(stderr, "Workflow JSON is empty, contains NUL, or was dropped\n");
        return -1;
    }
    cJSON *root = cJSON_ParseWithLengthOpts(output->buf, output->len + 1, NULL, 1);
    if (!cJSON_IsObject(root)) {
        fprintf(stderr, "Workflow output must be one complete JSON object (no trailing text)\n");
        cJSON_Delete(root);
        return -1;
    }
    if (!cJSON_IsBool(cJSON_GetObjectItemCaseSensitive(root, "result"))) {
        fprintf(stderr, "decide output requires a boolean result field\n");
        cJSON_Delete(root);
        return -1;
    }
    puts("----------------------");
    puts("[Workflow JSON parsed]");
    const cJSON *field;
    cJSON_ArrayForEach(field, root) {
        const char *type = cJSON_IsString(field) ? "string" :
                           cJSON_IsNumber(field) ? "number" :
                           cJSON_IsBool(field) ? "boolean" :
                           cJSON_IsNull(field) ? "null" :
                           cJSON_IsArray(field) ? "array" : "object";
        char *value = cJSON_PrintUnformatted(field);
        if (!value) {
            fprintf(stderr, "Cannot format workflow JSON field\n");
            cJSON_Delete(root);
            return -1;
        }
        printf("  %s (%s): %s\n", field->string, type, value);
        cJSON_free(value);
    }
    puts("----------------------");
    cJSON_Delete(root);
    return 0;
}

/* Called under the application mutex. Display content without the NLG envelope. */
static void print_text(demo_ctx_t *dc, const char *text, size_t len)
{
    size_t raw_len = 0;
    const char *raw = nlg_extract_content(text, len, &raw_len);
    if (raw) {
        char *decoded = malloc(raw_len + 1);
        int n = decoded ? json_unescape(raw, raw_len, decoded, raw_len + 1) : -1;
        if (n < 0) {
            fprintf(stderr, "Cannot decode NLG content\n");
            dc->failed = 1;
        } else {
            dc->meaningful |= has_text(decoded, (size_t)n);
            accumulate_output(dc, decoded, (size_t)n);
        }
        free(decoded);
        fputs("[NLG content] ", stdout);
        nlg_print_content(text, len);
        fputc('\n', stdout);
    } else {
        fwrite(text, 1, len, stdout);
        fputc('\n', stdout);
        dc->meaningful |= has_text(text, len);
        accumulate_output(dc, text, len);
    }
    fflush(stdout);
}

static void on_text(tai_ctx_t *ctx, const tai_text_msg_t *msg, void *ud)
{
    (void)ctx;
    demo_ctx_t *dc = ud;
    dc->pal->mutex_lock(dc->mutex);
    if (dc->active && !dc->failed) {
        size_t raw_len;
        /* Complete NLG lines can arrive on MIDDLE chunks. Other text must be
         * reassembled before interpreting it, including fragmented NLG JSON. */
        if ((dc->text.len == 0 || dc->text.done) &&
            nlg_extract_content(msg->text, msg->len, &raw_len)) {
            print_text(dc, msg->text, msg->len);
        } else if (demo_textbuf_accum(&dc->text, msg) == 1) {
            print_text(dc, dc->text.buf, dc->text.len);
        }
        if (dc->text.dropped) dc->failed = 1;
    }
    dc->pal->mutex_unlock(dc->mutex);
}

static void on_image(tai_ctx_t *ctx, const tai_image_msg_t *msg, void *ud)
{
    (void)ctx;
    demo_ctx_t *dc = ud;
    dc->pal->mutex_lock(dc->mutex);
    if (!dc->active || dc->failed) goto out;
    if (msg->stream_flag == TAI_STREAM_START ||
        msg->stream_flag == TAI_STREAM_ONE_SHOT) {
        if (dc->image_started || dc->image_done) {
            fprintf(stderr, "Expected one image stream per Event\n");
            dc->failed = 1;
            goto out;
        }
        dc->image_started = 1;
        dc->image_format = msg->format;
    }
    if (!dc->image_started || msg->len > MAX_IMAGE_BYTES - dc->image_len) {
        fprintf(stderr, "Image stream missing START or exceeds 16 MiB\n");
        dc->failed = 1;
        goto out;
    }
    size_t need = dc->image_len + msg->len;
    if (need > dc->image_cap) {
        size_t cap = dc->image_cap ? dc->image_cap : 4096;
        while (cap < need) cap *= 2;
        uint8_t *image = realloc(dc->image, cap);
        if (!image) {
            fprintf(stderr, "Cannot buffer generated image\n");
            dc->failed = 1;
            goto out;
        }
        dc->image = image;
        dc->image_cap = cap;
    }
    if (msg->len) memcpy(dc->image + dc->image_len, msg->data, msg->len);
    dc->image_len = need;
    if (msg->stream_flag == TAI_STREAM_END ||
        msg->stream_flag == TAI_STREAM_ONE_SHOT) {
        dc->image_started = 0;
        dc->image_done = dc->image_len != 0;
        if (!dc->image_done) dc->failed = 1;
    }
out:
    dc->pal->mutex_unlock(dc->mutex);
}

/* The owning thread saves only after EventEnd; callbacks no longer mutate it. */
static int save_image(const demo_ctx_t *dc)
{
    char path[] = "workflow-image-XXXXXX";
    int fd = mkstemp(path);
    if (fd < 0) {
        perror("Cannot create image file");
        return -1;
    }
    FILE *file = fdopen(fd, "wb");
    if (!file) {
        close(fd);
        unlink(path);
        return -1;
    }
    int failed = fwrite(dc->image, 1, dc->image_len, file) != dc->image_len;
    if (fclose(file) != 0) failed = 1;
    if (failed) {
        fprintf(stderr, "Cannot save complete image\n");
        unlink(path);
        return -1;
    }
    printf("[Workflow image saved] %s (%zu bytes, %s)\n", path, dc->image_len,
           dc->image_format == TAI_IMG_PNG ? "PNG" :
           dc->image_format == TAI_IMG_JPEG ? "JPEG" : "unknown format");
    return 0;
}

static void on_event(tai_ctx_t *ctx, const tai_event_msg_t *msg, void *ud)
{
    demo_ctx_t *dc = ud;
    if (msg->event_type == TAI_EVT_MCP_CMD) {
        /* Never hold the application mutex across an SDK send. */
        demo_mcp_reply_no_tools(ctx, msg);
        return;
    }
    dc->pal->mutex_lock(dc->mutex);
    printf("[Event type=%u id=%s]\n", (unsigned)msg->event_type,
           msg->event_id ? msg->event_id : "");
    if (msg->len) {
        fputs("[Event payload] ", stdout);
        fwrite(msg->data, 1, msg->len, stdout);
        fputc('\n', stdout);
    }
    if (msg->event_type == TAI_EVT_CHAT_BREAK ||
        msg->event_type == TAI_EVT_SERVER_TIMEOVER) {
        fprintf(stderr, "Interrupted or timed out by server; not replaying\n");
        dc->failed = 1;
    } else if (msg->event_type == TAI_EVT_END && dc->active) {
        if (demo_textbuf_flush(&dc->text))
            print_text(dc, dc->text.buf, dc->text.len);
        if (dc->pal->time_ms() >= dc->deadline) {
            fprintf(stderr, "EventEnd arrived after the response deadline\n");
            dc->failed = 1;
        }
        int expects_image = strcmp(dc->action, "generate-img") == 0;
        if (dc->text.dropped || dc->output.dropped || dc->image_started ||
            (expects_image ? !dc->image_done : !dc->meaningful)) {
            fprintf(stderr, "EventEnd without the expected complete %s response\n",
                    expects_image ? "image" : "text");
            dc->failed = 1;
        }
        if (!dc->failed && strcmp(dc->action, "decide") == 0 &&
            print_workflow_result(&dc->output) != 0)
            dc->failed = 1;
        dc->done = 1;
        dc->active = 0;
    }
    fflush(stdout);
    dc->pal->mutex_unlock(dc->mutex);
}

static void on_disconnect(tai_ctx_t *ctx, const tai_disconnect_msg_t *msg, void *ud)
{
    (void)ctx;
    demo_ctx_t *dc = ud;
    dc->pal->mutex_lock(dc->mutex);
    fprintf(stderr, "Disconnected: reason=%u code=%u detail=%u; not replaying\n",
            (unsigned)msg->reason, (unsigned)msg->close_code, (unsigned)msg->detail);
    dc->failed = 1;
    dc->pal->mutex_unlock(dc->mutex);
}

int main(int argc, char **argv)
{
    int result = 1;
    size_t case_count = 0, completed = 0;
    workflow_case_t *cases = calloc((size_t)argc + 3, sizeof(*cases));
    if (!cases) { fprintf(stderr, "Out of memory\n"); return 1; }
    const char *text = NULL;
    const char *agent_code = NULL;
    int32_t timeout = DEFAULT_TIMEOUT;
    iot_client_config_t iot_cfg = {
        .region = AY, .env = PROD, .mqtt_disable_auto_connect = true
    };
    const pal_t *pal = NULL;
    iot_client_t *iot = NULL;
    tai_ctx_t *ctx = NULL;
    void *ctx_mem = NULL;
    demo_ctx_t dc = {0};
    tai_conn_params_t cp;
    char token[4096] = {0};

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0) {
            usage(argv[0]);
            result = 0;
            goto cleanup;
        } else if (strcmp(argv[i], "--case") == 0) {
            if (i + 2 >= argc || !argv[i + 1][0] ||
                parse_int32(argv[i + 2], &cases[case_count].count) != 0) {
                fprintf(stderr, "--case requires a nonempty ACTION and signed 32-bit COUNT\n");
                goto cleanup;
            }
            cases[case_count++].action = argv[i + 1];
            i += 2;
        } else if (strcmp(argv[i], "--text") == 0 ||
                   strcmp(argv[i], "--agent-code") == 0) {
            if (i + 1 >= argc || !argv[i + 1][0]) {
                fprintf(stderr, "%s requires a nonempty value\n", argv[i]);
                goto cleanup;
            }
            if (strcmp(argv[i], "--text") == 0) text = argv[i + 1];
            else agent_code = argv[i + 1];
            i++;
        } else if (strcmp(argv[i], "--region") == 0) {
            if (i + 1 >= argc || parse_region(argv[i + 1], &iot_cfg.region) != 0) {
                fprintf(stderr, "--region requires AY, AZ, UEAZ, EU, WEAZ, IN or SG\n");
                goto cleanup;
            }
            i++;
        } else if (strcmp(argv[i], "--timeout") == 0) {
            if (i + 1 >= argc || parse_int32(argv[i + 1], &timeout) != 0 ||
                timeout < 1 || timeout > MAX_TIMEOUT) {
                fprintf(stderr, "--timeout requires an integer from 1 to 3600\n");
                goto cleanup;
            }
            i++;
        } else {
            fprintf(stderr, "Unknown option: %s (see --help)\n", argv[i]);
            goto cleanup;
        }
    }
    if (!case_count) {
        cases[0] = (workflow_case_t){.action = "chat", .count = 1};
        cases[1] = (workflow_case_t){.action = "generate-img", .count = 1};
        cases[2] = (workflow_case_t){.action = "decide", .count = 1};
        case_count = 3;
    }

    if (read_credential("TUYA_DEVICE_ID", iot_cfg.devid,
                        sizeof(iot_cfg.devid), 1) != 0 ||
        read_credential("TUYA_SECRET_KEY", iot_cfg.secret_key,
                        sizeof(iot_cfg.secret_key), 16) != 0 ||
        read_credential("TUYA_LOCAL_KEY", iot_cfg.local_key,
                        sizeof(iot_cfg.local_key), 16) != 0)
        goto cleanup;

    pal = tai_pal_posix();
    if (iot_init(pal) != OPRT_OK) {
        fprintf(stderr, "iot_init failed\n");
        goto cleanup;
    }
    for (size_t i = 0; i < case_count; i++) {
        cases[i].json = build_workflow_params(cases[i].action, cases[i].count);
        if (!cases[i].json) {
            fprintf(stderr, "Cannot serialize workflow inputs\n");
            goto cleanup;
        }
    }
    dc.pal = pal;
    dc.mutex = pal->mutex_create();
    if (!dc.mutex) { fprintf(stderr, "Cannot create mutex\n"); goto cleanup; }
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    fprintf(stderr, "Use a WARN-level SDK build before supplying credentials; redact logs before sharing.\n");
    iot = iot_client_init(&iot_cfg);
    if (!iot) { fprintf(stderr, "iot_client_init failed\n"); goto cleanup; }
    iot_atop_rejection_t rejection = {0};
    int rc = iot_client_get_session_token_ex(iot, agent_code, token, sizeof(token), &rejection);
    if (rc != OPRT_OK || !token[0]) {
        fprintf(stderr, "Session token failed: rc=%d code=%s message=%s\n",
                rc, rejection.code, rejection.msg);
        fprintf(stderr, "Check activated credentials, region, product Agent and privacy agreement.\n");
        goto cleanup;
    }
    if (parse_token(token, &cp) != 0 || !cp.derived_client_id[0] || !cp.agent_token[0]) {
        fprintf(stderr, "Token lacks usable connection identity or Agent token\n");
        goto cleanup;
    }
    iot_client_deinit(iot);
    iot = NULL;
    if (interrupted) goto cleanup;

    tai_config_t cfg = {
        .host = cp.host, .port = cp.port, .tls_sni = cp.tls_sni,
        .device_id = cp.derived_client_id, .local_key = iot_cfg.local_key,
        .protocol_version = TAI_VER_21, .client_type = TAI_CLIENT_DEVICE,
        .sign_level = TAI_SIGN_HMAC_SHA256,
        .biz_code = (uint32_t)cp.biz_code, .biz_tag = (uint64_t)cp.biz_tag,
        .agent_token = cp.agent_token,
        .event_custom_param_json = cases[0].json,
        .pal = pal, .on_text = on_text, .on_image = on_image, .on_event = on_event,
        .on_disconnect = on_disconnect, .user_data = &dc
    };
    ctx_mem = pal->malloc(tai_ctx_size());
    if (!ctx_mem || !(ctx = tai_ctx_init(ctx_mem, &cfg))) {
        fprintf(stderr, "Cannot initialize TAI context\n");
        goto cleanup;
    }
    rc = tai_connect(ctx);
    if (rc != TAI_OK) {
        fprintf(stderr, "tai_connect failed: %d\n", rc);
        goto cleanup;
    }
    for (size_t i = 0; i < case_count && !interrupted; i++) {
        pal->mutex_lock(dc.mutex);
        int failed = dc.failed;
        uint64_t deadline = pal->time_ms() + (uint64_t)timeout * 1000;
        dc.deadline = deadline;
        dc.active = !failed;
        dc.done = dc.meaningful = 0;
        dc.action = cases[i].action;
        dc.image_len = 0;
        dc.image_started = dc.image_done = 0;
        dc.image_format = 0;
        demo_textbuf_reset(&dc.text);
        demo_textbuf_reset(&dc.output);
        pal->mutex_unlock(dc.mutex);
        if (failed) break;
        if (i) {
            /* NULL restores built-in chat attributes; it is not "unchanged". */
            tai_set_event_params(ctx, NULL, cases[i].json);
        }
        const char *prompt = text ? text : default_prompt(cases[i].action);
        printf("\n[case %zu/%zu] inputs=%s\n[USER_TEXT] %s\n",
               i + 1, case_count, cases[i].json, prompt);
        fflush(stdout);
        rc = tai_send_text(ctx, prompt, strlen(prompt));
        if (rc != TAI_OK) {
            fprintf(stderr, "tai_send_text failed: %d; not replaying\n", rc);
            break;
        }
        int done = 0;
        while (!interrupted) {
            pal->mutex_lock(dc.mutex);
            done = dc.done;
            failed = dc.failed;
            pal->mutex_unlock(dc.mutex);
            if (done || failed) break;
            if (pal->time_ms() >= deadline) {
                fprintf(stderr, "Response deadline exceeded (%d seconds)\n", (int)timeout);
                break;
            }
            pal->sleep_ms(20);
        }
        if (!done || failed || interrupted) break;
        if (dc.image_done && save_image(&dc) != 0) break;
        completed++;
    }

cleanup:
    /* Join before inspecting final state or releasing borrowed callback data. */
    if (ctx) {
        tai_disconnect(ctx);
        if (completed == case_count && !dc.failed && !interrupted) result = 0;
        tai_ctx_deinit(ctx);
    }
    if (interrupted) { fprintf(stderr, "Interrupted; not replaying\n"); result = 1; }
    if (ctx_mem) pal->free(ctx_mem);
    if (iot) iot_client_deinit(iot);
    if (dc.mutex) pal->mutex_destroy(dc.mutex);
    demo_textbuf_free(&dc.text);
    demo_textbuf_free(&dc.output);
    free(dc.image);
    for (size_t i = 0; i < case_count; i++)
        if (cases[i].json) cJSON_free(cases[i].json);
    free(cases);
    if (ctx_mem)
        printf("Responses received: %zu/%zu; %s. Workflow branch/count verification requires cloud evidence.\n",
               completed, case_count, result == 0 ? "completed" : "failed");
    return result;
}
