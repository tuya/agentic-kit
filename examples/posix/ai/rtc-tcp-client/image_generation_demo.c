/*
 * Configure: cmake -S examples/posix -B build-examples -DCMAKE_C_FLAGS="-DAGENTIC_KIT_LOG_LEVEL=2"
 * Build: cmake --build build-examples --target image_generation_demo
 * Rebuild all SDK C libraries with this WARN ceiling before using credentials:
 * DEBUG response logs and INFO Packet logs may expose the Session token.
 * Run: image_generation_demo --prompt "Generate a red panda" --ca-file trust.pem
 *   or, accepting unverified TLS (MITM risk): ... --allow-insecure-fallback
 * Credentials: TUYA_DEVICE_ID, TUYA_SECRET_KEY, TUYA_LOCAL_KEY (activated device).
 * Requires an Agent workflow configured for inline text-to-image generation.
 * PROD only; first complete JPEG/PNG, at most 16 MiB; no download or replay.
 */
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "iot_client.h"
#include "iot_atop.h"
#include "tuya_ai.h"
#include "cJSON.h"
#include "mbedtls/platform_util.h"
#include "demo_json.h"
#include "demo_mcp.h"

#define IMAGE_LIMIT (16u * 1024u * 1024u)
#define CA_LIMIT (256u * 1024u)

typedef struct {
    const char *prompt, *ca_file, *output_dir;
    unsigned timeout;
    iot_region_t region;
    int allow_insecure; /* explicit opt-in: use TLS without a CA */
} options_t;

typedef struct {
    pthread_mutex_t mutex;
    int outcome; /* 0 waiting, 1 complete, -1 failed; terminal states are immutable */
    const char *error;
    unsigned char *bytes;
    size_t len, capacity;
    int active;
    uint8_t format;
    uint16_t width, height, data_id;
    char event_id[256];
} image_state_t;

static volatile sig_atomic_t cancelled;

static void on_signal(int sig)
{
    (void)sig;
    cancelled = 1;
}

static void usage(void)
{
    puts("Usage: image_generation_demo --prompt TEXT [--ca-file FILE | --allow-insecure-fallback] [options]\n"
         "  --ca-file FILE       Trusted CA bundle for bootstrap HTTPS and TAI\n"
         "  --allow-insecure-fallback\n"
         "                       Permit TLS without a CA; peer unverified, MITM risk\n"
         "  --output-dir DIR     Default ./generated-images; parent must exist\n"
         "  --timeout SECONDS    Response wait after sending (default 120)\n"
         "  --region REGION      AY (default), AZ, UEAZ, EU, WEAZ, IN, SG; PROD\n"
         "  --help               Show this help\n"
         "Required environment: TUYA_DEVICE_ID, TUYA_SECRET_KEY, TUYA_LOCAL_KEY.\n"
         "The CA must trust bootstrap HTTPS and TAI. Without --ca-file, TLS is\n"
         "unverified and requires the explicit --allow-insecure-fallback opt-in.\n"
         "A supplied --ca-file is always verified, even with the fallback flag.\n"
         "Before using credentials, rebuild all SDK C libraries at WARN ceiling:\n"
         "  -DCMAKE_C_FLAGS=\"-DAGENTIC_KIT_LOG_LEVEL=2\"\n"
         "DEBUG response logs and INFO Packet logs may expose the Session token.\n"
         "Saves the first complete inline JPEG/PNG, max 16 MiB. URL-only output\n"
         "is unsupported. No reconnect/resubmit. Exit 0 only after publication.\n"
         "Requires an activated device and a configured text-to-image workflow;\n"
         "sending a prompt alone does not configure that workflow.\n"
         "All devices use their product's single bound Agent; no Agent override.");
}

