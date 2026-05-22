/**
 * @file activate_demo_main.c
 * @brief Entry point for the API-based activation demo.
 *
 * The third-party backend obtains a pairing token via Tuya OpenAPI
 * (using tuya_openapi.py) and passes it to this program. The device
 * then activates itself on Tuya cloud using the token.
 *
 * Usage:
 *   ./activate_demo <token> [uuid] [authkey] [product_key] [firmware_key]
 */

#include <stdio.h>
#include <stdlib.h>

#include "activate_demo.h"

#define DEFAULT_UUID         "uuid17a65d2314ac60f5"
#define DEFAULT_AUTHKEY      "tNn74X0lff222ocdUVVFYmjP15oWr9Vn"
#define DEFAULT_PRODUCT_KEY  "5gkeobhit9sd6odu"
#define DEFAULT_FIRMWARE_KEY ""

int main(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <token> [uuid] [authkey] [product_key] [firmware_key]\n",
                argv[0]);
        return 1;
    }

    const char *token        = argv[1];
    const char *uuid         = (argc >= 3) ? argv[2] : DEFAULT_UUID;
    const char *authkey      = (argc >= 4) ? argv[3] : DEFAULT_AUTHKEY;
    const char *product_key  = (argc >= 5) ? argv[4] : DEFAULT_PRODUCT_KEY;
    const char *firmware_key = (argc >= 6) ? argv[5] : DEFAULT_FIRMWARE_KEY;

    printf("=== API-based activation demo ===\n");
    printf("Token        : %s\n", token);
    printf("UUID         : %s\n", uuid);
    printf("Product key  : %s\n", product_key);
    printf("\n");

    int ret = demo_activate_run(token, uuid, authkey, product_key, firmware_key);
    return ret == 0 ? 0 : 1;
}
