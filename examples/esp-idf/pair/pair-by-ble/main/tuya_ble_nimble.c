#include <string.h>
#include <stdatomic.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_sm.h"
#include "host/util/util.h"
#include "store/config/ble_store_config.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

static const char *TAG = "tuya_ble";

#define TUYA_BLE_HAL_LOGI(fmt, ...) ESP_LOGI(TAG, fmt, ##__VA_ARGS__)
#define TUYA_BLE_HAL_LOGW(fmt, ...) ESP_LOGW(TAG, fmt, ##__VA_ARGS__)
#define TUYA_BLE_HAL_LOGE(fmt, ...) ESP_LOGE(TAG, fmt, ##__VA_ARGS__)
#define TUYA_BLE_HAL_HEXDUMP(buf, len) ESP_LOG_BUFFER_HEX_LEVEL(TAG, buf, len, ESP_LOG_INFO)
#include "tuya_ble_nimble.h"
#include "tuya_ble_bigdata.h"

#define ADV_INTERVAL_MIN 48
#define ADV_INTERVAL_MAX 96

static const ble_uuid128_t s_write_chr_uuid = BLE_UUID128_INIT(
    0xD0, 0x07, 0x9B, 0x5F, 0x80, 0x00, 0x01, 0x80,
    0x01, 0x10, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00);

static const ble_uuid128_t s_notify_chr_uuid = BLE_UUID128_INIT(
    0xD0, 0x07, 0x9B, 0x5F, 0x80, 0x00, 0x01, 0x80,
    0x01, 0x10, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00);

static const ble_uuid128_t s_read_chr_uuid = BLE_UUID128_INIT(
    0xD0, 0x07, 0x9B, 0x5F, 0x80, 0x00, 0x01, 0x80,
    0x01, 0x10, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00);

static const ble_uuid16_t s_svc_uuid = BLE_UUID16_INIT(0xFD50);

static tuya_ble_prov_state_t s_prov;
static bool s_prov_done;
static uint8_t s_own_addr_type;
static uint16_t s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static struct ble_npl_callout s_transport_timer;
static uint16_t s_notify_attr_handle;
static bool s_notify_enabled;
typedef struct {
    uint32_t token;
    char ccode[3];
} wifi_scan_request_t;

typedef struct {
    uint32_t token;
    uint16_t count;
    tuya_ble_wifi_ap_t aps[TUYA_BLE_WIFI_LIST_MAX];
} wifi_scan_result_t;

static QueueHandle_t s_scan_requests;
static QueueHandle_t s_scan_results;
static SemaphoreHandle_t s_owner_stopped;
static SemaphoreHandle_t s_worker_stopped;
static SemaphoreHandle_t s_host_stopped;
static atomic_bool s_stop_requested;
static bool s_running;
/* BLE-owner state: cancellation must not release the worker's occupied slot. */
static bool s_wifi_scan_pending;
static bool s_wifi_scan_cancelled;
static uint32_t s_wifi_scan_token;

static int prov_gap_event(struct ble_gap_event *event, void *arg);
static int prov_gatt_write_access(uint16_t conn_handle, uint16_t attr_handle,
                                  struct ble_gatt_access_ctxt *ctxt, void *arg);
static int prov_gatt_notify_access(uint16_t conn_handle, uint16_t attr_handle,
                                   struct ble_gatt_access_ctxt *ctxt, void *arg);
static int prov_gatt_read_access(uint16_t conn_handle, uint16_t attr_handle,
                                 struct ble_gatt_access_ctxt *ctxt, void *arg);
static void prov_start_advertise(void);

static const struct ble_gatt_svc_def s_gatt_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &s_svc_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid = &s_write_chr_uuid.u,
                .access_cb = prov_gatt_write_access,
                .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
            },
            {
                .uuid = &s_notify_chr_uuid.u,
                .access_cb = prov_gatt_notify_access,
                .val_handle = &s_notify_attr_handle,
                .flags = BLE_GATT_CHR_F_NOTIFY,
            },
            {
                .uuid = &s_read_chr_uuid.u,
                .access_cb = prov_gatt_read_access,
                .flags = BLE_GATT_CHR_F_READ,
            },
            { 0 },
        },
    },
    { 0 },
};