static int parse_options(int argc, char **argv, options_t *o)
{
    *o = (options_t){.output_dir = "./generated-images", .timeout = 120, .region = AY};
    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];
        if (!strcmp(arg, "--help")) return 1;
        if (!strcmp(arg, "--allow-insecure-fallback")) { o->allow_insecure = 1; continue; }
        if (++i == argc || !argv[i][0]) return -1;
        const char *value = argv[i];
        if (!strcmp(arg, "--prompt")) o->prompt = value;
        else if (!strcmp(arg, "--ca-file")) o->ca_file = value;
        else if (!strcmp(arg, "--output-dir")) o->output_dir = value;
        else if (!strcmp(arg, "--timeout")) {
            char *end;
            errno = 0;
            unsigned long n = strtoul(value, &end, 10);
            if (*value < '0' || *value > '9' || *end || errno || !n || n > UINT_MAX)
                return -1;
            o->timeout = (unsigned)n;
        } else if (!strcmp(arg, "--region")) {
            const char *names[] = {"AY", "AZ", "UEAZ", "EU", "WEAZ", "IN", "SG"};
            size_t n;
            for (n = 0; n < sizeof(names) / sizeof(names[0]); n++)
                if (!strcmp(value, names[n])) break;
            if (n == sizeof(names) / sizeof(names[0])) return -1;
            o->region = (iot_region_t)n;
        } else return -1;
    }
    return o->prompt && (o->ca_file || o->allow_insecure) ? 0 : -1;
}

static int credentials(iot_client_config_t *cfg)
{
    const char *names[] = {"TUYA_DEVICE_ID", "TUYA_SECRET_KEY", "TUYA_LOCAL_KEY"};
    char *fields[] = {cfg->devid, cfg->secret_key, cfg->local_key};
    size_t sizes[] = {sizeof(cfg->devid), sizeof(cfg->secret_key), sizeof(cfg->local_key)};
    for (size_t i = 0; i < 3; i++) {
        const char *value = getenv(names[i]);
        if (!value || !*value) {
            fprintf(stderr, "Required environment variable missing: %s\n", names[i]);
            return -1;
        }
        if (demo_copy_field(fields[i], sizes[i], value, names[i])) return -1;
    }
    return 0;
}

static int prepare_directory(const char *path)
{
    struct stat st;
    if (mkdir(path, 0700) && errno != EEXIST) return -1;
    if (stat(path, &st)) return -1;
    if (!S_ISDIR(st.st_mode)) { errno = ENOTDIR; return -1; }
    return access(path, W_OK | X_OK);
}

static char *read_ca(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    char *ca = malloc(CA_LIMIT + 1);
    size_t n = ca ? fread(ca, 1, CA_LIMIT + 1, f) : 0;
    int bad = !ca || !n || n > CA_LIMIT || ferror(f) || (ca && memchr(ca, 0, n));
    if (fclose(f)) bad = 1;
    if (bad) { free(ca); errno = EINVAL; return NULL; }
    ca[n] = 0;
    return ca;
}

/* Reject decoded NULs in every value and field name before cJSON allocates
 * copies that strlen-based validation and cleanup could truncate. */
static int validate_json_strings(const char *json)
{
    if (!json) return -1;
    while (*json) {
        if (*json != '"') { json++; continue; }
        const char *body = json + 1;
        const char *end = json_str_end(body);
        char probe[1];
        if (!end || json_unescape_ex(body, (size_t)(end - body), probe, sizeof(probe), 1) < 0)
            return -1;
        json = end + 1;
    }
    return 0; /* cJSON enforces the surrounding syntax. */
}

/* cJSON owns decoded copies, including strings unrelated to the expected field. */
static void wipe_json(cJSON *node)
{
    for (; node; node = node->next) {
        wipe_json(node->child);
        if (node->valuestring) mbedtls_platform_zeroize(node->valuestring, strlen(node->valuestring));
        if (node->string) mbedtls_platform_zeroize(node->string, strlen(node->string));
    }
}

