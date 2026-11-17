/**
 * @file ble_only_demo_main.c
 * @brief Entry point for the BLE-only provisioning demo.
 *
 * Usage:
 *   ./ble_only_activate [token] [uuid] [authkey] [product_key]
 *
 * With a token the demo first performs the App-assisted cloud activation
 * (the registration a real App triggers for a WiFi-less device) and delivers
 * the real Activation information over the loopback BLE link. Without one it
 * runs offline with a synthetic payload -- the full BLE wire flow, no cloud.
 */

#include <stdio.h>
#include <string.h>

#include "ble_only_demo.h"

#define DEFAULT_UUID         "uuid17a65d2314ac60f5"
#define DEFAULT_AUTHKEY      "tNn74X0lff222ocdUVVFYmjP15oWr9Vn"
#define DEFAULT_PRODUCT_KEY  "5gkeobhit9sd6odu"

int main(int argc, char *argv[])
{
    /* Positional: [token] [uuid] [authkey] [product_key]. A token of "-"
     * explicitly selects the offline dry run. */
    const char *token = argc > 1 && strcmp(argv[1], "-") != 0 ? argv[1] : NULL;
    const char *uuid = argc > 2 ? argv[2] : DEFAULT_UUID;
    const char *authkey = argc > 3 ? argv[3] : DEFAULT_AUTHKEY;
    const char *product_key = argc > 4 ? argv[4] : DEFAULT_PRODUCT_KEY;

    printf("=== BLE-only provisioning demo (App-assisted activation) ===\n");
    printf("Cloud leg  : %s\n", token ? "real (token supplied)" : "offline dry run");
    printf("UUID       : %s\n", uuid);
    printf("Product key: %s\n\n", product_key);

    return demo_ble_only_run(token, uuid, authkey, product_key) == 0 ? 0 : 1;
}