void tuya_ble_hal_random(uint8_t *buf, size_t len)
{
    esp_fill_random(buf, len);
}

static esp_err_t wifi_scan_run(const wifi_scan_request_t *request,
                               wifi_scan_result_t *result)
{
    wifi_mode_t mode = WIFI_MODE_NULL;
    if (esp_wifi_get_mode(&mode) != ESP_OK) {
        wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();
        esp_err_t err = esp_wifi_init(&config);
        if (err != ESP_OK) return err;
    }
    esp_err_t err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err != ESP_OK) return err;
    err = esp_wifi_start();
    if (err != ESP_OK) return err;

    if (request->ccode[0]) {
        wifi_country_t country = {.cc = "", .schan = 1, .nchan = 13,
                                  .max_tx_power = 20, .policy = WIFI_COUNTRY_POLICY_AUTO};
        country.cc[0] = request->ccode[0];
        country.cc[1] = request->ccode[1];
        (void)esp_wifi_set_country(&country);
    }
    /* A blocking scan has no SCAN_DONE event: its return belongs to this request,
     * unlike an asynchronous event whose scan_id is not returned at submission. */
    wifi_scan_config_t config = {.scan_type = WIFI_SCAN_TYPE_ACTIVE};
    err = esp_wifi_scan_start(&config, true);
    if (err != ESP_OK) {
        (void)esp_wifi_clear_ap_list();
        return err;
    }
    wifi_ap_record_t records[TUYA_BLE_WIFI_LIST_MAX];
    uint16_t count = TUYA_BLE_WIFI_LIST_MAX;
    err = esp_wifi_scan_get_ap_records(&count, records);
    if (err != ESP_OK) {
        (void)esp_wifi_clear_ap_list();
        return err;
    }
    for (uint16_t i = 0; i < count; i++) {
        size_t len = strnlen((const char *)records[i].ssid, TUYA_BLE_WIFI_SSID_MAX);
        memcpy(result->aps[i].ssid, records[i].ssid, len);
        result->aps[i].ssid[len] = '\0';
        result->aps[i].rssi = records[i].rssi;
        result->aps[i].sec = records[i].authmode == WIFI_AUTH_OPEN ? 0 : 1;
    }
    result->count = count;
    return ESP_OK;
}

static void wifi_scan_worker(void *arg)
{
    (void)arg;
    wifi_scan_request_t request;
    while (xQueueReceive(s_scan_requests, &request, portMAX_DELAY) == pdTRUE) {
        if (request.token == 0) break;
        wifi_scan_result_t result = {.token = request.token};
        esp_err_t err = wifi_scan_run(&request, &result);
        if (err != ESP_OK) ESP_LOGW(TAG, "WiFi scan failed: %s", esp_err_to_name(err));
        /* One occupied slot, released only when the BLE owner drains this copy.
         * Never enqueue onto NimBLE here, including during shutdown. */
        BaseType_t sent = xQueueSend(s_scan_results, &result, 0);
        configASSERT(sent == pdTRUE);
    }
    xSemaphoreGive(s_worker_stopped);
    vTaskDelete(NULL);
}

static void wifi_scan_poll(void)
{
    wifi_scan_result_t result;
    if (xQueueReceive(s_scan_results, &result, 0) != pdTRUE) return;
    bool deliver = s_wifi_scan_pending && !s_wifi_scan_cancelled &&
                   result.token == s_wifi_scan_token;
    s_wifi_scan_pending = false;
    if (deliver) {
        (void)tuya_ble_bigdata_wifi_list_complete(&s_prov, result.token,
                                                 result.aps, result.count);
    }
}