static int copy_json_string(cJSON *node, char *out, size_t cap)
{
    if (!cJSON_IsString(node) || !node->valuestring || !node->valuestring[0] ||
        strlen(node->valuestring) >= cap) return -1;
    strcpy(out, node->valuestring);
    return 0;
}

/* Unlike parse_token(), reject missing/invalid ports instead of defaulting to 443. */
static int parse_connection(const char *token, tai_conn_params_t *p)
{
    size_t decoded_len = 0;
    char *decoded = b64_decode(token, &decoded_len);
    const char *json = decoded ? decoded : token;
    cJSON *root = NULL;
    int rc = -1;
    memset(p, 0, sizeof(*p));
    if (decoded && memchr(decoded, 0, decoded_len)) goto done;
    if (validate_json_strings(json)) goto done;
    root = cJSON_ParseWithOpts(json, NULL, 1);
    cJSON *conn = cJSON_GetObjectItemCaseSensitive(root, "connect_conf");
    cJSON *sess = cJSON_GetObjectItemCaseSensitive(root, "session_conf");
    cJSON *hosts = cJSON_GetObjectItemCaseSensitive(conn, "hosts");
    cJSON *domains = cJSON_GetObjectItemCaseSensitive(conn, "domains");
    cJSON *port = cJSON_GetObjectItemCaseSensitive(conn, "ecc_tls_port");
    if (!port) port = cJSON_GetObjectItemCaseSensitive(conn, "tcpport");
    if (!cJSON_IsObject(root) || !cJSON_IsObject(conn) || !cJSON_IsObject(sess) ||
        !cJSON_IsArray(hosts) ||
        copy_json_string(cJSON_GetArrayItem(hosts, 0), p->host, sizeof(p->host)) ||
        copy_json_string(cJSON_GetObjectItemCaseSensitive(conn, "derived_client_id"),
                         p->derived_client_id, sizeof(p->derived_client_id)) ||
        copy_json_string(cJSON_GetObjectItemCaseSensitive(sess, "agentToken"),
                         p->agent_token, sizeof(p->agent_token)) ||
        !cJSON_IsNumber(port) || port->valuedouble < 1 || port->valuedouble > 65535 ||
        port->valuedouble != port->valueint) goto done;
    p->port = (uint16_t)port->valueint;
    if (domains) {
        if (!cJSON_IsArray(domains) ||
            copy_json_string(cJSON_GetArrayItem(domains, 0), p->tls_sni, sizeof(p->tls_sni))) goto done;
    } else strcpy(p->tls_sni, p->host);
    cJSON *biz = cJSON_GetObjectItemCaseSensitive(sess, "bizConfig");
    cJSON *code = cJSON_GetObjectItemCaseSensitive(biz, "bizCode");
    cJSON *tag = cJSON_GetObjectItemCaseSensitive(biz, "bizTag");
    if (cJSON_IsNumber(code) && code->valuedouble >= 0 && code->valuedouble <= INT_MAX)
        p->biz_code = code->valueint;
    if (cJSON_IsNumber(tag) && tag->valuedouble >= 0 && tag->valuedouble < (double)LONG_MAX)
        p->biz_tag = (long)tag->valuedouble;
    rc = 0;
 done:
    wipe_json(root);
    cJSON_Delete(root);
    if (decoded) { mbedtls_platform_zeroize(decoded, decoded_len); free(decoded); }
    if (rc) mbedtls_platform_zeroize(p, sizeof(*p));
    return rc;
}

static int decode_image_key(const char *text, uint8_t key[TAI_IMAGE_KEY_LEN])
{
    static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t len = 0;
    if (!text || strlen(text) != 24 || text[22] != '=' || text[23] != '=') return -1;
    for (size_t i = 0; i < 22; i++) if (!strchr(alphabet, text[i])) return -1;
    if ((strchr(alphabet, text[21]) - alphabet) & 15) return -1;
    return mbedtls_base64_decode(key, TAI_IMAGE_KEY_LEN, &len,
                                (const unsigned char *)text, 24) == 0 &&
           len == TAI_IMAGE_KEY_LEN ? 0 : -1;
}

