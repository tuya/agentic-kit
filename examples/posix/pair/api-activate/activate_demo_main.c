/**
 * @file activate_demo_main.c
 * @brief Entry point for the API-based activation demo.
 *
 * The third-party backend obtains a pairing token via Tuya OpenAPI
 * (using tuya_openapi.py) and passes it to this program. The device
 * then activates itself on Tuya cloud using the token.
 *
 * Usage:
 *   ./activate_demo <token> [uuid] [authkey] [product_key] [firmware_key] [--release]
 *
 * --release runs the other half of the lifecycle: once activated, the device
 * hands the binding straight back via iot_client_reset(). Useful for checking
 * both directions in one run without touching the app.
 */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "activate_demo.h"

#define DEFAULT_UUID         "uuid17a65d2314ac60f5"
#define DEFAULT_AUTHKEY      "tNn74X0lff222ocdUVVFYmjP15oWr9Vn"
#define DEFAULT_PRODUCT_KEY  "5gkeobhit9sd6odu"
#define DEFAULT_FIRMWARE_KEY ""

int main(int argc, char *argv[])
{
    /* --release may appear anywhere after the token, so the positional
     * arguments keep their meaning whether or not it is present. */
    bool release = false;
    const char *pos[6] = {0};
    int npos = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--release") == 0) {
            release = true;
        } else if (npos < 6) {
            pos[npos++] = argv[i];
        }
    }

    if (npos < 1) {
        fprintf(stderr,
            "Usage: %s <token> [uuid] [authkey] [product_key] [firmware_key] [--release]\n"
            "\n"
            "  --release   after activating, immediately reset (unbind) the device\n"
            "              — the other end of the same lifecycle\n",
            argv[0]);
        return 1;
    }

    const char *token        = pos[0];
    const char *uuid         = pos[1] ? pos[1] : DEFAULT_UUID;
    const char *authkey      = pos[2] ? pos[2] : DEFAULT_AUTHKEY;
    const char *product_key  = pos[3] ? pos[3] : DEFAULT_PRODUCT_KEY;
    const char *firmware_key = pos[4] ? pos[4] : DEFAULT_FIRMWARE_KEY;

    printf("=== API-based activation demo ===\n");
    printf("Token        : %s\n", token);
    printf("UUID         : %s\n", uuid);
    printf("Product key  : %s\n", product_key);
    printf("Lifecycle    : %s\n", release ? "activate + release" : "activate only");
    printf("\n");

    int ret = demo_activate_run(token, uuid, authkey, product_key, firmware_key, release);
    return ret == 0 ? 0 : 1;
}