static void wifi_scan_cancel(void)
{
    /* Logical cancellation only: do not wait for WiFi under a BLE host lock,
     * and do not admit another request until the old radio operation returns. */
    s_wifi_scan_cancelled = true;
}

static int wifi_scan_request(uint16_t count, const char *ccode,
                             uint32_t token, void *ctx)
{
    (void)count;
    (void)ctx;
    if (s_prov_done || atomic_load(&s_stop_requested) || s_wifi_scan_pending) return -1;

    wifi_scan_request_t request = {.token = token};
    if (ccode[0]) memcpy(request.ccode, ccode, 2);
    if (xQueueSend(s_scan_requests, &request, 0) != pdTRUE) return -1;
    s_wifi_scan_token = token;
    s_wifi_scan_cancelled = false;
    s_wifi_scan_pending = true;
    return 0;
}

static int nimble_send(const uint8_t *buf, uint16_t len, void *ctx)
{
    (void)ctx;

    if (s_conn_handle == BLE_HS_CONN_HANDLE_NONE) return -1;
    if (!s_notify_enabled) return TUYA_BLE_SEND_BUSY;

    struct os_mbuf *om = ble_hs_mbuf_from_flat(buf, len);
    if (om == NULL) return TUYA_BLE_SEND_BUSY;

    int rc = ble_gatts_notify_custom(s_conn_handle, s_notify_attr_handle, om);
    if (rc == BLE_HS_ENOMEM || rc == BLE_HS_EBUSY) return TUYA_BLE_SEND_BUSY;
    return rc == 0 ? 0 : -1;
}

static int prov_gatt_write_access(uint16_t conn_handle, uint16_t attr_handle,
                                  struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn_handle;
    (void)attr_handle;
    (void)arg;

    if (s_prov_done || atomic_load(&s_stop_requested) ||
        ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) {
        return BLE_ATT_ERR_UNLIKELY;
    }

    uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
    ESP_LOGI(TAG, "[GATT] Write, %d bytes", len);

    if (len == 0 || len > TUYA_BLE_RX_BUF_SIZE) {
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }

    uint8_t raw[TUYA_BLE_RX_BUF_SIZE];
    os_mbuf_copydata(ctxt->om, 0, len, raw);
    if (tuya_ble_prov_on_data(&s_prov, raw, len) != 0) {
        ESP_LOGW(TAG, "[GATT] on_data rejected %d bytes", len);
    }
    return 0;
}

static int prov_gatt_notify_access(uint16_t conn_handle, uint16_t attr_handle,
                                   struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn_handle;
    (void)attr_handle;
    (void)ctxt;
    (void)arg;
    return 0;
}

static int prov_gatt_read_access(uint16_t conn_handle, uint16_t attr_handle,
                                 struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn_handle;
    (void)attr_handle;
    (void)arg;

    const uint8_t *adv_data;
    const uint8_t *rsp_data;
    uint8_t adv_len;
    uint8_t rsp_len;
    tuya_ble_prov_get_read_payload(&s_prov, &adv_data, &adv_len, &rsp_data, &rsp_len);

    ESP_LOGI(TAG, "[GATT] Read characteristic, returning adv+rsp (%d+%d bytes)", adv_len, rsp_len);

    int rc = os_mbuf_append(ctxt->om, adv_data, adv_len);
    if (rc == 0) {
        rc = os_mbuf_append(ctxt->om, rsp_data, rsp_len);
    }
    return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}