static int fetch_image_key(iot_client_t *client, uint8_t key[TAI_IMAGE_KEY_LEN])
{
    iot_atop_request_t req = {.api = "thing.km.secret.get.device", .version = "1.0", .data = "{}"};
    iot_atop_response_t resp = {0};
    cJSON *root = NULL;
    int rc = iot_atop_call(client, &req, &resp);
    if (rc == OPRT_OK) {
        if (!validate_json_strings(resp.result))
            root = cJSON_ParseWithOpts(resp.result, NULL, 1);
        cJSON *secret = cJSON_GetObjectItemCaseSensitive(root, "secretValue");
        char encoded[25] = {0};
        if (!cJSON_IsObject(root) || !cJSON_IsString(secret) ||
            json_object_get_string(resp.result, "secretValue", encoded, sizeof(encoded)) ||
            decode_image_key(encoded, key)) rc = OPRT_INVALID_RESULT;
        mbedtls_platform_zeroize(encoded, sizeof(encoded));
    }
    wipe_json(root);
    cJSON_Delete(root);
    if (resp.result) mbedtls_platform_zeroize(resp.result, strlen(resp.result));
    mbedtls_platform_zeroize(resp.error_code, sizeof(resp.error_code));
    mbedtls_platform_zeroize(resp.error_msg, sizeof(resp.error_msg));
    iot_atop_response_free(client, &resp);
    if (rc) {
        mbedtls_platform_zeroize(key, TAI_IMAGE_KEY_LEN);
        fprintf(stderr, "Image-key acquisition failed: %d\n", rc);
    }
    return rc;
}

static void discard_image(image_state_t *s)
{
    free(s->bytes);
    s->bytes = NULL;
    s->len = s->capacity = 0;
    s->active = 0;
}

static void fail_image(image_state_t *s, const char *error)
{
    if (s->outcome) return;
    discard_image(s);
    s->error = error;
    s->outcome = -1;
}

static void on_image(tai_ctx_t *ctx, const tai_image_msg_t *msg, void *user)
{
    image_state_t *s = user;
    const char *id = msg->event_id ? msg->event_id : "";
    pthread_mutex_lock(&s->mutex);
    if (s->outcome) goto done;
    if (msg->payload_type != TAI_IMG_PAYLOAD_RAW) {
        fail_image(s, msg->payload_type == TAI_IMG_PAYLOAD_URL ?
                   "URL image delivery is unsupported (no download)" : "Expected plaintext RAW image bytes");
        goto done;
    }
    if (msg->stream_flag == TAI_STREAM_START || msg->stream_flag == TAI_STREAM_ONE_SHOT) {
        discard_image(s);
        if (strlen(id) >= sizeof(s->event_id)) {
            fail_image(s, "Image Event ID too long"); goto done;
        }
        strcpy(s->event_id, id);
        s->data_id = msg->data_id;
        s->format = msg->format;
        s->width = msg->width;
        s->height = msg->height;
        s->active = 1;
    } else if ((msg->stream_flag != TAI_STREAM_MIDDLE && msg->stream_flag != TAI_STREAM_END) ||
               !s->active || strcmp(id, s->event_id) || msg->data_id != s->data_id ||
               (msg->format && msg->format != s->format) ||
               (msg->width && msg->width != s->width) ||
               (msg->height && msg->height != s->height)) {
        fail_image(s, "Orphan or conflicting Image Stream continuation"); goto done;
    }
    if (msg->len > IMAGE_LIMIT - s->len) {
        fail_image(s, "Image exceeds 16 MiB limit"); goto done;
    }
    if (msg->len && !msg->data) { fail_image(s, "Missing image bytes"); goto done; }
    size_t needed = s->len + msg->len;
    if (needed > s->capacity) {
        size_t cap = s->capacity ? s->capacity : 4096;
        while (cap < needed) cap *= 2;
        unsigned char *bytes = realloc(s->bytes, cap);
        if (!bytes) { fail_image(s, "Image allocation failed"); goto done; }
        s->bytes = bytes;
        s->capacity = cap;
    }
    if (msg->len) memcpy(s->bytes + s->len, msg->data, msg->len);
    s->len = needed;
    if (msg->stream_flag == TAI_STREAM_END || msg->stream_flag == TAI_STREAM_ONE_SHOT) {
        if (!s->len) fail_image(s, "Empty image");
        else { s->outcome = 1; s->active = 0; }
    }
 done:;
    int terminal = s->outcome;
    pthread_mutex_unlock(&s->mutex);
    if (terminal) tai_request_disconnect(ctx);
}

