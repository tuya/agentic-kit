/**
 * @file unbind_demo.c
 * @brief Detect a cloud-initiated device removal.
 *
 * One direction only, on purpose. The device-initiated half -- calling
 * iot_client_reset() to hand the binding back -- lives with the activation it
 * undoes, in pair/api-activate under --release: binding and unbinding are the
 * two ends of one lifecycle, and splitting them across programs hides that.
 *
 * The cloud can push a protocol-11 notice when a user removes the device from
 * the app. If the device was offline, query its binding status over ATOP
 * before (re)connecting so a missed notice is still detected.
 *
 * Flow:
 *   1. Initialize iot_client with activated device credentials.
 *   2. Register a reset_callback that sets a flag and prints the type.
 *   3. Query the cloud for binding status, then connect to MQTT and pump the
 *      receive loop. Query again before each reconnect.
 *   4. On an unbound status or protocol-11 notice, leave the loop. After a
 *      notice, query binding status once more outside the callback, then exit.
 *
 * No storage is wiped here — the demo only demonstrates the calls.
 * See dp_management_demo for the full teardown + state-wipe pattern.
 */

#include "unbind_demo.h"

#include "iot_client.h"

#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>

#define TAG "unbind_demo"

static volatile sig_atomic_t g_running = 1;
static bool g_reset_received = false;

static void on_signal(int sig)
{
    (void)sig;
    g_running = 0;
}

static void print_removal_guidance(void)
{
    printf("[%s] On a real device: wipe credentials, clear stored state,\n", TAG);
    printf("[%s] and re-enter pairing mode.\n", TAG);
}

static bool binding_was_removed(iot_client_t *client, const char *stage)
{
    iot_binding_status_t status;
    int rc = iot_client_get_binding_status(client, &status);
    if (rc != OPRT_OK) {
        fprintf(stderr, "[%s] %s: binding status query failed: %d; status is unknown\n", TAG, stage, rc);
        return false;
    }

    switch (status) {
    case IOT_BINDING_STATUS_BOUND:
        printf("[%s] %s: cloud binding status: enable (bound)\n", TAG, stage);
        return false;
    case IOT_BINDING_STATUS_UNBOUND:
        printf("[%s] %s: cloud binding status: reset (unbound)\n", TAG, stage);
        break;
    case IOT_BINDING_STATUS_FACTORY_RESET:
        printf("[%s] %s: cloud binding status: reset_factory (factory reset requested)\n", TAG, stage);
        break;
    default:
        fprintf(stderr, "[%s] %s: unrecognized binding status; status is unknown\n", TAG, stage);
        return false;
    }

    return true;
}

static void on_reset(iot_reset_type_t type, void *user_data)
{
    (void)user_data;
    printf("\n[%s] ** device-remove notice received **\n", TAG);
    printf("[%s]    type : %s\n", TAG,
           type == IOT_RESET_REMOTE_FACTORY ? "factory_reset" : "remote_unbind");
    printf("[%s] The device was removed from the cloud.\n", TAG);
    print_removal_guidance();
    g_reset_received = true;
    g_running = 0;
}

static void on_message(const char *topic, size_t topic_len,
                       const uint8_t *data, size_t data_len)
{
    printf("[%s] message: topic=%.*s (%zu bytes)\n",
           TAG, (int)topic_len, topic, data_len);
}

int demo_unbind_run(const char *devid, const char *secret_key,
                    const char *local_key)
{
    if (iot_init_default() != OPRT_OK) {
        fprintf(stderr, "[%s] iot_init_default failed\n", TAG);
        return -1;
    }
    /* Log volume is decided at compile time now; build with
     * -DAGENTIC_KIT_LOG_LEVEL=3 to restore the info-only volume. */

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    iot_client_config_t cfg = {
        .region            = AY,
        .env               = PROD,
        .mqtt_disable_tls  = false,
        .mqtt_disable_auto_connect = true,
        .reset_callback    = on_reset,
        .message_callback  = on_message,
    };
    strncpy(cfg.devid,      devid,      sizeof(cfg.devid) - 1);
    strncpy(cfg.secret_key, secret_key, sizeof(cfg.secret_key) - 1);
    strncpy(cfg.local_key,  local_key,  sizeof(cfg.local_key) - 1);

    iot_client_t *client = iot_client_init(&cfg);
    if (!client) {
        fprintf(stderr, "[%s] iot_client_init failed\n", TAG);
        return -1;
    }
    printf("[%s] client initialized (devid=%s)\n", TAG, client->devid);

    int ret;
    if (binding_was_removed(client, "before unbind")) {
        print_removal_guidance();
        goto shutdown;
    }
    if (!g_running) {
        goto shutdown;
    }

    ret = iot_client_connect(client);
    if (ret != OPRT_OK) {
        fprintf(stderr, "[%s] MQTT connect failed: %d\n", TAG, ret);
        iot_client_deinit(client);
        return -1;
    }
    printf("[%s] MQTT connected\n", TAG);

    printf("[%s] waiting for device-remove notice\n", TAG);
    printf("[%s] Remove the device from the app to trigger the callback.\n", TAG);
    printf("[%s] Binding status will be queried again before reconnecting.\n", TAG);
    printf("[%s] (Ctrl-C to quit without unbinding)\n\n", TAG);

    while (g_running) {
        int rc = iot_client_process(client, 200);
        if (rc != OPRT_OK && g_running) {
            fprintf(stderr, "[%s] link error %d; reconnecting...\n", TAG, rc);
            iot_client_disconnect(client);
            sleep(2);
            if (!g_running) {
                break;
            }
            if (binding_was_removed(client, "before reconnect")) {
                print_removal_guidance();
                break;
            }
            ret = iot_client_connect(client);
            if (ret != OPRT_OK) {
                fprintf(stderr, "[%s] reconnect failed: %d\n", TAG, ret);
                continue;
            }
            printf("[%s] reconnected\n", TAG);
        }
    }

shutdown:
    printf("\n[%s] shutting down\n", TAG);
    iot_client_disconnect(client);
    if (g_reset_received) {
        /* The callback has returned; a blocking HTTPS query is safe here. */
        binding_was_removed(client, "after unbind");
    }
    iot_client_deinit(client);
    return 0;
}