static void prov_start_advertise(void)
{
    if (s_prov_done || atomic_load(&s_stop_requested)) return;
    const uint8_t *adv_data;
    const uint8_t *rsp_data;
    uint8_t adv_len;
    uint8_t rsp_len;
    tuya_ble_prov_get_adv_data(&s_prov, &adv_data, &adv_len, &rsp_data, &rsp_len);

    ble_gap_adv_set_data(adv_data, adv_len);
    ble_gap_adv_rsp_set_data(rsp_data, rsp_len);

    struct ble_gap_adv_params adv_params = {0};
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    adv_params.itvl_min = ADV_INTERVAL_MIN;
    adv_params.itvl_max = ADV_INTERVAL_MAX;

    int rc = ble_gap_adv_start(s_own_addr_type, NULL, BLE_HS_FOREVER,
                               &adv_params, prov_gap_event, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to start advertising, rc=%d", rc);
    }
    ESP_LOGI(TAG, "Advertising started");
}

static int prov_gap_event(struct ble_gap_event *event, void *arg)
{
    (void)arg;
    struct ble_gap_conn_desc desc;

    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            s_conn_handle = event->connect.conn_handle;
            wifi_scan_cancel();
            tuya_ble_prov_close(&s_prov);
            s_notify_enabled = false;
            tuya_ble_prov_tick(&s_prov, (uint64_t)esp_timer_get_time() / 1000);
            ble_gap_conn_find(s_conn_handle, &desc);
            ESP_LOGI(TAG, "[GAP] CONNECT, handle=%d, peer=%02x:%02x:%02x:%02x:%02x:%02x",
                     s_conn_handle,
                     desc.peer_id_addr.val[5], desc.peer_id_addr.val[4],
                     desc.peer_id_addr.val[3], desc.peer_id_addr.val[2],
                     desc.peer_id_addr.val[1], desc.peer_id_addr.val[0]);
        } else {
            ESP_LOGW(TAG, "[GAP] CONNECT failed, status=%d", event->connect.status);
            if (!s_prov_done) {
                prov_start_advertise();
            }
        }
        break;

    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGW(TAG, "[GAP] DISCONNECT, reason=0x%x", event->disconnect.reason);
        s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
        s_notify_enabled = false;
        wifi_scan_cancel();
        tuya_ble_prov_close(&s_prov);
        if (!s_prov_done) {
            prov_start_advertise();
        }
        break;

    case BLE_GAP_EVENT_MTU:
        ESP_LOGI(TAG, "[GAP] MTU=%d", event->mtu.value);
        tuya_ble_prov_set_gatt_payload(&s_prov, event->mtu.value - 3);
        break;

    case BLE_GAP_EVENT_ENC_CHANGE:
        ESP_LOGI(TAG, "[GAP] ENC_CHANGE, status=%d", event->enc_change.status);
        break;

    case BLE_GAP_EVENT_SUBSCRIBE:
        ESP_LOGI(TAG, "[GAP] SUBSCRIBE, attr=%d (notify_attr=%d), notify=%d, indicate=%d",
                 event->subscribe.attr_handle, s_notify_attr_handle,
                 event->subscribe.cur_notify, event->subscribe.cur_indicate);
        if (event->subscribe.attr_handle == s_notify_attr_handle) {
            s_notify_enabled = event->subscribe.cur_notify;
            if (s_notify_enabled) tuya_ble_prov_tx_ready(&s_prov);
            ESP_LOGI(TAG, "[GAP] Tuya notify %s", s_notify_enabled ? "ENABLED" : "DISABLED");
        }
        break;

    case BLE_GAP_EVENT_REPEAT_PAIRING:
        ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc);
        ble_store_util_delete_peer(&desc.peer_id_addr);
        return BLE_GAP_REPEAT_PAIRING_RETRY;

    case BLE_GAP_EVENT_ADV_COMPLETE:
        if (!s_prov_done) {
            prov_start_advertise();
        }
        break;

    default:
        ESP_LOGI(TAG, "[GAP] event type=%d", event->type);
        break;
    }
    return 0;
}

