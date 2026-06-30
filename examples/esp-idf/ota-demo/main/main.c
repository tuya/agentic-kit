/*
 * ESP-IDF OTA demo using agentic-kit iot_ota API.
 *
 * Flow:
 *   1. Connect WiFi
 *   2. Initialize iot_client with persisted device credentials
 *   3. Check cloud for a firmware upgrade via iot_ota_check_upgrade()
 *   4. If an upgrade is available, report UPGRADING status
 *   5. Download firmware using esp_http_client and flash via esp_ota_*
 *   6. Report SUCCESS (or FAILURE), then reboot
 *
 * The SDK provides the cloud-protocol primitives (version check, status
 * reporting). The application owns the download + flash logic.
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
#include "esp_system.h"
#include "esp_app_format.h"
#include "esp_app_desc.h"
#include "esp_ota_ops.h"
#include "esp_http_client.h"
#include "nvs_flash.h"

#include "iot_client.h"
#include "iot_ota.h"
#include "pal.h"

#include "app_config.h"

#define TAG "ota_demo"

#define WIFI_CONNECTED_BIT  BIT0
#define OTA_BUF_SIZE        4096

static EventGroupHandle_t s_wifi_event_group;

extern const pal_t *tai_pal_freertos(void);

/* ----------------------------------------------------------------------- */
/* WiFi                                                                    */
/* ----------------------------------------------------------------------- */

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    (void)arg; (void)event_data;
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGW(TAG, "WiFi disconnected, retrying...");
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

    ESP_LOGI(TAG, "Connecting to WiFi \"%s\"...", WIFI_SSID);
    xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT,
                        pdFALSE, pdFALSE, portMAX_DELAY);
    ESP_LOGI(TAG, "WiFi connected");
}

/* ----------------------------------------------------------------------- */
/* Firmware download + flash (ESP-IDF esp_ota_* + esp_http_client)         */
/* ----------------------------------------------------------------------- */

static esp_err_t download_and_flash(const char *url)
{
    ESP_LOGI(TAG, "Starting firmware download: %s", url);

    const esp_partition_t *update_part = esp_ota_get_next_update_partition(NULL);
    if (update_part == NULL) {
        ESP_LOGE(TAG, "No OTA partition available");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Update partition: %s @ 0x%08lx", update_part->label,
             (unsigned long)update_part->address);

    esp_http_client_config_t http_cfg = {
        .url = url,
        .timeout_ms = 30000,
        .buffer_size = OTA_BUF_SIZE,
        .buffer_size_tx = OTA_BUF_SIZE,
        .keep_alive_enable = true,
    };

    esp_http_client_handle_t client = esp_http_client_init(&http_cfg);
    if (client == NULL) {
        ESP_LOGE(TAG, "Failed to init HTTP client");
        return ESP_FAIL;
    }

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open HTTP connection: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return err;
    }

    int content_length = esp_http_client_fetch_headers(client);
    int status_code = esp_http_client_get_status_code(client);
    ESP_LOGI(TAG, "HTTP %d, content-length: %d", status_code, content_length);

    if (status_code != 200) {
        ESP_LOGE(TAG, "HTTP error: status %d", status_code);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_FAIL;
    }

    /* Begin OTA write */
    esp_ota_handle_t update_handle = 0;
    err = esp_ota_begin(update_part, OTA_WITH_SEQUENTIAL_WRITES, &update_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin failed: %s", esp_err_to_name(err));
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return err;
    }

    char *buf = malloc(OTA_BUF_SIZE);
    if (buf == NULL) {
        ESP_LOGE(TAG, "Failed to allocate %d-byte buffer", OTA_BUF_SIZE);
        esp_ota_abort(update_handle);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_ERR_NO_MEM;
    }

    int total_read = 0;
    int last_pct = -1;

    while (true) {
        int read_len = esp_http_client_read(client, buf, OTA_BUF_SIZE);
        if (read_len < 0) {
            ESP_LOGE(TAG, "HTTP read error: %s", esp_err_to_name((esp_err_t)read_len));
            free(buf);
            esp_ota_abort(update_handle);
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
            return ESP_FAIL;
        }
        if (read_len == 0) {
            break;  /* EOF */
        }

        err = esp_ota_write(update_handle, buf, read_len);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_write failed: %s", esp_err_to_name(err));
            free(buf);
            esp_ota_abort(update_handle);
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
            return err;
        }

        total_read += read_len;
        if (content_length > 0) {
            int pct = total_read * 100 / content_length;
            if (pct != last_pct && pct % 10 == 0) {
                ESP_LOGI(TAG, "Progress: %d%% (%d/%d bytes)",
                         pct, total_read, content_length);
                last_pct = pct;
            }
        }
    }

    free(buf);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    if (content_length > 0 && total_read != content_length) {
        ESP_LOGE(TAG, "Size mismatch: read %d, expected %d", total_read, content_length);
        esp_ota_abort(update_handle);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Download complete: %d bytes", total_read);

    err = esp_ota_end(update_handle);
    if (err != ESP_OK) {
        if (err == ESP_ERR_OTA_VALIDATE_FAILED) {
            ESP_LOGE(TAG, "Image validation failed — firmware is corrupted");
        } else {
            ESP_LOGE(TAG, "esp_ota_end failed: %s", esp_err_to_name(err));
        }
        return err;
    }

    err = esp_ota_set_boot_partition(update_part);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "Firmware written and boot partition set to %s", update_part->label);
    return ESP_OK;
}

