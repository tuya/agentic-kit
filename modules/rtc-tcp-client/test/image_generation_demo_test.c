/* Exercise the demo translation unit, not a second implementation. All SDK
 * bootstrap/receive traffic is synthetic; no subprocess or cloud credentials. */
#include <dirent.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "iot_atop.h"
#include "tuya_ai.h"

static int fake_init_default(void);
static iot_client_t *fake_iot_init(const iot_client_config_t *);
static void fake_iot_deinit(iot_client_t *);
static int fake_token(iot_client_t *, const char *, char *, size_t, iot_atop_rejection_t *);
static int fake_atop(iot_client_t *, const iot_atop_request_t *, iot_atop_response_t *);
static void fake_response_free(iot_client_t *, iot_atop_response_t *);
static tai_ctx_t *fake_ctx_init(void *, const tai_config_t *);
static int fake_connect(tai_ctx_t *);
static int fake_send(tai_ctx_t *, const char *, size_t);
static void fake_request_disconnect(tai_ctx_t *);
static void fake_disconnect(tai_ctx_t *);
static void fake_ctx_deinit(tai_ctx_t *);
static void *fake_realloc(void *, size_t);
static ssize_t fake_write(int, const void *, size_t);
static int fake_close(int);
static int fake_link(const char *, const char *);

#define iot_init_default fake_init_default
#define iot_client_init fake_iot_init
#define iot_client_deinit fake_iot_deinit
#define iot_client_get_session_token_ex fake_token
#define iot_atop_call fake_atop
#define iot_atop_response_free fake_response_free
#define tai_ctx_init fake_ctx_init
#define tai_connect fake_connect
#define tai_send_text fake_send
#define tai_request_disconnect fake_request_disconnect
#define tai_disconnect fake_disconnect
#define tai_ctx_deinit fake_ctx_deinit
#define realloc fake_realloc
#define write fake_write
#define close fake_close
#define link fake_link
#define main image_generation_demo_main
#include "../../../examples/posix/ai/rtc-tcp-client/image_generation_demo.c"
#undef main
#undef iot_init_default
#undef iot_client_init
#undef iot_client_deinit
#undef iot_client_get_session_token_ex
#undef iot_atop_call
#undef iot_atop_response_free
#undef tai_ctx_init
#undef tai_connect
#undef tai_send_text
#undef tai_request_disconnect
#undef tai_disconnect
#undef tai_ctx_deinit
#undef realloc
#undef write
#undef close
#undef link

static int failures, cases;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", __func__, __LINE__, #c); failures++; } } while (0)
#define RUN(fn) do { int before = failures; cases++; fn(); printf("%s %s\n", before == failures ? "PASS" : "FAIL", #fn); } while (0)

static const unsigned char png[] = {
    0x89,0x50,0x4e,0x47,0x0d,0x0a,0x1a,0x0a,0x00,0x00,0x00,0x0d,0x49,0x48,0x44,0x52,
    0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x01,0x08,0x04,0x00,0x00,0x00,0xb5,0x1c,0x0c,
    0x02,0x00,0x00,0x00,0x0b,0x49,0x44,0x41,0x54,0x78,0xda,0x63,0x64,0xf8,0x0f,0x00,
    0x01,0x05,0x01,0x01,0x27,0x18,0xe3,0x66,0x00,0x00,0x00,0x00,0x49,0x45,0x4e,0x44,
    0xae,0x42,0x60,0x82
};
static const char jpeg_b64[] =
    "/9j/4AAQSkZJRgABAQAAAQABAAD/2wBDAAgGBgcGBQgHBwcJCQgKDBQNDAsLDBkSEw8UHRofHh0aHBwgJC4nICIsIxwcKDcpLDAxNDQ0Hyc5PTgyPC4zNDL/"
    "2wBDAQkJCQwLDBgNDRgyIRwhMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjL/"
    "wAARCAABAAEDASIAAhEBAxEB/8QAFQABAQAAAAAAAAAAAAAAAAAAAAb/xAAUEAEAAAAAAAAAAAAAAAAAAAAA/8QAFQEBAQAAAAAAAAAAAAAAAAAABgf/"
    "xAAUEQEAAAAAAAAAAAAAAAAAAAAA/9oADAMBAAIRAxEAPwCLAGVxf//Z";
static const char good_token[] =
    "{\"connect_conf\":{\"hosts\":[\"synthetic.invalid\"],\"domains\":[\"sni.invalid\"],"
    "\"ecc_tls_port\":443,\"derived_client_id\":\"fake-derived\"},"
    "\"session_conf\":{\"agentToken\":\"synthetic-agent-token\",\"bizConfig\":{\"bizCode\":1,\"bizTag\":2}}}";
static const char good_key[] = "{\"secretValue\":\"AAAAAAAAAAAAAAAAAAAAAA==\"}";
static char directory[PATH_MAX], ca_path[PATH_MAX];
static const char *key_response = good_key, *token_response = good_token;
static int atop_status, token_status, init_fail, ctor_fail, connect_status, send_status;
static int oom, write_fault, close_fault, link_fault, delivery;
static int init_calls, token_calls, atop_calls, freed_responses, ctor_calls, connect_calls, send_calls;
static int no_ca_run; /* main() run with --allow-insecure-fallback and no CA */
static int iot_alive, ctx_alive, requested, joined;
static tai_config_t captured;
static const char *borrowed_ca;
static pthread_t worker;
static int worker_started;