static void ble_on_sync(void)
{
    ble_addr_t rnd_addr;
    int rc = ble_hs_id_gen_rnd(0, &rnd_addr);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_hs_id_gen_rnd failed, rc=%d", rc);
        return;
    }
    rc = ble_hs_id_set_rnd(rnd_addr.val);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_hs_id_set_rnd failed, rc=%d", rc);
        return;
    }
    s_own_addr_type = BLE_OWN_ADDR_RANDOM;
    ESP_LOGI(TAG, "Using random static addr: %02x:%02x:%02x:%02x:%02x:%02x",
             rnd_addr.val[5], rnd_addr.val[4], rnd_addr.val[3],
             rnd_addr.val[2], rnd_addr.val[1], rnd_addr.val[0]);

    ble_gatts_show_local();
    ESP_LOGI(TAG, "BLE host synced, starting advertising");
    prov_start_advertise();
}

static void ble_on_reset(int reason)
{
    ESP_LOGW(TAG, "BLE host reset, reason=%d", reason);
    s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
    s_notify_enabled = false;
    wifi_scan_cancel();
    tuya_ble_prov_close(&s_prov);
}

/* Runs on the NimBLE event queue, never the application/IoT task. */
static void transport_tick(struct ble_npl_event *event)
{
    (void)event;
    if (atomic_load(&s_stop_requested)) {
        if (!s_prov_done) {
            s_prov_done = true;
            wifi_scan_cancel();
            tuya_ble_prov_close(&s_prov);
            xSemaphoreGive(s_owner_stopped);
        }
        return;
    }
    wifi_scan_poll();
    if (s_conn_handle != BLE_HS_CONN_HANDLE_NONE &&
        tuya_ble_prov_tick(&s_prov, (uint64_t)esp_timer_get_time() / 1000) < 0) {
        ble_gap_terminate(s_conn_handle, BLE_ERR_REM_USER_CONN_TERM);
    }
    if (!s_prov_done) ble_npl_callout_reset(&s_transport_timer, ble_npl_time_ms_to_ticks32(100));
}

static void nimble_host_task(void *param)
{
    (void)param;
    nimble_port_run();
    /* Let the application delete/join this task before deinitializing the port.
     * Self-deletion in IDF leaves its host_task_h uncleared across a restart. */
    xSemaphoreGive(s_host_stopped);
    vTaskSuspend(NULL);
}

static void wifi_scan_resources_free(void)
{
    if (s_scan_requests) vQueueDelete(s_scan_requests);
    if (s_scan_results) vQueueDelete(s_scan_results);
    if (s_owner_stopped) vSemaphoreDelete(s_owner_stopped);
    if (s_worker_stopped) vSemaphoreDelete(s_worker_stopped);
    if (s_host_stopped) vSemaphoreDelete(s_host_stopped);
    s_scan_requests = NULL;
    s_scan_results = NULL;
    s_owner_stopped = NULL;
    s_worker_stopped = NULL;
    s_host_stopped = NULL;
}

