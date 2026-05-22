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

typedef int (*tuya_ble_hal_send_t)(const uint8_t *buf, uint16_t len, void *ctx);

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

    uint8_t trsmitr_seq;
    uint16_t peer_pkt_len;
    uint8_t rx_buf[TUYA_BLE_RX_BUF_SIZE];
    uint16_t rx_len;
    uint32_t rx_total_len;
    uint8_t tx_frame[TUYA_BLE_TX_BUF_SIZE];
    uint8_t tx_enc_pkt[TUYA_BLE_TX_BUF_SIZE];
    uint8_t tx_trsmitr_buf[TUYA_BLE_TX_BUF_SIZE];
    uint8_t rx_frame[TUYA_BLE_TX_BUF_SIZE];
} tuya_ble_prov_state_t;

int tuya_ble_prov_init(tuya_ble_prov_state_t *state, const tuya_ble_prov_cfg_ext_t *cfg);
void tuya_ble_prov_reset_conn(tuya_ble_prov_state_t *state);
int tuya_ble_prov_on_data(tuya_ble_prov_state_t *state, const uint8_t *raw, uint16_t len);
void tuya_ble_prov_get_adv_data(const tuya_ble_prov_state_t *state,
                                 const uint8_t **adv_data, uint8_t *adv_len,
                                 const uint8_t **rsp_data, uint8_t *rsp_len);
void tuya_ble_prov_get_read_payload(const tuya_ble_prov_state_t *state,
                                     const uint8_t **adv_data, uint8_t *adv_len,
                                     const uint8_t **rsp_data, uint8_t *rsp_len);
void tuya_ble_prov_set_paired(tuya_ble_prov_state_t *state, bool paired);

#ifdef __cplusplus
}
#endif

#endif /* TUYA_BLE_PROV_H */