static void on_event(tai_ctx_t *ctx, const tai_event_msg_t *msg, void *user)
{
    image_state_t *s = user;
    pthread_mutex_lock(&s->mutex);
    if (!s->outcome) {
        /* The cloud ends the request Event as soon as it accepts it -- long
         * before a slow (10-30 s) text-to-image result arrives in the response
         * Event. Only an EventEnd that cuts off an image already streaming is a
         * failure; otherwise keep waiting for the image or the timeout. */
        if (msg->event_type == TAI_EVT_END) {
            if (s->active) fail_image(s, "Event ended without a complete inline image");
        }
        else if (msg->event_type == TAI_EVT_CHAT_BREAK)
            fail_image(s, "Image generation interrupted by ChatBreak");
        else if (msg->event_type == TAI_EVT_SERVER_TIMEOVER)
            fail_image(s, "Server timeover before image completion");
        else if (msg->event_type == TAI_EVT_START && s->active &&
                 strcmp(s->event_id, msg->event_id ? msg->event_id : ""))
            discard_image(s);
    }
    int terminal = s->outcome;
    pthread_mutex_unlock(&s->mutex);
    if (terminal) tai_request_disconnect(ctx);
    else if (msg->event_type == TAI_EVT_MCP_CMD) demo_mcp_reply_no_tools(ctx, msg);
}

static void on_disconnect(tai_ctx_t *ctx, const tai_disconnect_msg_t *msg, void *user)
{
    image_state_t *s = user;
    /* Static because fail_image() stores the pointer; the mutex serializes it.
     * reason==PROTOCOL means our decoder rejected the server's bytes, so the
     * TAI_PROTO_ERR_* detail is the whole diagnosis. */
    static char text[96];
    pthread_mutex_lock(&s->mutex);
    if (msg->reason == TAI_DISCONNECT_PROTOCOL) {
        snprintf(text, sizeof(text),
                 "Protocol/authentication failure before image completion (TAI_PROTO_ERR_%u)",
                 (unsigned)msg->detail);
        fail_image(s, text);
    } else {
        fail_image(s, "Disconnected without a complete inline image");
    }
    pthread_mutex_unlock(&s->mutex);
    tai_request_disconnect(ctx);
}

/* Text and audio are intentionally discarded; neither can complete an image. */
static void on_text(tai_ctx_t *ctx, const tai_text_msg_t *msg, void *user)
{
    (void)ctx; (void)msg; (void)user;
}

static int64_t monotonic_ms(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts)) return -1;
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static int wait_image(image_state_t *s, unsigned timeout)
{
    int64_t now = monotonic_ms();
    int64_t deadline = now + (int64_t)timeout * 1000;
    for (;;) {
        pthread_mutex_lock(&s->mutex);
        if (cancelled) fail_image(s, "Interrupted by signal");
        else if (now < 0) fail_image(s, "Monotonic clock failed");
        else if (now >= deadline) fail_image(s, "Timed out waiting for a complete inline image");
        int outcome = s->outcome;
        pthread_mutex_unlock(&s->mutex);
        if (outcome) return outcome;
        struct timespec pause = {.tv_nsec = 20000000};
        nanosleep(&pause, NULL);
        now = monotonic_ms();
    }
}

