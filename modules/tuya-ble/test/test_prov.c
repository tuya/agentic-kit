#include "tuya_ble_prov.h"

#include <stdio.h>
#include <string.h>

#define EXPECT_TRUE(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        return 1; \
    } \
} while (0)

static int mock_send(const uint8_t *buf, uint16_t len, void *ctx)
{
    uint16_t *sent_len = (uint16_t *)ctx;
    EXPECT_TRUE(buf != NULL);
    EXPECT_TRUE(len > 0);
    if (sent_len != NULL) {
        *sent_len = len;
    }
    return 0;
}

static tuya_ble_prov_cfg_ext_t make_cfg(uint16_t *sent_len)
{
    tuya_ble_prov_cfg_ext_t cfg = {
        .device_name = "TuyaDevice",
        .product_key = "abcdefghijklmnop",
        .uuid = "abcdefghijklmnop",
        .auth_key = "0123456789abcdef0123456789abcdef",
        .cb = NULL,
        .send_fn = mock_send,
        .send_ctx = sent_len,
    };
    return cfg;
}

static int test_init_rejects_invalid_args(void)
{
    tuya_ble_prov_state_t state;
    tuya_ble_prov_cfg_ext_t cfg = make_cfg(NULL);

    EXPECT_TRUE(tuya_ble_prov_init(NULL, &cfg) != 0);
    EXPECT_TRUE(tuya_ble_prov_init(&state, NULL) != 0);

    cfg.device_name = NULL;
    EXPECT_TRUE(tuya_ble_prov_init(&state, &cfg) != 0);

    cfg = make_cfg(NULL);
    cfg.product_key = NULL;
    EXPECT_TRUE(tuya_ble_prov_init(&state, &cfg) != 0);

    cfg = make_cfg(NULL);
    cfg.uuid = NULL;
    EXPECT_TRUE(tuya_ble_prov_init(&state, &cfg) != 0);

    cfg = make_cfg(NULL);
    cfg.auth_key = NULL;
    EXPECT_TRUE(tuya_ble_prov_init(&state, &cfg) != 0);

    return 0;
}

static int test_init_builds_adv_and_rsp_data(void)
{
    tuya_ble_prov_state_t state;
    uint16_t sent_len = 0;
    tuya_ble_prov_cfg_ext_t cfg = make_cfg(&sent_len);

    EXPECT_TRUE(tuya_ble_prov_init(&state, &cfg) == 0);

    const uint8_t *adv_data = NULL;
    const uint8_t *rsp_data = NULL;
    uint8_t adv_len = 0;
    uint8_t rsp_len = 0;

    tuya_ble_prov_get_adv_data(&state, &adv_data, &adv_len, &rsp_data, &rsp_len);
    EXPECT_TRUE(adv_data == state.adv_data);
    EXPECT_TRUE(rsp_data == state.rsp_data);
    EXPECT_TRUE(adv_len > 0);
    EXPECT_TRUE(rsp_len > 0);
    EXPECT_TRUE(adv_len <= TUYA_BLE_ADV_DATA_LEN);
    EXPECT_TRUE(rsp_len <= TUYA_BLE_RSP_DATA_LEN);
    EXPECT_TRUE(sent_len == 0);

    return 0;
}

static int test_get_read_payload_matches_adv_data(void)
{
    tuya_ble_prov_state_t state;
    tuya_ble_prov_cfg_ext_t cfg = make_cfg(NULL);

    EXPECT_TRUE(tuya_ble_prov_init(&state, &cfg) == 0);

    const uint8_t *adv_data = NULL;
    const uint8_t *rsp_data = NULL;
    uint8_t adv_len = 0;
    uint8_t rsp_len = 0;
    const uint8_t *read_adv_data = NULL;
    const uint8_t *read_rsp_data = NULL;
    uint8_t read_adv_len = 0;
    uint8_t read_rsp_len = 0;

    tuya_ble_prov_get_adv_data(&state, &adv_data, &adv_len, &rsp_data, &rsp_len);
    tuya_ble_prov_get_read_payload(&state, &read_adv_data, &read_adv_len, &read_rsp_data, &read_rsp_len);

    EXPECT_TRUE(read_adv_data == adv_data);
    EXPECT_TRUE(read_rsp_data == rsp_data);
    EXPECT_TRUE(read_adv_len == adv_len);
    EXPECT_TRUE(read_rsp_len == rsp_len);

    return 0;
}

