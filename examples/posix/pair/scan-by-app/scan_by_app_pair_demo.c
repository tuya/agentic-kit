/**
 * @file scan_by_app_pair_demo.c
 * @brief Scan-by-app pairing demo: device shows QR in terminal, app scans it.
 *
 * Flow:
 *  1. Fetch the activation QR-code URL from Tuya cloud.
 *  2. Encode the URL into a QR code using nayuki/qrcodegen.
 *  3. Print the QR code as Unicode block characters in the terminal.
 *  4. Wait for the user to scan (or use a supplied token directly).
 *  5. After activation, obtain a session token and listen briefly.
 */

#include "scan_by_app_pair_demo.h"
#include "iot_client.h"
#include "qrcodegen.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#define TAG "scan_by_app"

/* -- MQTT message callback ------------------------------------------------- */

static void message_callback(const char *topic, size_t topic_len,
                              const uint8_t *data, size_t data_len)
{
    printf("[MESSAGE] topic=%.*s, %zu bytes: %.*s\n",
           (int)topic_len, topic, data_len, (int)data_len, data);
}

/* -- QR code terminal renderer --------------------------------------------- */

static void print_qr_terminal(const uint8_t qrcode[])
{
    int size = qrcodegen_getSize(qrcode);
    int quiet = 2;

    for (int y = -quiet; y < size + quiet; y += 2) {
        for (int x = -quiet; x < size + quiet; x++) {
            bool top    = qrcodegen_getModule(qrcode, x, y);
            bool bottom = (y + 1 < size + quiet)
                        ? qrcodegen_getModule(qrcode, x, y + 1)
                        : false;

            if (top && bottom)
                fputs("\xe2\x96\x88", stdout);   /* U+2588 */
            else if (top)
                fputs("\xe2\x96\x80", stdout);   /* U+2580 */
            else if (bottom)
                fputs("\xe2\x96\x84", stdout);   /* U+2584 */
            else
                fputc(' ', stdout);
        }
        fputc('\n', stdout);
    }
}

/* -- Public entry point ---------------------------------------------------- */

int demo_scan_by_app_pair_run(const char *uuid, const char *authkey,
                              const char *product_key, const char *token)
{
    iot_on_boarding_config_t ob_config = {
        .timeout_ms       = 120000,
        .mqtt_disable_tls = true,
        .env              = PROD,
    };
    memcpy((char *)ob_config.uuid,        uuid,        strlen(uuid));
    memcpy((char *)ob_config.authkey,     authkey,     strlen(authkey));
    memcpy((char *)ob_config.product_key, product_key, strlen(product_key));

    iot_init_default();

    /* -- Step 1: Fetch QR code URL from cloud ------------------------------ */
    iot_qrcode_request_t qr_req = {
        .uuid    = ob_config.uuid,
        .authkey = ob_config.authkey,
        .app_id  = "",
        .type    = 1,
        .env     = ob_config.env,
    };
    iot_qrcode_response_t qr_resp = {0};

    int qr_ret = iot_get_qrcode_info(&qr_req, &qr_resp);
    if (qr_ret != OPRT_OK || !qr_resp.url) {
        fprintf(stderr, "[%s] iot_get_qrcode_info failed (%d)\n", TAG, qr_ret);
        return -1;
    }

    printf("=== QR Code URL ===\n  %s\n\n", qr_resp.url);

    /* -- Step 2: Render QR code in terminal -------------------------------- */
    uint8_t qr_buf[qrcodegen_BUFFER_LEN_MAX];
    uint8_t tmp_buf[qrcodegen_BUFFER_LEN_MAX];

    bool ok = qrcodegen_encodeText(
        qr_resp.url, tmp_buf, qr_buf,
        qrcodegen_Ecc_LOW, qrcodegen_VERSION_MIN, qrcodegen_VERSION_MAX,
        qrcodegen_Mask_AUTO, true);

    if (ok) {
        printf("=== Scan this QR code with the app to activate ===\n\n");
        print_qr_terminal(qr_buf);
        printf("\n");
    } else {
        printf("(QR code too long to render in terminal; use the URL above)\n\n");
    }

    free(qr_resp.url);

    /* -- Step 3: On-boarding ----------------------------------------------- */
    iot_client_t *client = NULL;

    if (token) {
        printf("=== On-boarding with token ===\n");
        printf("  token : %s\n\n", token);
        client = iot_client_init_on_boarding_with_token(&ob_config, token);
    } else {
        printf("=== Waiting for app to scan QR ... ===\n\n");
        client = iot_client_init_on_boarding(&ob_config);
    }

    if (!client) {
        fprintf(stderr, "[%s] On-boarding failed\n", TAG);
        return -1;
    }

    printf("=== On-boarding success ===\n");
    printf("  devid      : %s\n", client->devid);
    printf("  secret_key : %s\n", client->secret_key);
    printf("  local_key  : %s\n", client->local_key);
    printf("  schema_id  : %s\n", client->schema_id);
    printf("  region     : %d\n", client->region);
    printf("  env        : %d\n", client->env);
    printf("\n");

    client->message_callback = message_callback;

    /* -- Step 4: Session token --------------------------------------------- */
    char session_token[2048] = {0};
    int ret = iot_client_get_session_token(client, NULL, session_token, sizeof(session_token));
    if (ret != OPRT_OK) {
        fprintf(stderr, "[%s] iot_client_get_session_token failed: %d\n", TAG, ret);
        iot_client_deinit(client);
        return ret;
    }

    printf("=== Session Token ===\n%s\n\n", session_token);

    /* -- Step 5: Publish test & listen ------------------------------------- */
    const char *msg = "{\"cmd\":\"hello\"}";
    ret = iot_client_publish(client, (const uint8_t *)msg, strlen(msg));
    if (ret == OPRT_OK)
        printf("=== Published: %s ===\n\n", msg);
    else
        fprintf(stderr, "[%s] iot_client_publish failed: %d\n", TAG, ret);

    printf("=== Listening for messages (30s) ===\n");
    for (int i = 0; i < 300; i++)
        iot_client_process(client, 100);

    iot_client_deinit(client);
    printf("=== Done ===\n");
    return 0;
}