static int save_image(const image_state_t *s, const char *directory, char *path, size_t path_cap)
{
    static const unsigned char png[] = {137, 80, 78, 71, 13, 10, 26, 10};
    int format = 0;
    if (s->outcome != 1 || !s->bytes) { errno = EINVAL; return -1; }
    if (s->len >= sizeof(png) && !memcmp(s->bytes, png, sizeof(png))) format = TAI_IMG_PNG;
    else if (s->len >= 3 && s->bytes[0] == 255 && s->bytes[1] == 216 && s->bytes[2] == 255)
        format = TAI_IMG_JPEG;
    if (!format || (s->format && s->format != format)) {
        fprintf(stderr, "Unsupported or mismatched JPEG/PNG signature\n");
        errno = EINVAL; return -1;
    }
    char temporary[PATH_MAX];
    int n = snprintf(temporary, sizeof(temporary), "%s/.image-XXXXXX", directory);
    if (n < 0 || (size_t)n >= sizeof(temporary)) { errno = ENAMETOOLONG; return -1; }
    int fd = mkstemp(temporary);
    if (fd < 0) return -1;
    int rc = -1, saved_errno;
    n = snprintf(path, path_cap, "%s/image-%s.%s", directory,
                 strrchr(temporary, '-') + 1, format == TAI_IMG_PNG ? "png" : "jpg");
    if (n < 0 || (size_t)n >= path_cap) { errno = ENAMETOOLONG; goto done; }
    size_t offset = 0;
    while (offset < s->len) {
        ssize_t written = write(fd, s->bytes + offset, s->len - offset);
        if (written < 0 && errno == EINTR) continue;
        if (written <= 0) { if (!written) errno = EIO; goto done; }
        offset += (size_t)written;
    }
    /* A failed close must not be retried: POSIX may already have released fd. */
    int close_rc = close(fd);
    fd = -1;
    if (close_rc) goto done;
    if (link(temporary, path)) goto done;
    if (unlink(temporary)) {
        saved_errno = errno;
        unlink(path);
        errno = saved_errno;
        goto done;
    }
    rc = 0;
 done:
    saved_errno = errno;
    if (fd >= 0) close(fd);
    if (rc) { unlink(temporary); if (path_cap) path[0] = 0; }
    errno = saved_errno;
    return rc;
}

