#ifndef TUYA_BLE_PROV_H
#define TUYA_BLE_PROV_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define TUYA_BLE_SSID_MAX_LEN       64
#define TUYA_BLE_PASSWORD_MAX_LEN   64
#define TUYA_BLE_TOKEN_MAX_LEN      16
#define TUYA_BLE_NAME_MAX_LEN       5
#define TUYA_BLE_ADV_DATA_LEN       31
#define TUYA_BLE_RSP_DATA_LEN       31
#define TUYA_BLE_ID_LEN             16
#define TUYA_BLE_PAIR_RAND_LEN      6
#define TUYA_BLE_RX_BUF_SIZE        512
#define TUYA_BLE_TX_BUF_SIZE        1024

/* send_fn copies bytes before returning: 0 accepted, 1 busy (retry unchanged),
 * any other result is a permanent failure. Call all APIs on one BLE owner context;
 * callbacks must not reenter the state. No SDK worker or sleeps are used. */
#define TUYA_BLE_SEND_BUSY 1
#define TUYA_BLE_TX_QUEUE_DEPTH 4

/* Encryption-mode byte of a Packet (big-data uplinks name the mode for
 * tuya_ble_prov_send_frame). */
#define TUYA_BLE_ENCRYPTION_MODE_NONE   0x00
#define TUYA_BLE_ENCRYPTION_MODE_KEY_11 0x0B
#define TUYA_BLE_ENCRYPTION_MODE_KEY_12 0x0C

/* Radio capability advertised in the scan response and device-info response.
 * Advertise only bands the port can actually scan and join: claiming 5 GHz on
 * a 2.4-GHz-only radio makes the app offer networks the device cannot use. */
#define TUYA_BLE_COMM_ABILITY_2_4_GHZ 0x0004
#define TUYA_BLE_COMM_ABILITY_5_GHZ   0x0008

/* Optional checked CSPRNG: return the number of bytes filled, or negative on failure. */
typedef int (*tuya_ble_random_t)(uint8_t *buf, size_t len, void *ctx);

typedef int (*tuya_ble_hal_send_t)(const uint8_t *buf, uint16_t len, void *ctx);

/* WiFi-list scan request hook (big-data channel 0x801E sub 0x0003; see
 * tuya_ble_bigdata.h). Called on the BLE owner context when the app queries
 * the surrounding WiFi list. Must not block and must not call back into the
 * state: copy cnt/ccode/token, start the port's scan asynchronously, and
 * deliver the result later from the BLE owner context via
 * tuya_ble_bigdata_wifi_list_complete(). Return 0 only if the scan started;
 * any other value makes the SDK answer the app with an empty list. */
typedef int (*tuya_ble_wifi_scan_request_fn)(uint16_t cnt, const char *ccode,
                                             uint32_t token, void *ctx);

void tuya_ble_hal_random(uint8_t *buf, size_t len);

#ifndef TUYA_BLE_HAL_LOGI
#define TUYA_BLE_HAL_LOGI(fmt, ...)
#endif

#ifndef TUYA_BLE_HAL_LOGW
#define TUYA_BLE_HAL_LOGW(fmt, ...)
#endif

#ifndef TUYA_BLE_HAL_LOGE
#define TUYA_BLE_HAL_LOGE(fmt, ...)
#endif

#ifndef TUYA_BLE_HAL_HEXDUMP
#define TUYA_BLE_HAL_HEXDUMP(buf, len)
#endif

typedef struct {
    char ssid[TUYA_BLE_SSID_MAX_LEN + 1];
    char password[TUYA_BLE_PASSWORD_MAX_LEN + 1];
    char token[TUYA_BLE_TOKEN_MAX_LEN + 1];
} tuya_ble_wifi_creds_t;

typedef void (*tuya_ble_prov_cb_t)(const tuya_ble_wifi_creds_t *creds);

typedef struct {
    const char *device_name;
    const char *product_key;
    const char *uuid;
    const char *auth_key;
    tuya_ble_prov_cb_t cb;
} tuya_ble_prov_cfg_t;

typedef struct {
    const char *device_name;
    const char *product_key;
    const char *uuid;
    const char *auth_key;
    tuya_ble_prov_cb_t cb;
    tuya_ble_hal_send_t send_fn;
    void *send_ctx;
    /* Optional checked entropy source. NULL retains the legacy full-fill HAL contract. */
    tuya_ble_random_t random_fn;
    void *random_ctx;
    /* Zero selects the safe 2.4-GHz default. The capability is descriptive;
     * it does not enable the psk3 device-info tail. */
    uint16_t comm_ability;
    /* Optional asynchronous nearby-WiFi scan provider. Supplying it advertises
     * WiFi-list support to the Tuya App (scan-response bit 5 plus device-info
     * CombosFlag bit 0), which makes the App issue 0x801E/subcommand 0x0003. */
    tuya_ble_wifi_scan_request_fn wifi_scan_request;
    void *wifi_scan_ctx;
} tuya_ble_prov_cfg_ext_t;

