/**
 * @file main.c
 * @brief ESP32 BLE WiFi provisioning application
 * @version 1.0
 * @date 2026-03-23
 */
#include <stdio.h>
#include <string.h>
#include <inttypes.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "esp_system.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "esp_crt_bundle.h"
#include "app_config.h"
#include "tuya_ble_nimble.h"
#include "iot_client.h"
#include "pal.h"

static const char *TAG = "app";

/* ---------------------------------------------------------------------------
 * Macros
 * --------------------------------------------------------------------------- */
#define WIFI_CONNECTED_BIT  BIT0
#define PROV_DONE_BIT       BIT1

/* ---------------------------------------------------------------------------
 * File scope variables
 * --------------------------------------------------------------------------- */
static EventGroupHandle_t s_wifi_event_group;
static EventGroupHandle_t s_prov_event_group;
static tuya_ble_wifi_creds_t s_wifi_creds;
static iot_client_t *s_iot_client;

extern const pal_t *tai_pal_freertos(void);

const pal_t *get_default_pal(void)
{
    return tai_pal_freertos();
}

/* ---------------------------------------------------------------------------
 * Function implementations
 * --------------------------------------------------------------------------- */

/**
 * @brief WiFi and IP event handler
 * @param[in] arg user argument (unused)
 * @param[in] event_base event base type
 * @param[in] event_id event identifier
 * @param[in] event_data event-specific data
 * @return none
 */
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "WiFi STA started, connecting...");
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_CONNECTED) {
        ESP_LOGI(TAG, "WiFi connected to AP");
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *disconn = (wifi_event_sta_disconnected_t *)event_data;
        ESP_LOGW(TAG, "WiFi disconnected, reason=%d, retrying...", disconn->reason);
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

/**
 * @brief Initialize WiFi station mode and connect
 * @param[in] ssid WiFi SSID string
 * @param[in] password WiFi password string
 * @return none
 */
static void wifi_init_sta(const char *ssid, const char *password)
{
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                    &wifi_event_handler, NULL, &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                    &wifi_event_handler, NULL, &instance_got_ip));

    wifi_config_t wifi_config = {0};
    size_t ssid_len = strlen(ssid);
    size_t pass_len = strlen(password);
    if (ssid_len > sizeof(wifi_config.sta.ssid)) {
        ssid_len = sizeof(wifi_config.sta.ssid);
    }
    if (pass_len > sizeof(wifi_config.sta.password)) {
        pass_len = sizeof(wifi_config.sta.password);
    }
    memcpy(wifi_config.sta.ssid, ssid, ssid_len);
    memcpy(wifi_config.sta.password, password, pass_len);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
}

/**
 * @brief BLE provisioning completion callback
 * @param[in] creds received WiFi credentials
 * @return none
 * @note Called from NimBLE host task context, must not block
 */
static void on_tuya_ble_prov_complete(const tuya_ble_wifi_creds_t *creds)
{
    memcpy(&s_wifi_creds, creds, sizeof(s_wifi_creds));
    xEventGroupSetBits(s_prov_event_group, PROV_DONE_BIT);
}

static void iot_log_callback(log_level_t level, const char *fmt, va_list args)
{
    esp_log_level_t esp_level = ESP_LOG_INFO;

    switch (level) {
    case LOG_ERROR:
        esp_level = ESP_LOG_ERROR;
        break;
    case LOG_WARN:
        esp_level = ESP_LOG_WARN;
        break;
    case LOG_INFO:
        esp_level = ESP_LOG_INFO;
        break;
    case LOG_DEBUG:
        esp_level = ESP_LOG_DEBUG;
        break;
    default:
        break;
    }

    esp_log_writev(esp_level, TAG, fmt, args);
}