static void reset_fakes(void)
{
    key_response = good_key; token_response = good_token;
    atop_status = token_status = init_fail = ctor_fail = connect_status = send_status = 0;
    oom = write_fault = close_fault = link_fault = delivery = 0;
    init_calls = token_calls = atop_calls = freed_responses = ctor_calls = connect_calls = send_calls = 0;
    iot_alive = ctx_alive = requested = joined = worker_started = 0;
    cancelled = 0;
    memset(&captured, 0, sizeof(captured));
}

static int fake_init_default(void) { init_calls++; return 0; }
static iot_client_t *fake_iot_init(const iot_client_config_t *cfg)
{
    CHECK(cfg->mqtt_disable_auto_connect && !cfg->mqtt_disable_tls);
    CHECK(!strcmp(cfg->devid, getenv("TUYA_DEVICE_ID")));
    CHECK(!strcmp(cfg->secret_key, getenv("TUYA_SECRET_KEY")));
    CHECK(!strcmp(cfg->local_key, getenv("TUYA_LOCAL_KEY")));
    if (no_ca_run) CHECK(!cfg->cacert);
    else CHECK(cfg->env == PROD && cfg->cacert && !strcmp(cfg->cacert, "synthetic CA\n"));
    CHECK(!cfg->schema && !cfg->cert_bundle_attach);
    if (init_fail) return NULL;
    iot_client_t *client = calloc(1, sizeof(*client));
    client->cacert = cfg->cacert;
    borrowed_ca = cfg->cacert;
    iot_alive++;
    return client;
}
static void fake_iot_deinit(iot_client_t *client)
{
    CHECK(iot_alive == 1 && client->cacert == borrowed_ca);
    iot_alive--;
    free(client);
}
static int fake_token(iot_client_t *client, const char *agent, char *out, size_t cap,
                      iot_atop_rejection_t *rejection)
{
    (void)rejection;
    CHECK(agent == NULL);
    CHECK(iot_alive && client->cacert == borrowed_ca);
    token_calls++;
    if (token_status) return token_status;
    CHECK(strlen(token_response) < cap);
    snprintf(out, cap, "%s", token_response);
    return 0;
}
static int fake_atop(iot_client_t *client, const iot_atop_request_t *req, iot_atop_response_t *resp)
{
    (void)client;
    CHECK(iot_alive && !ctx_alive);
    CHECK(!strcmp(req->api, "thing.km.secret.get.device") && !strcmp(req->version, "1.0") && !strcmp(req->data, "{}"));
    atop_calls++;
    memset(resp, 0, sizeof(*resp));
    if (key_response) resp->result = strdup(key_response);
    strcpy(resp->error_code, "synthetic-secret-code");
    strcpy(resp->error_msg, "synthetic-secret-error");
    return atop_status;
}
static void fake_response_free(iot_client_t *client, iot_atop_response_t *resp)
{
    (void)client;
    CHECK(iot_alive && !resp->error_code[0] && !resp->error_msg[0]);
    if (resp->result)
        for (size_t i = 0; i < strlen(key_response); i++) CHECK(resp->result[i] == 0);
    freed_responses++;
    free(resp->result);
    memset(resp, 0, sizeof(*resp));
}
static tai_ctx_t *fake_ctx_init(void *mem, const tai_config_t *cfg)
{
    ctor_calls++;
    CHECK(iot_alive && cfg->image_key && cfg->ca_cert == borrowed_ca && !cfg->disable_tls);
    for (size_t i = 0; i < TAI_IMAGE_KEY_LEN; i++) CHECK(cfg->image_key[i] == 0);
    CHECK(!strcmp(cfg->device_id, "fake-derived") && !strcmp(cfg->agent_token, "synthetic-agent-token"));
    CHECK(!cfg->event_custom_param_json && !cfg->session_attrs_json);
    captured = *cfg;
    if (ctor_fail) return NULL;
    ctx_alive++;
    return mem;
}
static int fake_connect(tai_ctx_t *ctx)
{
    (void)ctx; connect_calls++;
    CHECK(!iot_alive && ctx_alive && captured.ca_cert == borrowed_ca);
    return connect_status;
}
static void *deliver_image(void *ctx)
{
    tai_image_msg_t image = {.data = png, .len = sizeof(png), .format = TAI_IMG_PNG,
        .width = 1, .height = 1, .event_id = "fixture-event", .data_id = 6};
    if (delivery == 1) {
        tai_event_msg_t end = {.event_type = TAI_EVT_END};
        captured.on_event(ctx, &end, captured.user_data);
    } else if (delivery == 2) {
        image.payload_type = TAI_IMG_PAYLOAD_URL;
        captured.on_image(ctx, &image, captured.user_data);
    } else {
        captured.on_image(ctx, &image, captured.user_data);
        tai_disconnect_msg_t close_msg = {.reason = TAI_DISCONNECT_CONNECTION_CLOSE};
        captured.on_disconnect(ctx, &close_msg, captured.user_data);
    }
    return NULL;
}
static int fake_send(tai_ctx_t *ctx, const char *text, size_t len)
{
    send_calls++;
    CHECK(len == strlen(text) && len > 0);
    if (send_status) return send_status;
    if (delivery == 3) { on_signal(SIGINT); return 0; }
    if (delivery == 4) return 0;
    CHECK(!pthread_create(&worker, NULL, deliver_image, ctx));
    worker_started = 1;
    return 0;
}
static void fake_request_disconnect(tai_ctx_t *ctx)
{
    (void)ctx;
    /* Proves the application lock is released before the SDK stop request. */
    if (!ctx && captured.user_data) {
        image_state_t *s = captured.user_data;
        int rc = pthread_mutex_trylock(&s->mutex);
        CHECK(rc == 0);
        if (!rc) pthread_mutex_unlock(&s->mutex);
    }
    requested++;
}
static void fake_disconnect(tai_ctx_t *ctx)
{
    (void)ctx;
    if (worker_started) { CHECK(!pthread_join(worker, NULL)); worker_started = 0; }
    joined = 1;
}
static void fake_ctx_deinit(tai_ctx_t *ctx)
{
    (void)ctx;
    CHECK(joined && ctx_alive && captured.ca_cert == borrowed_ca);
    CHECK(!strcmp(captured.agent_token, "synthetic-agent-token"));
    ctx_alive--;
    captured.user_data = NULL;
}
static void *fake_realloc(void *p, size_t n) { return oom ? NULL : realloc(p, n); }
static ssize_t fake_write(int fd, const void *p, size_t n)
{
    if (ctx_alive) CHECK(joined);
    if (write_fault == 1) { errno = ENOSPC; return -1; }
    if (write_fault == 2) { write_fault = 3; errno = EINTR; return -1; }
    if (write_fault == 3 && n > 3) n = 3;
    if (write_fault == 4) return 0;
    return write(fd, p, n);
}
static int fake_close(int fd)
{
    int rc = close(fd);
    if (close_fault) { errno = EIO; return -1; }
    return rc;
}
static int fake_link(const char *source, const char *dest)
{
    if (link_fault == 1) { errno = EPERM; return -1; }
    if (link_fault == 2) {
        int fd = open(dest, O_CREAT | O_EXCL | O_WRONLY, 0600);
        CHECK(fd >= 0);
        if (fd >= 0) { CHECK(write(fd, "existing", 8) == 8); close(fd); }
    }
    return link(source, dest);
}

