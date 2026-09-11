#include "tuya_ble_bigdata.h"

#include "cJSON.h"
#include "log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BIGDATA_FLAG_RESPONSE_REQUESTED 0x0001
#define BIGDATA_FLAG_SEGMENTED          0x0002

static int send_json(tuya_ble_prov_state_t *state, uint16_t subcmd,
                     const char *json, size_t len)
{
    uint8_t payload[4 + TUYA_BLE_WIFI_LIST_JSON_MAX];

    if (len > TUYA_BLE_WIFI_LIST_JSON_MAX) return -1;
    payload[0] = 0;
    payload[1] = BIGDATA_FLAG_RESPONSE_REQUESTED;
    payload[2] = (uint8_t)(subcmd >> 8);
    payload[3] = (uint8_t)subcmd;
    memcpy(payload + 4, json, len);
    return tuya_ble_prov_send_frame(state, TUYA_BLE_FRM_BIGDATA_UPLINK_RSP,
                                    payload, (uint16_t)(4 + len),
                                    TUYA_BLE_ENCRYPTION_MODE_KEY_12);
}

static int send_empty_list(tuya_ble_prov_state_t *state)
{
    static const char empty[] = "{\"wifi_list\":[]}";
    return send_json(state, TUYA_BLE_SUB_WIFI_LIST, empty, sizeof(empty) - 1);
}

static int compare_rssi_desc(const void *left, const void *right)
{
    const tuya_ble_wifi_ap_t *a = left;
    const tuya_ble_wifi_ap_t *b = right;
    return (b->rssi > a->rssi) - (b->rssi < a->rssi);
}

static int append_escaped(char *out, size_t cap, const char *in)
{
    size_t used = 0;

    for (size_t i = 0; i < TUYA_BLE_WIFI_SSID_MAX && in[i]; i++) {
        unsigned char c = (unsigned char)in[i];
        const char *escape = NULL;
        if (c == '"') escape = "\\\"";
        else if (c == '\\') escape = "\\\\";
        else if (c == '\b') escape = "\\b";
        else if (c == '\f') escape = "\\f";
        else if (c == '\n') escape = "\\n";
        else if (c == '\r') escape = "\\r";
        else if (c == '\t') escape = "\\t";
        if (escape != NULL) {
            size_t n = strlen(escape);
            if (used + n >= cap) return -1;
            memcpy(out + used, escape, n);
            used += n;
        } else if (c < 0x20) {
            if (used + 6 >= cap) return -1;
            int n = snprintf(out + used, cap - used, "\\u%04x", c);
            if (n != 6) return -1;
            used += 6;
        } else {
            if (used + 1 >= cap) return -1;
            out[used++] = (char)c;
        }
    }
    if (used >= cap) return -1;
    out[used] = '\0';
    return (int)used;
}

static int send_wifi_list(tuya_ble_prov_state_t *state,
                          const tuya_ble_wifi_ap_t *aps, uint16_t count)
{
    char json[TUYA_BLE_WIFI_LIST_JSON_MAX + 1];
    size_t used = sizeof("{\"wifi_list\":[") - 1;

    memcpy(json, "{\"wifi_list\":[", used);
    for (uint16_t i = 0; i < count; i++) {
        char ssid[TUYA_BLE_WIFI_SSID_MAX * 6 + 1];
        int ssid_len = append_escaped(ssid, sizeof(ssid), aps[i].ssid);
        if (ssid_len < 0) continue;

        int n = snprintf(json + used, sizeof(json) - used,
                         "%s{\"ssid\":\"%s\",\"rssi\":%d,\"sec\":%u}",
                         used == sizeof("{\"wifi_list\":[") - 1 ? "" : ",",
                         ssid, (int)aps[i].rssi, (unsigned)aps[i].sec);
        if (n < 0 || (size_t)n + used + 2 > TUYA_BLE_WIFI_LIST_JSON_MAX) break;
        used += (size_t)n;
    }
    json[used++] = ']';
    json[used++] = '}';
    return send_json(state, TUYA_BLE_SUB_WIFI_LIST, json, used);
}

