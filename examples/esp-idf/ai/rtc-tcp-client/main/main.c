/*
 * ESP-IDF rtc-tcp-client example -- text chat demo (iot-sdk + TAI).
 *
 * Uses tai_connect() which auto-starts a background receive thread.
 * Sends a text query and waits for the AI response.
 */

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"

#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

#include "tuya_ai.h"
#include "iot_client.h"
#include "pal.h"

#include "app_config.h"

#include "mbedtls/base64.h"

#define WIFI_CONNECTED_BIT BIT0
#define MAX_WAIT_MS        60000

static const char *TAG = "app";
static EventGroupHandle_t s_wifi_event_group;

extern const pal_t *tai_pal_freertos(void);

const pal_t *get_default_pal(void)
{
    return tai_pal_freertos();
}

static volatile int g_done = 0;

/* -- Callbacks ---------------------------------------------------------- */

static void on_text(tai_ctx_t *ctx, const char *text, size_t len,
                    uint8_t stream_flag, void *ud)
{
    (void)ctx; (void)ud;
    printf("%.*s", (int)len, text);
    fflush(stdout);
    if (stream_flag == TAI_STREAM_END || stream_flag == TAI_STREAM_ONE_SHOT)
        printf("\n");
}

static void on_audio(tai_ctx_t *ctx, const uint8_t *data, size_t len,
                     uint32_t sample_rate, uint16_t frame_duration, void *ud)
{
    (void)ctx; (void)data; (void)len; (void)ud;
    (void)sample_rate; (void)frame_duration;
}

static void on_event(tai_ctx_t *ctx, uint16_t event_type,
                     const uint8_t *data, size_t len, void *ud)
{
    (void)data; (void)len; (void)ud;
    if (event_type == TAI_EVT_END) {
        g_done = 1;
    } else if (event_type == TAI_EVT_MCP_CMD) {
        tai_send_mcp_response(ctx,
            "{\"jsonrpc\":\"2.0\",\"id\":1,"
            "\"result\":{\"content\":[{\"type\":\"text\",\"text\":\"\"}]}}");
    }
}

static void on_disconnect(tai_ctx_t *ctx, uint16_t code, void *ud)
{
    (void)ctx; (void)ud;
    ESP_LOGW(TAG, "Disconnected: code=%u", (unsigned)code);
    g_done = 1;
}

/* -- Minimal JSON helpers ----------------------------------------------- */

static const char *json_find_value(const char *json, const char *key)
{
    if (!json || !key) return NULL;
    char search[128];
    snprintf(search, sizeof(search), "\"%s\"", key);
    const char *p = strstr(json, search);
    if (!p) return NULL;
    p += strlen(search);
    while (*p == ' ' || *p == ':' || *p == '\t') p++;
    return p;
}

static int json_get_string(const char *json, const char *key,
                           char *out, size_t cap)
{
    const char *p = json_find_value(json, key);
    if (!p || *p != '"') return -1;
    p++;
    const char *end = strchr(p, '"');
    if (!end) return -1;
    size_t len = (size_t)(end - p);
    if (len >= cap) len = cap - 1;
    memcpy(out, p, len);
    out[len] = '\0';
    return 0;
}

static int json_get_long(const char *json, const char *key, long *out)
{
    const char *p = json_find_value(json, key);
    if (!p) return -1;
    if (*p != '-' && (*p < '0' || *p > '9')) return -1;
    *out = strtol(p, NULL, 10);
    return 0;
}

static int json_array_first_string(const char *json, const char *key,
                                   char *out, size_t cap)
{
    const char *p = json_find_value(json, key);
    if (!p || *p != '[') return -1;
    p++;
    while (*p == ' ') p++;
    if (*p != '"') return -1;
    p++;
    const char *end = strchr(p, '"');
    if (!end) return -1;
    size_t len = (size_t)(end - p);
    if (len >= cap) len = cap - 1;
    memcpy(out, p, len);
    out[len] = '\0';
    return 0;
}

static char *json_get_object(const char *json, const char *key)
{
    const char *p = json_find_value(json, key);
    if (!p || *p != '{') return NULL;
    int depth = 0;
    const char *start = p, *q = p;
    while (*q) {
        if (*q == '{') depth++;
        else if (*q == '}' && --depth == 0) {
            size_t len = (size_t)(q - start + 1);
            char *obj = (char *)malloc(len + 1);
            if (obj) { memcpy(obj, start, len); obj[len] = '\0'; }
            return obj;
        }
        q++;
    }
    return NULL;
}