static int output_count(int remove_files)
{
    DIR *dir = opendir(directory);
    CHECK(dir != NULL);
    if (!dir) return 0;
    struct dirent *entry;
    int count = 0;
    while ((entry = readdir(dir))) {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..") || !strcmp(entry->d_name, "ca.pem")) continue;
        count++;
        if (remove_files) {
            char path[PATH_MAX];
            snprintf(path, sizeof(path), "%s/%s", directory, entry->d_name);
            CHECK(!unlink(path));
        }
    }
    closedir(dir);
    return count;
}
static void verify_bytes(const char *path, const void *bytes, size_t len)
{
    unsigned char data[2048];
    FILE *f = fopen(path, "rb");
    CHECK(f != NULL);
    if (!f) return;
    size_t n = fread(data, 1, sizeof(data), f);
    CHECK(n == len && !memcmp(data, bytes, len));
    CHECK(!fclose(f));
}
static image_state_t new_state(void)
{
    image_state_t s = {0};
    return s;
}
static void start_state(image_state_t *s)
{
    *s = new_state();
    CHECK(!pthread_mutex_init(&s->mutex, NULL));
}
static void end_state(image_state_t *s)
{
    discard_image(s);
    CHECK(!pthread_mutex_destroy(&s->mutex));
}
static tai_image_msg_t image_message(void)
{
    tai_image_msg_t m = {.data = png, .len = sizeof(png), .format = TAI_IMG_PNG,
        .width = 1, .height = 1, .data_id = 6, .event_id = "fixture-event"};
    return m;
}
static void test_callback_file_and_lifetime(void)
{
    reset_fakes();
    image_state_t s; start_state(&s);
    captured.user_data = &s;
    unsigned char source[sizeof(png)]; memcpy(source, png, sizeof(png));
    char id[] = "fixture-event";
    tai_image_msg_t m = image_message(); m.data = source; m.event_id = id;
    on_image(NULL, &m, &s);
    memset(source, 0, sizeof(source)); memset(id, 'x', sizeof(id) - 1);
    CHECK(s.outcome == 1 && !strcmp(s.event_id, "fixture-event") && requested == 1);
    m = image_message(); m.payload_type = TAI_IMG_PAYLOAD_URL;
    on_image(NULL, &m, &s);
    tai_event_msg_t e = {.event_type = TAI_EVT_CHAT_BREAK}; on_event(NULL, &e, &s);
    tai_disconnect_msg_t d = {.reason = TAI_DISCONNECT_PROTOCOL}; on_disconnect(NULL, &d, &s);
    cancelled = 1; CHECK(wait_image(&s, 0) == 1); cancelled = 0;
    char path[PATH_MAX]; CHECK(!save_image(&s, directory, path, sizeof(path)));
    CHECK(!strcmp(path + strlen(path) - 4, ".png"));
    verify_bytes(path, png, sizeof(png)); CHECK(output_count(1) == 1);
    captured.user_data = NULL;
    end_state(&s);
}
static void test_stream_empty_markers_and_metadata(void)
{
    reset_fakes(); image_state_t s; start_state(&s);
    tai_image_msg_t m = image_message(); m.stream_flag = TAI_STREAM_START; m.len = 0; m.data = NULL;
    on_image(NULL, &m, &s);
    m.stream_flag = TAI_STREAM_MIDDLE; m.format = m.width = m.height = 0;
    m.timestamp_ms = 999; m.len = 12; m.data = png; on_image(NULL, &m, &s);
    m.data = png + 12; m.len = sizeof(png) - 12; m.timestamp_ms++;
    on_image(NULL, &m, &s);
    m.stream_flag = TAI_STREAM_END; m.len = 0; m.data = NULL; on_image(NULL, &m, &s);
    CHECK(s.outcome == 1 && s.format == TAI_IMG_PNG && s.width == 1 && s.height == 1);
    char path[PATH_MAX]; CHECK(!save_image(&s, directory, path, sizeof(path)));
    verify_bytes(path, png, sizeof(png)); CHECK(output_count(1) == 1); end_state(&s);
}
static void test_jpeg_and_end_payload(void)
{
    reset_fakes(); size_t len; char *jpeg = b64_decode(jpeg_b64, &len); CHECK(jpeg != NULL);
    if (!jpeg) return;
    for (int streamed = 0; streamed < 2; streamed++) {
        image_state_t s; start_state(&s); tai_image_msg_t m = image_message();
        m.data = (const uint8_t *)jpeg; m.len = len; m.format = TAI_IMG_JPEG;
        if (streamed) { m.stream_flag = TAI_STREAM_START; m.len = 3; }
        on_image(NULL, &m, &s);
        if (streamed) {
            m.stream_flag = TAI_STREAM_END; m.data += 3; m.len = len - 3;
            on_image(NULL, &m, &s);
        }
        char path[PATH_MAX]; CHECK(!save_image(&s, directory, path, sizeof(path)));
        CHECK(!strcmp(path + strlen(path) - 4, ".jpg")); verify_bytes(path, jpeg, len);
        CHECK(output_count(1) == 1); end_state(&s);
    }
    free(jpeg);
}
static void test_stream_rejection_matrix(void)
{
    reset_fakes();
    for (int mode = 0; mode < 12; mode++) {
        image_state_t s; start_state(&s); tai_image_msg_t m = image_message();
        if (mode < 7) { m.stream_flag = TAI_STREAM_START; on_image(NULL, &m, &s); }
        m.stream_flag = TAI_STREAM_END;
        switch (mode) {
        case 0: m.event_id = "other"; break;
        case 1: m.data_id++; break;
        case 2: m.format = TAI_IMG_JPEG; break;
        case 3: m.width++; break;
        case 4: m.height++; break;
        case 5: m.payload_type = TAI_IMG_PAYLOAD_URL; break;
        case 6: m.len = IMAGE_LIMIT; break;
        case 7: m.stream_flag = TAI_STREAM_MIDDLE; break;
        case 8: break;
        case 9: m.stream_flag = TAI_STREAM_ONE_SHOT; m.len = 0; break;
        case 10: m.stream_flag = TAI_STREAM_ONE_SHOT; m.payload_type = TAI_IMG_PAYLOAD_BASE64; break;
        case 11: m.stream_flag = 99; break;
        }
        on_image(NULL, &m, &s);
        CHECK(s.outcome == -1 && !s.bytes && !s.len && s.error);
        char path[PATH_MAX]; CHECK(save_image(&s, directory, path, sizeof(path)) != 0);
        CHECK(output_count(0) == 0); end_state(&s);
    }
}
static void test_replacement_and_event_transition(void)
{
    reset_fakes(); image_state_t s; start_state(&s);
    tai_image_msg_t m = image_message(); m.stream_flag = TAI_STREAM_START;
    on_image(NULL, &m, &s);
    m.event_id = "replacement"; on_image(NULL, &m, &s);
    CHECK(s.len == sizeof(png) && !strcmp(s.event_id, "replacement"));
    tai_event_msg_t e = {.event_type = TAI_EVT_START, .event_id = "new-event"};
    on_event(NULL, &e, &s); CHECK(!s.active && !s.len && !s.outcome);
    m = image_message(); on_image(NULL, &m, &s); CHECK(s.outcome == 1 && s.len == sizeof(png));
    end_state(&s);
}
static void test_terminal_events_disconnect_and_cancel(void)
{
    reset_fakes();
    for (int mode = 0; mode < 8; mode++) {
        image_state_t s; start_state(&s); tai_image_msg_t m = image_message();
        m.stream_flag = TAI_STREAM_START; on_image(NULL, &m, &s);
        if (mode < 3) {
            unsigned events[] = {TAI_EVT_END, TAI_EVT_CHAT_BREAK, TAI_EVT_SERVER_TIMEOVER};
            tai_event_msg_t e = {.event_type = events[mode]}; on_event(NULL, &e, &s);
        } else if (mode < 6) {
            tai_disconnect_msg_t d = {.reason = mode == 3 ? TAI_DISCONNECT_PROTOCOL :
                mode == 4 ? TAI_DISCONNECT_TRANSPORT : TAI_DISCONNECT_SESSION_CLOSE,
                .detail = TAI_PROTO_ERR_IMAGE_AUTH}; on_disconnect(NULL, &d, &s);
        } else {
            if (mode == 6) on_signal(SIGTERM);
            CHECK(wait_image(&s, 0) == -1); cancelled = 0;
        }
        CHECK(s.outcome == -1 && !s.len && !s.bytes); end_state(&s);
    }
}
static void test_oom_and_cumulative_limit(void)
{
    reset_fakes(); image_state_t s; start_state(&s); tai_image_msg_t m = image_message();
    oom = 1; on_image(NULL, &m, &s); CHECK(s.outcome == -1 && strstr(s.error, "allocation"));
    end_state(&s); oom = 0; start_state(&s);
    unsigned char *large = calloc(1, IMAGE_LIMIT); CHECK(large != NULL);
    if (large) {
        m.data = large; m.len = IMAGE_LIMIT; m.stream_flag = TAI_STREAM_START;
        on_image(NULL, &m, &s); CHECK(s.len == IMAGE_LIMIT && s.capacity == IMAGE_LIMIT);
        m.len = 1; m.stream_flag = TAI_STREAM_END; on_image(NULL, &m, &s);
        CHECK(s.outcome == -1 && !s.bytes && strstr(s.error, "16 MiB")); free(large);
    }
    end_state(&s);
}
static void test_signature_and_persistence_faults(void)
{
    reset_fakes();
    for (int mode = 0; mode < 8; mode++) {
        image_state_t s; start_state(&s); tai_image_msg_t m = image_message();
        on_image(NULL, &m, &s);
        if (mode == 0) s.format = TAI_IMG_JPEG;
        if (mode == 1) s.bytes[0] = 0;
        if (mode == 2) write_fault = 1;
        if (mode == 3) close_fault = 1;
        if (mode == 4) link_fault = 1;
        if (mode == 5) link_fault = 2;
        if (mode == 6) write_fault = 4;
        char path[PATH_MAX];
        CHECK(save_image(&s, directory, path, mode == 7 ? 2 : sizeof(path)) != 0);
        CHECK(output_count(0) == (mode == 5 ? 1 : 0));
        if (mode == 5) {
            DIR *d = opendir(directory); struct dirent *e;
            while ((e = readdir(d))) if (!strncmp(e->d_name, "image-", 6)) {
                snprintf(path, sizeof(path), "%s/%s", directory, e->d_name);
                verify_bytes(path, "existing", 8);
            }
            closedir(d);
        }
        output_count(1); end_state(&s); reset_fakes();
    }
    image_state_t s; start_state(&s); tai_image_msg_t m = image_message(); on_image(NULL, &m, &s);
    write_fault = 2; char path[PATH_MAX]; CHECK(!save_image(&s, directory, path, sizeof(path)));
    verify_bytes(path, png, sizeof(png)); CHECK(output_count(1) == 1); end_state(&s);
}
static void test_cli_credentials_and_preflight(void)
{
    reset_fakes(); options_t o;
    char *args[] = {"demo", "--prompt", "draw", "--ca-file", ca_path};
    CHECK(!parse_options(5, args, &o) && o.timeout == 120 && o.region == AY);
    CHECK(parse_options(4, args, &o) < 0);
    char *help[] = {"demo", "--help"}; CHECK(image_generation_demo_main(2, help) == 0);
    const char *bad[] = {"0", "-1", "1x", "+1", "999999999999999999999999999999"};
    char *more[] = {"demo", "--prompt", "x", "--ca-file", ca_path, "--timeout", NULL};
    for (size_t i = 0; i < sizeof(bad)/sizeof(bad[0]); i++) {
        more[6] = (char *)bad[i]; CHECK(parse_options(7, more, &o) < 0);
    }
    more[5] = "--region"; more[6] = "SG"; CHECK(!parse_options(7, more, &o) && o.region == SG);
    more[6] = "invalid"; CHECK(parse_options(7, more, &o) < 0);
    more[5] = "--agent-code"; more[6] = "other-agent";
    CHECK(parse_options(7, more, &o) < 0);
    CHECK(image_generation_demo_main(7, more) == 2 && init_calls == 0);
    more[5] = "--unknown"; CHECK(parse_options(7, more, &o) < 0);
    iot_client_config_t cfg = {0};
    unsetenv("TUYA_LOCAL_KEY"); CHECK(credentials(&cfg) != 0);
    setenv("TUYA_LOCAL_KEY", "", 1); CHECK(credentials(&cfg) != 0);
    setenv("TUYA_LOCAL_KEY", "0123456789012345678901234567890123", 1); CHECK(credentials(&cfg) != 0);
    setenv("TUYA_LOCAL_KEY", "synthetic-local", 1); CHECK(!credentials(&cfg));
    CHECK(!prepare_directory(directory)); CHECK(prepare_directory(ca_path) != 0);
    char missing[PATH_MAX]; snprintf(missing, sizeof(missing), "%s/missing/child", directory);
    CHECK(prepare_directory(missing) != 0); CHECK(read_ca(missing) == NULL);
    char *ca = read_ca(ca_path); CHECK(ca && !strcmp(ca, "synthetic CA\n")); free(ca);
    CHECK(init_calls == 0);
}
static void test_ca_limits(void)
{
    char path[PATH_MAX]; snprintf(path, sizeof(path), "%s/invalid-ca", directory);
    FILE *f = fopen(path, "wb"); CHECK(f != NULL); fclose(f);
    CHECK(read_ca(path) == NULL);
    f = fopen(path, "wb"); CHECK(f != NULL); fwrite("x\0y", 1, 3, f); fclose(f);
    CHECK(read_ca(path) == NULL);
    f = fopen(path, "wb"); CHECK(f != NULL);
    for (unsigned i = 0; i <= CA_LIMIT; i++) fputc('x', f);
    fclose(f); CHECK(read_ca(path) == NULL); CHECK(!unlink(path));
}
static void test_token_validation(void)
{
    tai_conn_params_t p; CHECK(!parse_connection(good_token, &p) && p.port == 443);
    unsigned char encoded[2048]; size_t n;
    CHECK(!mbedtls_base64_encode(encoded, sizeof(encoded), &n, (const unsigned char *)good_token, strlen(good_token)));
    CHECK(!parse_connection((char *)encoded, &p));
    const char *bad[] = {"{}", "null", "[]", "broken", "{\"connect_conf\":{\"hosts\":[\"host\"]}}"};
    for (size_t i = 0; i < sizeof(bad)/sizeof(bad[0]); i++) CHECK(parse_connection(bad[i], &p) != 0);
    const char *ports[] = {"0", "-1", "65536", "443.5", "\"443\"", "null"};
    for (size_t i = 0; i < sizeof(ports)/sizeof(ports[0]); i++) {
        char text[1024]; snprintf(text, sizeof(text),
            "{\"connect_conf\":{\"hosts\":[\"x\"],\"derived_client_id\":\"id\",\"tcpport\":%s},"
            "\"session_conf\":{\"agentToken\":\"token\"}}", ports[i]);
        CHECK(parse_connection(text, &p) != 0);
    }
}
static void test_token_string_validation(void)
{
    const struct { const char *from, *to; int valid; } tests[] = {
        {"connect_conf", "connect_conf\\u0000tail", 0},
        {"session_conf", "session_conf\\u0000tail", 0},
        {"hosts", "hosts\\u0000tail", 0},
        {"domains", "domains\\u0000tail", 0},
        {"ecc_tls_port", "ecc_tls_port\\u0000tail", 0},
        {"derived_client_id", "derived_client_id\\u0000tail", 0},
        {"agentToken", "agentToken\\u0000tail", 0},
        {"synthetic.invalid", "synthetic.invalid\\u0000tail", 0},
        {"sni.invalid", "sni.invalid\\u0000tail", 0},
        {"fake-derived", "fake-derived\\u0000tail", 0},
        {"synthetic-agent-token", "synthetic-agent-token\\u0000tail", 0},
        {"synthetic-agent-token", "\\u0000synthetic-agent-token", 0},
        {"bizCode", "unused\\u0000tail", 0},
        {"2}}}", "2},\"extra\":[{\"nested\":\"prefix\\u0000tail\"}]}}", 0},
        {"synthetic-agent-token", "prefix\\ud800", 0},
        {"synthetic-agent-token", "prefix\\udc00", 0},
        {"bizCode", "unused\\ud800", 0},
        {"synthetic-agent-token", "prefix\\\\u0000tail", 1},
        {"synthetic-agent-token", "prefix\\\\ud800tail", 1},
        {"synthetic-agent-token", "prefix\\\"quoted\\\"\\\\u0000tail", 1},
        {"synthetic-agent-token", "prefix\\ud83d\\ude00", 1},
        {"bizCode", "unused\\\\u0000tail", 1},
    };
    for (size_t i = 0; i < sizeof(tests)/sizeof(tests[0]); i++) {
        const char *at = strstr(good_token, tests[i].from);
        CHECK(at != NULL);
        if (!at) continue;
        char text[1024];
        int len = snprintf(text, sizeof(text), "%.*s%s%s", (int)(at - good_token), good_token,
                           tests[i].to, at + strlen(tests[i].from));
        CHECK(len > 0 && (size_t)len < sizeof(text));
        unsigned char encoded[2048]; size_t n;
        CHECK(!mbedtls_base64_encode(encoded, sizeof(encoded), &n,
                                    (const unsigned char *)text, strlen(text)));
        for (int base64 = 0; base64 < 2; base64++) {
            tai_conn_params_t p;
            memset(&p, 0xab, sizeof(p));
            CHECK((parse_connection(base64 ? (char *)encoded : text, &p) == 0) == tests[i].valid);
            if (!tests[i].valid) {
                const unsigned char *bytes = (const unsigned char *)&p;
                for (size_t j = 0; j < sizeof(p); j++) CHECK(bytes[j] == 0);
            } else if (!strcmp(tests[i].to, "prefix\\\\u0000tail")) {
                CHECK(!strcmp(p.agent_token, "prefix\\u0000tail"));
            }
        }
    }
    unsigned char encoded[2048]; size_t n;
    tai_conn_params_t p;
    /* Even a trailing decoded NUL is forbidden in the base64 payload. */
    CHECK(!mbedtls_base64_encode(encoded, sizeof(encoded), &n,
                                (const unsigned char *)good_token, sizeof(good_token)));
    CHECK(parse_connection((char *)encoded, &p) != 0);
}
static void test_key_validation_and_cleanup(void)
{
    reset_fakes(); iot_alive = 1;
    const char *bad[] = {NULL, "null", "[]", "{}", "{\"secretValue\":1}",
        "{\"secretValue\":\"AAAAAAAAAAAAAAAAAAAAAB==\"}",
        "{\"secretValue\":\"AAAAAAAAAAAAAAAAAAAAAA=\"}",
        "{\"secretValue\":\"AAAAAAAAAAAAAAAAAAAAAA== \"}",
        "{\"secretValue\":\"AAAAAAAAAAAAAAAAAAAA_A==\"}",
        "{\"secretValue\":\"AAAAAAAAAAAAAAAAAAAAAA==\\u0000junk\"}",
        "{\"secretValue\\u0000tail\":\"AAAAAAAAAAAAAAAAAAAAAA==\"}",
        "{\"secretValue\":\"AAAAAAAAAAAAAAAAAAAAAA==\",\"extra\":[{\"nested\":\"prefix\\u0000tail\"}]}",
        "{\"secretValue\":\"AAAAAAAAAAAAAAAAAAAAAA==\",\"extra\":[{\"name\\u0000tail\":\"secret\"}]}",
        "{\"secretValue\":\"AAAAAAAAAAAAAAAAAAAAAA==\",\"extra\":\"prefix\\ud800\"}",
        "{\"secretValue\":\"AAAAAAAAAAAAAAAAAAAAAA==\",\"extra\":\"prefix\\udc00\"}",
        "{\"secretValue\":{\"nested\":[\"prefix\\u0000tail\"]}}",
        "[{\"nested\\u0000tail\":\"secret\"}]",
        "{\"extra\":[{\"nested\":\"prefix\\u0000tail\"}],\"secretValue\":",
        "{\"extra\":[{\"name\\u0000tail\":\"secret\"}],\"secretValue\":"};
    uint8_t key[16];
    for (size_t i = 0; i < sizeof(bad)/sizeof(bad[0]); i++) {
        memset(key, 0xab, sizeof(key)); key_response = bad[i];
        CHECK(fetch_image_key(NULL, key) != 0);
        for (size_t j = 0; j < sizeof(key); j++) CHECK(key[j] == 0);
    }
    const char *good[] = {good_key,
        "{\"secretValue\":\"AAAAAAAAAAAAAAAAAAAAAA==\",\"extra\":[{\"name\\\\u0000\":\"prefix\\\\u0000tail\"}]}",
        "{\"secretValue\":\"AAAAAAAAAAAAAAAAAAAAAA==\",\"extra\":\"prefix\\\"quoted\\\"\\\\u0000tail\"}",
        "{\"secretValue\":\"AAAAAAAAAAAAAAAAAAAAAA==\",\"extra\":\"prefix\\ud83d\\ude00\"}"};
    for (size_t i = 0; i < sizeof(good)/sizeof(good[0]); i++) {
        key_response = good[i]; CHECK(!fetch_image_key(NULL, key));
    }
    key_response = good_key;
    atop_status = OPRT_ATOP_BUSINESS_ERROR; CHECK(fetch_image_key(NULL, key) == atop_status);
    atop_status = OPRT_TLS_HANDSHAKE_FAILED; CHECK(fetch_image_key(NULL, key) == atop_status);
    CHECK(freed_responses == atop_calls && !ctor_calls); iot_alive = 0;
}
static int run_main(void)
{
    char *args[] = {"image_generation_demo", "--prompt", "synthetic prompt", "--ca-file", ca_path,
        "--output-dir", directory, "--timeout", "1"};
    int rc = image_generation_demo_main(9, args);
    CHECK(!iot_alive && !ctx_alive && !worker_started);
    return rc;
}
static void test_main_preflight_and_secret_logs(void)
{
    reset_fakes();
    char *args[] = {"demo", "--prompt", "x", "--ca-file", "/no/such/ca.pem",
                    "--output-dir", directory};
    CHECK(image_generation_demo_main(7, args) != 0 && !init_calls);
    args[4] = ca_path; args[6] = ca_path;
    CHECK(image_generation_demo_main(7, args) != 0 && !init_calls);
    unsetenv("TUYA_DEVICE_ID"); args[6] = directory;
    CHECK(image_generation_demo_main(7, args) != 0 && !init_calls);
    setenv("TUYA_DEVICE_ID", "synthetic-device", 1);
    FILE *logs = tmpfile(); CHECK(logs != NULL);
    if (!logs) return;
    int original = dup(STDERR_FILENO); CHECK(original >= 0);
    fflush(stderr); CHECK(dup2(fileno(logs), STDERR_FILENO) >= 0);
    atop_status = OPRT_ATOP_BUSINESS_ERROR;
    CHECK(run_main() != 0);
    fflush(stderr); CHECK(dup2(original, STDERR_FILENO) >= 0); close(original);
    rewind(logs); char text[2048] = {0}; fread(text, 1, sizeof(text) - 1, logs); fclose(logs);
    CHECK(strstr(text, "Image-key acquisition failed"));
    CHECK(!strstr(text, "synthetic-secret") && !strstr(text, "AAAAAAAAAAAAAAAAAAAAAA=="));
    CHECK(!strstr(text, "synthetic-agent-token"));
}