typedef struct {
    tuya_ble_prov_cfg_ext_t cfg;
    tuya_ble_wifi_creds_t creds;

    uint8_t ble_id[TUYA_BLE_ID_LEN + 1];
    bool is_id_comp;
    uint8_t adv_data[TUYA_BLE_ADV_DATA_LEN];
    uint8_t adv_len;
    uint8_t rsp_data[TUYA_BLE_RSP_DATA_LEN];
    uint8_t rsp_len;

    uint32_t sn;
    uint32_t last_rx_sn;
    uint8_t pair_rand[TUYA_BLE_PAIR_RAND_LEN];
    uint8_t server_rand[16];
    uint8_t key_11[16];
    bool paired;
    bool handshake_ready;
    bool authenticated;
    bool credentials_pending;
    uint32_t connection_generation;
    uint16_t gatt_payload;
    uint32_t rx_next_subpkg;
    uint64_t now_ms;
    uint64_t rx_started_ms;
    uint64_t tx_started_ms;
    uint8_t tx_queue[TUYA_BLE_TX_QUEUE_DEPTH][TUYA_BLE_TX_BUF_SIZE];
    uint16_t tx_queue_len[TUYA_BLE_TX_QUEUE_DEPTH];
    uint8_t tx_head;
    uint8_t tx_count;
    uint16_t tx_offset;
    uint32_t tx_subpkg;

    uint8_t trsmitr_seq;
    uint16_t peer_pkt_len;
    uint8_t rx_buf[TUYA_BLE_RX_BUF_SIZE];
    uint16_t rx_len;
    uint32_t rx_total_len;
    uint8_t tx_frame[TUYA_BLE_TX_BUF_SIZE];
    uint8_t tx_enc_pkt[TUYA_BLE_TX_BUF_SIZE];
    uint8_t tx_trsmitr_buf[TUYA_BLE_TX_BUF_SIZE];
    uint8_t rx_frame[TUYA_BLE_TX_BUF_SIZE];
    uint32_t wifi_scan_token;
    uint16_t wifi_scan_count;
    bool wifi_scan_pending;
} tuya_ble_prov_state_t;

/* Config strings are borrowed and must outlive state: NUL-terminated product_key
 * (16 bytes), auth_key (32 bytes), uuid (16 bytes or 20 alphanumeric bytes).
 * Initialize the process PAL/cJSON hooks via iot_init() before delivering JSON.
 * State layout is not ABI-stable: recompile callers after upgrading this header. */
int tuya_ble_prov_init(tuya_ble_prov_state_t *state, const tuya_ble_prov_cfg_ext_t *cfg);
/* Legacy transport-only reset preserves paired; use close for GATT disconnect. */
void tuya_ble_prov_close(tuya_ble_prov_state_t *state);
/* Effective notification value budget, ATT MTU minus 3; default 20 bytes. */
int tuya_ble_prov_set_gatt_payload(tuya_ble_prov_state_t *state, uint16_t bytes);
int tuya_ble_prov_tx_ready(tuya_ble_prov_state_t *state);
/* Monotonic milliseconds; expires incomplete transport transfers after 10 s. */
int tuya_ble_prov_tick(tuya_ble_prov_state_t *state, uint64_t now_ms);
void tuya_ble_prov_reset_conn(tuya_ble_prov_state_t *state);
int tuya_ble_prov_on_data(tuya_ble_prov_state_t *state, const uint8_t *raw, uint16_t len);
void tuya_ble_prov_get_adv_data(const tuya_ble_prov_state_t *state,
                                 const uint8_t **adv_data, uint8_t *adv_len,
                                 const uint8_t **rsp_data, uint8_t *rsp_len);
void tuya_ble_prov_get_read_payload(const tuya_ble_prov_state_t *state,
                                     const uint8_t **adv_data, uint8_t *adv_len,
                                     const uint8_t **rsp_data, uint8_t *rsp_len);
/* Compatibility status setter; true never grants cryptographic authorization. */
void tuya_ble_prov_set_paired(tuya_ble_prov_state_t *state, bool paired);

/* Shared encrypted Frame sender for sibling BLE protocol modules. */
int tuya_ble_prov_send_frame(tuya_ble_prov_state_t *state, uint16_t cmd,
                             const uint8_t *data, uint16_t data_len,
                             uint8_t encrypt_mode);

#ifdef __cplusplus
}
#endif

#endif /* TUYA_BLE_PROV_H */