/* ----------------------------------------------------------------------- */
/* Mark current firmware valid (anti-rollback)                             */
/* ----------------------------------------------------------------------- */

static void mark_current_valid(void)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    if (strcmp(running->label, "factory") == 0) {
        ESP_LOGI(TAG, "Running from factory partition, skipping validate");
        return;
    }

    esp_ota_img_states_t state;
    if (esp_ota_get_state_partition(running, &state) != ESP_OK) {
        ESP_LOGW(TAG, "Could not read partition state");
        return;
    }

    if (state == ESP_OTA_IMG_PENDING_VERIFY) {
        ESP_LOGI(TAG, "Marking firmware %s as valid", running->label);
        esp_ota_mark_app_valid_cancel_rollback();
    }
}

/* ----------------------------------------------------------------------- */
/* app_main                                                                */
/* ----------------------------------------------------------------------- */

void app_main(void)
{
    /* NVS */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    /* WiFi */
    wifi_init_sta();

    /* Mark the currently-running firmware valid (first boot after OTA) */
    mark_current_valid();

    /* Print current firmware version */
    const esp_app_desc_t *desc = esp_app_get_description();
    ESP_LOGI(TAG, "Current firmware version: %s", desc->version);

    /* 1. Initialize agentic-kit */
    const pal_t *pal = tai_pal_freertos();
    iot_init(pal);

    /* 2. Initialize iot_client with persisted credentials */
    iot_client_config_t iot_cfg = {
        .devid            = {0},
        .secret_key       = {0},
        .local_key        = {0},
        .region           = DEFAULT_REGION,
        .env              = DEFAULT_ENV,
        .mqtt_disable_tls = false,
        .mqtt_auto_connect = false,
        .message_callback = NULL,
        .schema           = NULL,
        .schema_id        = NULL,
        .dp_state         = NULL,
    };
    memcpy((char *)iot_cfg.devid,      DEFAULT_DEVID,      strlen(DEFAULT_DEVID));
    memcpy((char *)iot_cfg.secret_key, DEFAULT_SECRET_KEY, strlen(DEFAULT_SECRET_KEY));
    memcpy((char *)iot_cfg.local_key,  DEFAULT_LOCAL_KEY,  strlen(DEFAULT_LOCAL_KEY));

    iot_client_t *iot = iot_client_init(&iot_cfg);
    if (iot == NULL) {
        ESP_LOGE(TAG, "iot_client_init failed");
        return;
    }
    ESP_LOGI(TAG, "iot_client initialized (devid=%s)", iot->devid);

    /* 3. Check cloud for firmware upgrade */
    iot_ota_upgrade_info_t info = {0};
    int rc = iot_ota_check_upgrade(iot, 0, desc->version, &info);
    if (rc != OPRT_OK) {
        ESP_LOGE(TAG, "iot_ota_check_upgrade failed: %d", rc);
        iot_ota_upgrade_info_free(iot, &info);
        iot_client_deinit(iot);
        return;
    }

    if (!info.has_upgrade) {
        ESP_LOGI(TAG, "No firmware upgrade available");
        iot_ota_upgrade_info_free(iot, &info);
        iot_client_deinit(iot);
        return;
    }

    ESP_LOGI(TAG, "Upgrade available:");
    ESP_LOGI(TAG, "  version : %s", info.version ? info.version : "?");
    ESP_LOGI(TAG, "  url     : %s", info.url);
    ESP_LOGI(TAG, "  size    : %ld", info.file_size);
    ESP_LOGI(TAG, "  md5     : %s", info.md5 ? info.md5 : "(none)");
    ESP_LOGI(TAG, "  hmac    : %s", info.hmac ? info.hmac : "(none)");

    /* 4. Report upgrade start */
    rc = iot_ota_report_status(iot, 0, OTA_STATUS_UPGRADING);
    if (rc != OPRT_OK) {
        ESP_LOGW(TAG, "Failed to report UPGRADING status: %d (continuing)", rc);
    }

    /* 5. Download and flash firmware */
    esp_err_t err = download_and_flash(info.url);
    iot_ota_upgrade_info_free(iot, &info);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Firmware upgrade FAILED: %s", esp_err_to_name(err));
        iot_ota_report_status(iot, 0, OTA_STATUS_UPGRD_EXEC);
        iot_client_deinit(iot);
        return;
    }

    /* 6. Report success */
    rc = iot_ota_report_status(iot, 0, OTA_STATUS_UPGRAD_FINI);
    if (rc != OPRT_OK) {
        ESP_LOGW(TAG, "Failed to report SUCCESS status: %d", rc);
    }

    iot_client_deinit(iot);

    ESP_LOGI(TAG, "Firmware upgrade successful, rebooting in 3 seconds...");
    vTaskDelay(pdMS_TO_TICKS(3000));
    esp_restart();
}