static void test_insecure_fallback(void)
{
    reset_fakes();
    options_t o;
    char *flag[] = {"demo", "--prompt", "draw", "--allow-insecure-fallback"};
    CHECK(!parse_options(4, flag, &o) && !o.ca_file && o.allow_insecure);
    char *bare[] = {"demo", "--prompt", "draw"};
    CHECK(parse_options(3, bare, &o) < 0);
    char *stray[] = {"demo", "--prompt", "draw", "--allow-insecure-fallback", "junk"};
    CHECK(parse_options(5, stray, &o) < 0); /* the flag takes no value */

    /* Without a CA the opt-in reaches tai_config_t and the run still succeeds. */
    reset_fakes();
    no_ca_run = 1;
    char *run[] = {"image_generation_demo", "--prompt", "synthetic prompt",
                   "--allow-insecure-fallback", "--output-dir", directory, "--timeout", "1"};
    CHECK(image_generation_demo_main(8, run) == 0);
    no_ca_run = 0;
    CHECK(!iot_alive && !ctx_alive && !worker_started);
    /* Without a CA the run still succeeds: the module verifies only when a
     * trust source is configured, so the demo's opt-in is a demo-side gate. */
    CHECK(!captured.ca_cert);
    CHECK(output_count(1) == 1);

    /* A supplied but unreadable CA never silently falls back to unverified TLS. */
    reset_fakes();
    char *bad[] = {"demo", "--prompt", "x", "--ca-file", "/no/such/ca.pem",
                   "--allow-insecure-fallback", "--output-dir", directory};
    CHECK(image_generation_demo_main(8, bad) != 0 && !init_calls);
}
static void test_main_success_threaded_and_failures(void)
{
    for (int mode = 0; mode < 15; mode++) {
        reset_fakes();
        switch (mode) {
        case 1: token_status = OPRT_TLS_HANDSHAKE_FAILED; break;
        case 2: token_response = "{}"; break;
        case 3: key_response = "null"; break;
        case 4: atop_status = OPRT_ATOP_BUSINESS_ERROR; break;
        case 5: ctor_fail = 1; break;
        case 6: connect_status = TAI_ERR_TLS; break;
        case 7: send_status = TAI_ERR_NET; break;
        case 8: delivery = 1; break;
        case 9: delivery = 2; break;
        case 10: delivery = 3; break;
        case 11: delivery = 4; break;
        case 12: init_fail = 1; break;
        case 13: write_fault = 1; break;
        case 14: oom = 1; break;
        }
        CHECK((run_main() == 0) == (mode == 0));
        CHECK(output_count(1) == (mode == 0 ? 1 : 0));
        CHECK(token_calls <= 1 && atop_calls <= 1 && connect_calls <= 1 && send_calls <= 1);
        CHECK(freed_responses == atop_calls);
        if (mode == 1 || mode == 2 || mode == 3 || mode == 4) CHECK(!ctor_calls);
    }
}