int main(int argc, char **argv)
{
    options_t opt;
    int parsed = parse_options(argc, argv, &opt);
    if (parsed) { usage(); return parsed > 0 ? 0 : 2; }
    int rc = 1, status, disconnected = 0;
    iot_client_config_t iot_cfg = {0};
    iot_client_t *iot = NULL;
    tai_conn_params_t params = {0};
    char token[8192] = {0}, path[PATH_MAX];
    uint8_t key[TAI_IMAGE_KEY_LEN] = {0};
    char *ca = NULL;
    void *mem = NULL;
    tai_ctx_t *ctx = NULL;
    image_state_t state = {.mutex = PTHREAD_MUTEX_INITIALIZER};
    struct sigaction action = {0}, old_int, old_term;
    int signals = 0;
    if (credentials(&iot_cfg)) goto done;
    if (prepare_directory(opt.output_dir)) { perror("Output directory"); goto done; }
    if (opt.ca_file) {
        ca = read_ca(opt.ca_file);
        if (!ca) { perror("CA file (nonempty, at most 256 KiB)"); goto done; }
    } else {
        fprintf(stderr, "WARNING: no CA file; TLS peer verification is disabled (MITM risk)\n");
    }
    cancelled = 0;
    action.sa_handler = on_signal;
    sigemptyset(&action.sa_mask);
    if (sigaction(SIGINT, &action, &old_int)) goto done;
    if (sigaction(SIGTERM, &action, &old_term)) { sigaction(SIGINT, &old_int, NULL); goto done; }
    signals = 1;
    status = iot_init_default();
    if (status) { fprintf(stderr, "SDK initialization failed: %d\n", status); goto done; }

    iot_cfg.region = opt.region;
    iot_cfg.env = PROD;
    iot_cfg.cacert = ca;
    iot_cfg.mqtt_disable_auto_connect = true;
    iot = iot_client_init(&iot_cfg);
    if (!iot) { fprintf(stderr, "IoT initialization failed\n"); goto done; }
    if (cancelled) goto done;
    /* This SDK implementation propagates TLS errors without refreshing trust,
     * despite the historical refresh claim in iot_client.h. Never retry here. */
    status = iot_client_get_session_token_ex(iot, NULL, token, sizeof(token), NULL);
    if (status) { fprintf(stderr, "Session token request failed: %d\n", status); goto done; }
    if (parse_connection(token, &params)) { fprintf(stderr, "Invalid Session token fields\n"); goto done; }
    mbedtls_platform_zeroize(token, sizeof(token));
    if (cancelled || fetch_image_key(iot, key)) goto done;
    tai_config_t cfg = {
        .host = params.host, .port = params.port, .tls_sni = params.tls_sni,
        .device_id = params.derived_client_id, .local_key = iot_cfg.local_key,
        .client_type = TAI_CLIENT_DEVICE, .protocol_version = TAI_VER_21,
        .agent_token = params.agent_token, .biz_code = (uint32_t)params.biz_code,
        .biz_tag = (uint64_t)params.biz_tag, .pal = iot->pal,
        .image_key = key, .ca_cert = ca,
        .on_image = on_image, .on_event = on_event,
        .on_disconnect = on_disconnect, .on_text = on_text, .user_data = &state
    };
    mem = malloc(tai_ctx_size());
    if (mem) ctx = tai_ctx_init(mem, &cfg);
    mbedtls_platform_zeroize(key, sizeof(key));
    iot_client_deinit(iot);
    iot = NULL;
    if (!ctx) { fprintf(stderr, "TAI construction failed\n"); goto done; }
    if (cancelled) goto done;
    status = tai_connect(ctx);
    if (status) { fprintf(stderr, "TAI Connection failed: %d\n", status); goto done; }
    if (cancelled) goto done;
    status = tai_send_text(ctx, opt.prompt, strlen(opt.prompt));
    if (status) {
        pthread_mutex_lock(&state.mutex);
        fail_image(&state, "Prompt send failed (not retried)");
        pthread_mutex_unlock(&state.mutex);
    } else wait_image(&state, opt.timeout);
    tai_disconnect(ctx);
    disconnected = 1;
    if (state.outcome != 1) {
        fprintf(stderr, "%s\n", state.error ? state.error : "No complete inline image");
        goto done;
    }
    if (save_image(&state, opt.output_dir, path, sizeof(path))) { perror("Save image"); goto done; }
    printf("Saved %s (%zu bytes, %s, dimensions %u x %u; 0 = unknown)\n", path, state.len,
           !strcmp(path + strlen(path) - 4, ".png") ? "PNG" : "JPEG", state.width, state.height);
    rc = 0;
 done:
    if (ctx) {
        if (!disconnected) tai_disconnect(ctx);
        tai_ctx_deinit(ctx);
    }
    if (iot) iot_client_deinit(iot);
    free(mem);
    discard_image(&state);
    pthread_mutex_destroy(&state.mutex);
    free(ca);
    mbedtls_platform_zeroize(key, sizeof(key));
    mbedtls_platform_zeroize(token, sizeof(token));
    mbedtls_platform_zeroize(&params, sizeof(params));
    mbedtls_platform_zeroize(&iot_cfg, sizeof(iot_cfg));
    if (signals) {
        sigaction(SIGINT, &old_int, NULL);
        sigaction(SIGTERM, &old_term, NULL);
    }
    cancelled = 0;
    return rc;
}