static int test_uuid_compression_modes(void)
{
    tuya_ble_prov_state_t state;
    tuya_ble_prov_cfg_ext_t cfg = make_cfg(NULL);

    cfg.uuid = "abcdefghijklmnop";
    EXPECT_TRUE(tuya_ble_prov_init(&state, &cfg) == 0);
    EXPECT_TRUE(state.is_id_comp == false);
    EXPECT_TRUE(memcmp(state.ble_id, cfg.uuid, strlen(cfg.uuid)) == 0);

    cfg.uuid = "abcdefghijklmnopqrst";
    EXPECT_TRUE(tuya_ble_prov_init(&state, &cfg) == 0);
    EXPECT_TRUE(state.is_id_comp == true);

    return 0;
}

static int test_reset_conn_clears_rx_state_only(void)
{
    tuya_ble_prov_state_t state;
    tuya_ble_prov_cfg_ext_t cfg = make_cfg(NULL);

    EXPECT_TRUE(tuya_ble_prov_init(&state, &cfg) == 0);
    state.trsmitr_seq = 7;
    state.rx_len = 12;
    state.rx_total_len = 34;
    state.paired = true;

    tuya_ble_prov_reset_conn(&state);

    EXPECT_TRUE(state.trsmitr_seq == 0);
    EXPECT_TRUE(state.rx_len == 0);
    EXPECT_TRUE(state.rx_total_len == 0);
    EXPECT_TRUE(state.paired == true);

    return 0;
}

static int test_set_paired_updates_state(void)
{
    tuya_ble_prov_state_t state;
    tuya_ble_prov_cfg_ext_t cfg = make_cfg(NULL);

    EXPECT_TRUE(tuya_ble_prov_init(&state, &cfg) == 0);
    EXPECT_TRUE(state.paired == false);

    tuya_ble_prov_set_paired(&state, true);
    EXPECT_TRUE(state.paired == true);

    tuya_ble_prov_set_paired(&state, false);
    EXPECT_TRUE(state.paired == false);

    tuya_ble_prov_set_paired(NULL, true);

    return 0;
}

static int test_on_data_rejects_invalid_args(void)
{
    tuya_ble_prov_state_t state;
    tuya_ble_prov_cfg_ext_t cfg = make_cfg(NULL);
    uint8_t raw[1] = {0};

    EXPECT_TRUE(tuya_ble_prov_init(&state, &cfg) == 0);
    EXPECT_TRUE(tuya_ble_prov_on_data(NULL, raw, sizeof(raw)) != 0);
    EXPECT_TRUE(tuya_ble_prov_on_data(&state, NULL, sizeof(raw)) != 0);
    EXPECT_TRUE(tuya_ble_prov_on_data(&state, raw, 0) != 0);

    return 0;
}

int main(void)
{
    EXPECT_TRUE(test_init_rejects_invalid_args() == 0);
    EXPECT_TRUE(test_init_builds_adv_and_rsp_data() == 0);
    EXPECT_TRUE(test_get_read_payload_matches_adv_data() == 0);
    EXPECT_TRUE(test_uuid_compression_modes() == 0);
    EXPECT_TRUE(test_reset_conn_clears_rx_state_only() == 0);
    EXPECT_TRUE(test_set_paired_updates_state() == 0);
    EXPECT_TRUE(test_on_data_rejects_invalid_args() == 0);

    printf("PASS test_prov\n");
    return 0;
}