static esp_err_t activate_device_with_ble_token(void)
{
    iot_on_boarding_config_t ob_config = {
        .uuid = DEVICE_UUID,
        .authkey = AUTH_KEY,
        .product_key = PRODUCT_KEY,
        .timeout_ms = 120000,
        .env = PROD,
        .mqtt_disable_tls = false,
        .cert_bundle_attach = (tls_cert_bundle_attach_fn)esp_crt_bundle_attach,
    };

    ESP_LOGI(TAG, "Starting activation with BLE token: %s", s_wifi_creds.token);
    s_iot_client = iot_client_init_on_boarding_with_token(&ob_config, s_wifi_creds.token);
    if (s_iot_client == NULL) {
        ESP_LOGE(TAG, "Activation request failed");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Activation succeeded, devid=%s", s_iot_client->devid);
    ESP_LOGI(TAG, "Activation local_key=%s", s_iot_client->local_key);
    ESP_LOGI(TAG, "Activation secret_key=%s", s_iot_client->secret_key);
    return ESP_OK;
}

/**
 * @brief Application entry point
 * @return none
 */
void app_main(void)
{
    /* Initialize NVS — required by BLE and WiFi */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_ERROR_CHECK(iot_init_default() == OPRT_OK ? ESP_OK : ESP_FAIL);

    printf("\n");
    printf("╔════════════════════════════════════════════════════════════╗\n");
    printf("║          ESP32 Agentic-kit BLE pair Demo Client            ║\n");
    printf("║                                                            ║\n");
    printf("║  This program tests AI Agent functions:                   ║\n");
    printf("║  - on boarding with ble                                    ║\n");
    printf("╚════════════════════════════════════════════════════════════╝\n");
    printf("\n");

    esp_chip_info_t chip_info;
    uint32_t flash_size;
    esp_chip_info(&chip_info);
    printf("This is %s chip with %d CPU core(s), %s%s%s%s, ",
           CONFIG_IDF_TARGET, chip_info.cores,
           (chip_info.features & CHIP_FEATURE_WIFI_BGN) ? "WiFi/" : "",
           (chip_info.features & CHIP_FEATURE_BT) ? "BT" : "",
           (chip_info.features & CHIP_FEATURE_BLE) ? "BLE" : "",
           (chip_info.features & CHIP_FEATURE_IEEE802154) ? ", 802.15.4 (Zigbee/Thread)" : "");

    unsigned major_rev = chip_info.revision / 100;
    unsigned minor_rev = chip_info.revision % 100;
    printf("silicon revision v%d.%d, ", major_rev, minor_rev);
    if (esp_flash_get_size(NULL, &flash_size) != ESP_OK) {
        printf("Get flash size failed\n");
        return;
    }
    printf("%" PRIu32 "MB %s flash\n", flash_size / (uint32_t)(1024 * 1024),
           (chip_info.features & CHIP_FEATURE_EMB_FLASH) ? "embedded" : "external");
    printf("Minimum free heap size: %" PRIu32 " bytes\n", esp_get_minimum_free_heap_size());

    /* BLE provisioning: wait for WiFi credentials from mobile client */
    s_prov_event_group = xEventGroupCreate();

    ESP_LOGI(TAG, "Starting BLE provisioning...");
    tuya_ble_prov_cfg_t prov_cfg = {
        .device_name = TUYA_BLE_DEVICE_NAME,
        .product_key = PRODUCT_KEY,
        .uuid        = DEVICE_UUID,
        .auth_key    = AUTH_KEY,
        .cb          = on_tuya_ble_prov_complete,
    };
    ESP_ERROR_CHECK(tuya_ble_nimble_start(&prov_cfg));

    ESP_LOGI(TAG, "Waiting for WiFi credentials via BLE...");
    xEventGroupWaitBits(s_prov_event_group, PROV_DONE_BIT,
                        pdTRUE, pdFALSE, portMAX_DELAY);

    ESP_LOGI(TAG, "Credentials received, stopping BLE...");
    tuya_ble_nimble_stop();
    vTaskDelay(pdMS_TO_TICKS(500));

    /* Connect WiFi with the credentials received over BLE */
    ESP_LOGI(TAG, "Connecting to WiFi SSID: %s, password: %s", s_wifi_creds.ssid, s_wifi_creds.password);
    wifi_init_sta(s_wifi_creds.ssid, s_wifi_creds.password);

    xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT,
                        pdFALSE, pdFALSE, portMAX_DELAY);

    ESP_LOGI(TAG, "WiFi connected successfully");
    ESP_LOGI(TAG, "Free heap before activation: %" PRIu32, esp_get_free_heap_size());
    ESP_LOGI(TAG, "Minimum free heap before activation: %" PRIu32, esp_get_minimum_free_heap_size());

    if (activate_device_with_ble_token() != ESP_OK) {
        return;
    }
}