/* -- Base64 decode ------------------------------------------------------ */

static char *b64_decode(const char *encoded, size_t *out_len)
{
    size_t elen = strlen(encoded);
    size_t dlen = 0;
    if (mbedtls_base64_decode(NULL, 0, &dlen,
                               (const unsigned char *)encoded, elen) != MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL)
        return NULL;
    char *out = (char *)malloc(dlen + 1);
    if (!out) return NULL;
    if (mbedtls_base64_decode((unsigned char *)out, dlen, &dlen,
                               (const unsigned char *)encoded, elen) != 0) {
        free(out);
        return NULL;
    }
    out[dlen] = '\0';
    if (out_len) *out_len = dlen;
    return out;
}

/* -- Parse token -------------------------------------------------------- */

typedef struct {
    char     host[256];
    char     tls_sni[256];
    char     derived_client_id[256];
    char     agent_token[256];
    uint16_t port;
    long     biz_code;
    long     biz_tag;
} tai_conn_params_t;

static int parse_token(const char *raw_token, tai_conn_params_t *p)
{
    memset(p, 0, sizeof(*p));

    char *json = NULL;
    {
        size_t dl = 0;
        char *decoded = b64_decode(raw_token, &dl);
        if (decoded && dl > 0 && decoded[0] == '{') {
            json = decoded;
        } else {
            free(decoded);
            json = strdup(raw_token);
        }
    }
    if (!json) return -1;

    char *conn = json_get_object(json, "connect_conf");
    if (!conn) {
        ESP_LOGE(TAG, "parse_token: 'connect_conf' not found");
        free(json);
        return -1;
    }

    json_array_first_string(conn, "hosts", p->host, sizeof(p->host));

    if (json_array_first_string(conn, "domains", p->tls_sni, sizeof(p->tls_sni)) != 0)
        strncpy(p->tls_sni, p->host, sizeof(p->tls_sni) - 1);

    long port = 0;
    if (json_get_long(conn, "ecc_tls_port", &port) != 0)
        json_get_long(conn, "tcpport", &port);
    p->port = (port > 0) ? (uint16_t)port : 443;

    json_get_string(conn, "derived_client_id",
                    p->derived_client_id, sizeof(p->derived_client_id));
    free(conn);

    char *sess = json_get_object(json, "session_conf");
    if (sess) {
        json_get_string(sess, "agentToken",
                        p->agent_token, sizeof(p->agent_token));
        char *biz = json_get_object(sess, "bizConfig");
        if (biz) {
            json_get_long(biz, "bizCode", &p->biz_code);
            json_get_long(biz, "bizTag",  &p->biz_tag);
            free(biz);
        }
        free(sess);
    }

    free(json);

    if (p->host[0] == '\0') {
        ESP_LOGE(TAG, "parse_token: could not extract host");
        return -1;
    }
    return 0;
}

/* -- WiFi --------------------------------------------------------------- */

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

static void wifi_init_sta(void)
{
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASSWORD,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Connecting to WiFi...");
    xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT,
                        pdFALSE, pdFALSE, portMAX_DELAY);
    ESP_LOGI(TAG, "WiFi connected");
}