static uint16_t parse_count_and_ccode(const uint8_t *data, uint16_t len,
                                      char ccode[3])
{
    uint16_t count = 10;
    cJSON *root = cJSON_ParseWithLength((const char *)data, len);

    ccode[0] = '\0';
    if (root == NULL || !cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return count;
    }
    cJSON *item = cJSON_GetObjectItemCaseSensitive(root, "cnt");
    if (cJSON_IsNumber(item) && item->valueint > 0) {
        count = item->valueint > TUYA_BLE_WIFI_LIST_MAX
            ? TUYA_BLE_WIFI_LIST_MAX : (uint16_t)item->valueint;
    }
    item = cJSON_GetObjectItemCaseSensitive(root, "ccode");
    if (cJSON_IsString(item) && item->valuestring != NULL &&
        item->valuestring[0] >= 'A' && item->valuestring[0] <= 'Z' &&
        item->valuestring[1] >= 'A' && item->valuestring[1] <= 'Z' &&
        item->valuestring[2] == '\0') {
        ccode[0] = item->valuestring[0];
        ccode[1] = item->valuestring[1];
        ccode[2] = '\0';
    }
    cJSON_Delete(root);
    return count;
}

static int send_netcfg_status(tuya_ble_prov_state_t *state)
{
    /* Wiki §4.2: type 1 means a query response. The demo is waiting for
     * credentials, so stage 0 (CFG) has status 0 (success/ready). */
    static const char status[] = "{\"type\":1,\"stage\":0,\"status\":0}";
    return send_json(state, TUYA_BLE_SUB_NCFG_STAT, status, sizeof(status) - 1);
}

int tuya_ble_bigdata_on_downlink(tuya_ble_prov_state_t *state,
                                 const uint8_t *payload, uint16_t len)
{
    if (state == NULL || payload == NULL || len < 4) return -1;

    uint16_t flag = ((uint16_t)payload[0] << 8) | payload[1];
    uint16_t subcmd = ((uint16_t)payload[2] << 8) | payload[3];
    if (flag & BIGDATA_FLAG_SEGMENTED) {
        log_emit(LOG_WARN, "[ble] bigdata: segmented downlink unsupported");
        return -1;
    }
    if (subcmd == TUYA_BLE_SUB_NCFG_STAT) {
        return send_netcfg_status(state);
    }
    if (subcmd != TUYA_BLE_SUB_WIFI_LIST) {
        log_emit(LOG_WARN, "[ble] bigdata: unsupported subcommand 0x%04x", subcmd);
        return -1;
    }
    if (state->wifi_scan_pending) {
        log_emit(LOG_WARN, "[ble] bigdata: WiFi scan already pending");
        return send_empty_list(state);
    }

    char ccode[3];
    uint16_t count = parse_count_and_ccode(payload + 4, len - 4, ccode);
    if (state->cfg.wifi_scan_request == NULL) {
        log_emit(LOG_WARN, "[ble] bigdata: no WiFi scan provider");
        return send_empty_list(state);
    }

    state->wifi_scan_token++;
    if (state->wifi_scan_token == 0) state->wifi_scan_token++;
    state->wifi_scan_count = count;
    state->wifi_scan_pending = true;
    if (state->cfg.wifi_scan_request(count, ccode, state->wifi_scan_token,
                                     state->cfg.wifi_scan_ctx) != 0) {
        state->wifi_scan_pending = false;
        log_emit(LOG_WARN, "[ble] bigdata: WiFi scan start failed");
        return send_empty_list(state);
    }
    return 0;
}

int tuya_ble_bigdata_wifi_list_complete(tuya_ble_prov_state_t *state,
                                        uint32_t token,
                                        const tuya_ble_wifi_ap_t *aps,
                                        uint16_t count)
{
    tuya_ble_wifi_ap_t sorted[TUYA_BLE_WIFI_LIST_MAX];

    if (state == NULL || !state->wifi_scan_pending || token == 0 ||
        token != state->wifi_scan_token || (count && aps == NULL)) {
        return -1;
    }
    state->wifi_scan_pending = false;
    uint16_t available = count;
    if (available > TUYA_BLE_WIFI_LIST_MAX) available = TUYA_BLE_WIFI_LIST_MAX;
    if (available == 0) return send_empty_list(state);

    memcpy(sorted, aps, (size_t)available * sizeof(sorted[0]));
    qsort(sorted, available, sizeof(sorted[0]), compare_rssi_desc);
    if (available > state->wifi_scan_count) available = state->wifi_scan_count;
    return send_wifi_list(state, sorted, available);
}