int main(void)
{
    char temp[] = "/tmp/image demo test-XXXXXX";
    char *dir = mkdtemp(temp);
    if (!dir) { perror("mkdtemp"); return 1; }
    snprintf(directory, sizeof(directory), "%s", dir);
    snprintf(ca_path, sizeof(ca_path), "%s/ca.pem", directory);
    FILE *f = fopen(ca_path, "wb");
    if (!f) return 1;
    fputs("synthetic CA\n", f); fclose(f);
    const char *names[] = {"TUYA_DEVICE_ID", "TUYA_SECRET_KEY", "TUYA_LOCAL_KEY"};
    char *old[3] = {0};
    for (size_t i = 0; i < 3; i++) { const char *v = getenv(names[i]); if (v) old[i] = strdup(v); }
    setenv(names[0], "synthetic-device", 1); setenv(names[1], "synthetic-secret", 1);
    setenv(names[2], "synthetic-local", 1);
    RUN(test_main_success_threaded_and_failures);
    RUN(test_main_preflight_and_secret_logs);
    RUN(test_insecure_fallback);
    RUN(test_callback_file_and_lifetime);
    RUN(test_stream_empty_markers_and_metadata);
    RUN(test_jpeg_and_end_payload);
    RUN(test_stream_rejection_matrix);
    RUN(test_replacement_and_event_transition);
    RUN(test_terminal_events_disconnect_and_cancel);
    RUN(test_oom_and_cumulative_limit);
    RUN(test_signature_and_persistence_faults);
    RUN(test_cli_credentials_and_preflight);
    RUN(test_ca_limits);
    RUN(test_token_validation);
    RUN(test_token_string_validation);
    RUN(test_key_validation_and_cleanup);
    CHECK(output_count(1) == 0); CHECK(!unlink(ca_path)); CHECK(!rmdir(directory));
    for (size_t i = 0; i < 3; i++) {
        if (old[i]) { setenv(names[i], old[i], 1); free(old[i]); } else unsetenv(names[i]);
    }
    printf("=== image_generation_demo_test: %d cases, %d failures ===\n", cases, failures);
    return failures ? 1 : 0;
}