/* -- app_main ----------------------------------------------------------- */

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    wifi_init_sta();

    /* 1. Init iot-sdk */
    iot_init_default();

    iot_client_config_t iot_cfg = {
        .devid            = {0},
        .secret_key       = {0},
        .local_key        = {0},
        .region           = AY,
        .env              = PROD,
        .mqtt_disable_tls = false,
        .message_callback = NULL,
    };
    memcpy((char *)iot_cfg.devid,      DEFAULT_DEVID,      strlen(DEFAULT_DEVID));
    memcpy((char *)iot_cfg.secret_key, DEFAULT_SECRET_KEY, strlen(DEFAULT_SECRET_KEY));
    memcpy((char *)iot_cfg.local_key,  DEFAULT_LOCAL_KEY,  strlen(DEFAULT_LOCAL_KEY));

    iot_client_t *iot = iot_client_init(&iot_cfg);
    if (!iot) {
        ESP_LOGE(TAG, "iot_client_init failed");
        return;
    }

    /* 2. Fetch session token */
    char *token = (char *)calloc(1, 4096);
    if (!token) { iot_client_deinit(iot); return; }

    int rc = iot_client_get_session_token(iot, NULL, token, 4096);
    if (rc != 0 || token[0] == '\0') {
        ESP_LOGE(TAG, "iot_client_get_session_token failed: %d", rc);
        free(token); iot_client_deinit(iot);
        return;
    }
    ESP_LOGI(TAG, "Session token acquired");
    ESP_LOGI(TAG, "Free heap before token parse: %u", (unsigned)esp_get_free_heap_size());
    ESP_LOGI(TAG, "Min free heap before token parse: %u", (unsigned)esp_get_minimum_free_heap_size());

    /* 3. Parse token */
    tai_conn_params_t cp;
    if (parse_token(token, &cp) != 0) {
        ESP_LOGE(TAG, "Token parse failed");
        free(token); iot_client_deinit(iot);
        return;
    }
    if (cp.biz_code == 0) cp.biz_code = 65537;
    if (cp.biz_tag  == 0) cp.biz_tag  = 119;

    ESP_LOGI(TAG, "TAI server: %s:%u (SNI: %s)", cp.host, cp.port, cp.tls_sni);
    ESP_LOGI(TAG, "Client ID: %s", cp.derived_client_id);
    ESP_LOGI(TAG, "tai_ctx_size=%u", (unsigned)tai_ctx_size());

    /* 4. Build TAI context */
    const pal_t *pal = tai_pal_freertos();

    tai_config_t tai_cfg = {
        .host              = cp.host,
        .port              = cp.port,
        .tls_sni           = cp.tls_sni,
        .device_id         = cp.derived_client_id,
        .local_key         = DEFAULT_LOCAL_KEY,
        .protocol_version  = TAI_VER_21,
        .client_type       = TAI_CLIENT_DEVICE,
        .sign_level        = TAI_SIGN_HMAC_SHA256,
        .biz_code          = (uint32_t)cp.biz_code,
        .biz_tag           = (uint64_t)cp.biz_tag,
        .agent_token       = cp.agent_token,
        .pal               = pal,
        .on_text           = on_text,
        .on_audio          = on_audio,
        .on_event          = on_event,
        .on_disconnect     = on_disconnect,
    };
    size_t sz = tai_ctx_size();
    void *ctx_buf = heap_caps_malloc(sz, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!ctx_buf) {
        ESP_LOGE(TAG, "Failed to allocate %zu bytes for TAI context in PSRAM", sz);
        free(token); iot_client_deinit(iot);
        return;
    }

    tai_ctx_t *ctx = tai_ctx_init(ctx_buf, &tai_cfg);
    if (!ctx) {
        ESP_LOGE(TAG, "tai_ctx_init failed");
        pal->free(ctx_buf); free(token); iot_client_deinit(iot);
        return;
    }

    /* 5. Connect */
    ESP_LOGI(TAG, "Connecting to TAI server...");
    rc = tai_connect(ctx);
    if (rc != TAI_OK) {
        ESP_LOGE(TAG, "tai_connect failed: %d", rc);
        tai_ctx_deinit(ctx); pal->free(ctx_buf);
        free(token); iot_client_deinit(iot);
        return;
    }
    ESP_LOGI(TAG, "Connected");

    /* 6. Send text query */
    const char *greeting = "Hello, AI! This is an ESP32 chat demo.";
    ESP_LOGI(TAG, "Sending: \"%s\"", greeting);
    printf("Response: ");
    fflush(stdout);

    rc = tai_send_text(ctx, greeting, strlen(greeting));
    if (rc != TAI_OK) {
        ESP_LOGE(TAG, "tai_send_text failed: %d", rc);
        tai_disconnect(ctx); tai_ctx_deinit(ctx); pal->free(ctx_buf);
        free(token); iot_client_deinit(iot);
        return;
    }

    /* 7. Wait for response */
    int waited = 0;
    while (!g_done && waited < MAX_WAIT_MS) {
        vTaskDelay(pdMS_TO_TICKS(100));
        waited += 100;
    }

    if (!g_done)
        ESP_LOGW(TAG, "Timed out after %d s", MAX_WAIT_MS / 1000);
    else
        ESP_LOGI(TAG, "Response complete");

    /* 8. Shutdown */
    tai_disconnect(ctx);
    tai_ctx_deinit(ctx);
    pal->free(ctx_buf);
    free(token);
    iot_client_deinit(iot);

    ESP_LOGI(TAG, "Done");

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
