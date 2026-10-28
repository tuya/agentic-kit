/* LAN-only example. Production applications reuse their existing IoT client. */
#include "iot_client.h"
#include "iot_dp.h"
#include "tuya_lan.h"
#include "log.h"
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

static volatile sig_atomic_t running = 1;
typedef struct { iot_client_t *client; bool report_pending; } app_t;

static void stop_requested(int sig) { (void)sig; running = 0; }

static int parse_port(const char *text, uint16_t *port)
{
    char *end = NULL;
    if (!text || !*text || *text == '-' || *text == '+') return -1;
    errno = 0;
    unsigned long n = strtoul(text, &end, 10);
    if (errno || *end || n > 65535) return -1;
    *port = (uint16_t)n;
    return 0;
}

static int apply_dps(const char *json, size_t len, void *user)
{
    app_t *app = user;
    int rc = iot_client_apply_local_dps(app->client, json, len);
    if (rc == OPRT_OK) app->report_pending = true;
    return rc;
}

static int query_dps(char *json, size_t capacity, size_t *len, void *user)
{
    app_t *app = user;
    char *state = NULL;
    if (iot_dp_dump_json(app->client, &state) != OPRT_OK) return LAN_ERR_MEMORY;
    size_t n = strlen(state);
    int rc = n <= capacity ? LAN_OK : LAN_ERR_LIMIT;
    if (rc == LAN_OK) { memcpy(json, state, n); *len = n; }
    app->client->pal->free(state);
    return rc;
}

static void drive_device(uint8_t id, const iot_dp_value_t *value, void *user)
{
    (void)user;
    if (id == 1 && value->type == IOT_DP_TYPE_BOOL)
        log_tag_info("lan-demo", "switch DP %u = %d", id, value->value.boolean);
}

int main(int argc, char **argv)
{
    const char *device_id = getenv("TUYA_DEVICE_ID");
    const char *local_key = getenv("TUYA_LOCAL_KEY");
    const char *uuid = getenv("TUYA_UUID");
    const char *product_key = getenv("TUYA_PRODUCT_KEY");
    if (argc < 2 || argc > 4 || !device_id || !local_key || !uuid || !product_key) {
        log_tag_error("lan-demo", "Usage: lan_demo <device-ipv4> [tcp-port [udp-port]]; set TUYA_DEVICE_ID, "
                      "TUYA_LOCAL_KEY, TUYA_UUID, TUYA_PRODUCT_KEY from saved activation credentials");
        return 2;
    }
    if (iot_init_default() != OPRT_OK) return 1;
    /* Empty cloud credentials avoid DNS and MQTT: this client owns only the
     * example's DP registry. A product passes its already activated client. */
    iot_client_config_t iot_config = {
        .schema = "[{\"id\":1,\"type\":\"obj\",\"mode\":\"rw\","
                  "\"property\":{\"type\":\"bool\"}}]",
        .mqtt_disable_auto_connect = true,
        .skip_version_report = true
    };
    app_t app = { .client = iot_client_init(&iot_config) };
    if (!app.client) return 1;
    iot_dp_value_t initial = { .type = IOT_DP_TYPE_BOOL, .value.boolean = false };
    if (iot_dp_set(app.client, 1, &initial) != OPRT_OK) {
        iot_client_deinit(app.client);
        return 1;
    }
    iot_dp_set_callback(app.client, drive_device, &app);
    tuya_lan_config_t config;
    tuya_lan_config_init(&config);
    if ((argc > 2 && parse_port(argv[2], &config.tcp_port)) ||
        (argc > 3 && parse_port(argv[3], &config.udp_port))) {
        iot_client_deinit(app.client);
        return 2;
    }
    config.pal = app.client->pal;
    config.device_id = device_id;
    config.local_key = local_key;
    config.uuid = uuid;
    config.product_key = product_key;
    config.ipv4 = argv[1];
    config.bind_ip = argv[1];
    if (strcmp(argv[1], "127.0.0.1") == 0) config.broadcast_ip = argv[1];
    config.on_dp = apply_dps;
    config.on_query = query_dps;
    config.user = &app;
    tuya_lan_t *lan = NULL;
    int rc = tuya_lan_create(&config, &lan);
    if (rc == LAN_OK) rc = tuya_lan_start(lan);
    if (rc != LAN_OK) {
        log_tag_error("lan-demo", "LAN start failed: %d", rc);
        tuya_lan_destroy(lan);
        iot_client_deinit(app.client);
        return 1;
    }
    signal(SIGINT, stop_requested);
    signal(SIGTERM, stop_requested);
    log_tag_info("lan-demo", "LAN listening: TCP %u, UDP %u",
                 tuya_lan_tcp_port(lan), tuya_lan_udp_port(lan));
    while (running) {
        rc = tuya_lan_process(lan);
        if (rc != LAN_OK) break;
        /* Application policy: explicitly notify LAN peers after local controls.
         * This does not publish MQTT or clear the cloud's DP dirty bits. */
        if (app.report_pending) {
            char *state = NULL;
            if (iot_dp_dump_json(app.client, &state) == OPRT_OK) {
                int report_rc = tuya_lan_report(lan, state, strlen(state));
                if (report_rc == LAN_OK) app.report_pending = false;
                app.client->pal->free(state);
            }
        }
        app.client->pal->sleep_ms(10);
    }
    tuya_lan_destroy(lan);
    iot_client_deinit(app.client);
    return rc == LAN_OK ? 0 : 1;
}