int tuya_ble_nimble_start(const tuya_ble_prov_cfg_t *cfg)
{
    if (s_running || cfg == NULL || cfg->device_name == NULL || cfg->product_key == NULL ||
        cfg->uuid == NULL || cfg->auth_key == NULL || cfg->cb == NULL) {
        return -1;
    }

    esp_log_level_set("NimBLE", ESP_LOG_DEBUG);
    esp_log_level_set("tuya_ble", ESP_LOG_DEBUG);

    esp_err_t err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return -1;
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return -1;
    if (esp_netif_get_handle_from_ifkey("WIFI_STA_DEF") == NULL) {
        esp_netif_create_default_wifi_sta();
    }

    s_prov_done = false;
    s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
    s_notify_enabled = false;
    s_wifi_scan_pending = false;
    s_wifi_scan_cancelled = false;
    atomic_store(&s_stop_requested, false);

    tuya_ble_prov_cfg_ext_t prov_cfg = {
        .device_name = cfg->device_name,
        .product_key = cfg->product_key,
        .uuid = cfg->uuid,
        .auth_key = cfg->auth_key,
        .cb = cfg->cb,
        .send_fn = nimble_send,
        .send_ctx = NULL,
        .comm_ability = TUYA_BLE_COMM_ABILITY_2_4_GHZ,
        .wifi_scan_request = wifi_scan_request,
        .wifi_scan_ctx = NULL,
    };

    if (tuya_ble_prov_init(&s_prov, &prov_cfg) != 0) {
        return -1;
    }

    int rc = nimble_port_init();
    if (rc != 0) {
        ESP_LOGE(TAG, "nimble_port_init failed, rc=%d", rc);
        return -1;
    }

    ble_hs_cfg.sync_cb = ble_on_sync;
    ble_hs_cfg.reset_cb = ble_on_reset;
    ble_hs_cfg.sm_io_cap = BLE_SM_IO_CAP_NO_IO;
    ble_hs_cfg.sm_bonding = 0;
    ble_hs_cfg.sm_mitm = 0;
    ble_hs_cfg.sm_sc = 0;
    ble_hs_cfg.sm_our_key_dist = 0;
    ble_hs_cfg.sm_their_key_dist = 0;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

    rc = ble_gatts_count_cfg(s_gatt_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gatts_count_cfg failed, rc=%d", rc);
        nimble_port_deinit();
        return -1;
    }

    rc = ble_gatts_add_svcs(s_gatt_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gatts_add_svcs failed, rc=%d", rc);
        nimble_port_deinit();
        return -1;
    }

    s_scan_requests = xQueueCreate(1, sizeof(wifi_scan_request_t));
    s_scan_results = xQueueCreate(1, sizeof(wifi_scan_result_t));
    s_owner_stopped = xSemaphoreCreateBinary();
    s_worker_stopped = xSemaphoreCreateBinary();
    s_host_stopped = xSemaphoreCreateBinary();
    if (!s_scan_requests || !s_scan_results || !s_owner_stopped ||
        !s_worker_stopped || !s_host_stopped ||
        xTaskCreate(wifi_scan_worker, "prov_scan", 8192, NULL, 5, NULL) != pdPASS) {
        wifi_scan_resources_free();
        nimble_port_deinit();
        return -1;
    }

    ble_npl_callout_init(&s_transport_timer, nimble_port_get_dflt_eventq(), transport_tick, NULL);
    ble_npl_callout_reset(&s_transport_timer, ble_npl_time_ms_to_ticks32(100));
    ble_store_clear();
    nimble_port_freertos_init(nimble_host_task);
    s_running = true;

    ESP_LOGI(TAG, "BLE provisioning started, device=%s, notify_handle=%d",
             cfg->device_name, s_notify_attr_handle);
    return 0;
}

/* Start/stop are serialized by the application task, never called from a BLE
 * callback or the ESP event loop. Only this task waits for producer shutdown. */
int tuya_ble_nimble_stop(void)
{
    if (!s_running) return 0;
    atomic_store(&s_stop_requested, true);
    xSemaphoreTake(s_owner_stopped, portMAX_DELAY);
    int rc = nimble_port_stop();
    if (rc != 0) {
        xSemaphoreGive(s_owner_stopped);
        return rc;
    }
    xSemaphoreTake(s_host_stopped, portMAX_DELAY);
    nimble_port_freertos_deinit();

    /* The owner can no longer submit work. Finish any admitted scan before
     * releasing its queues or handing the WiFi radio to the application. */
    wifi_scan_request_t stop = {0};
    xQueueSend(s_scan_requests, &stop, portMAX_DELAY);
    xSemaphoreTake(s_worker_stopped, portMAX_DELAY);
    wifi_scan_resources_free();

    /* The one-shot timer delivered the owner-stop acknowledgement without
     * rearming, so there is no timer producer or queued tick left to drain. */
    ble_npl_callout_deinit(&s_transport_timer);
    rc = nimble_port_deinit();
    ESP_ERROR_CHECK(rc);
    s_running = false;
    ESP_LOGI(TAG, "BLE provisioning stopped");
    return 0;
}
