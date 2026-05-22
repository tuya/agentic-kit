/**
 * @file scan_by_device_pair_demo_main.c
 * @brief Entry point for the scan-by-device pairing demo.
 *
 * The app shows a QR code on screen; the device captures and decodes it.
 *
 * Usage:
 *   ./scan_by_device_pair_demo [qr.jpg] [uuid] [authkey] [product_key] [firmware_key]
 */

#include <stdio.h>
#include <stdlib.h>

#include "scan_by_device_pair_demo.h"

#define DEFAULT_JPG_PATH     "res/qr.jpg"
#define DEFAULT_UUID         "uuid17a65d2314ac60f5"
#define DEFAULT_AUTHKEY      "tNn74X0lff222ocdUVVFYmjP15oWr9Vn"
#define DEFAULT_PRODUCT_KEY  "5gkeobhit9sd6odu"
#define DEFAULT_FIRMWARE_KEY ""

int main(int argc, char *argv[])
{
    const char *jpg_path     = (argc >= 2) ? argv[1] : DEFAULT_JPG_PATH;
    const char *uuid         = (argc >= 3) ? argv[2] : DEFAULT_UUID;
    const char *authkey      = (argc >= 4) ? argv[3] : DEFAULT_AUTHKEY;
    const char *product_key  = (argc >= 5) ? argv[4] : DEFAULT_PRODUCT_KEY;
    const char *firmware_key = (argc >= 6) ? argv[5] : DEFAULT_FIRMWARE_KEY;

    printf("=== scan-by-device pair demo ===\n");
    printf("QR image     : %s\n", jpg_path);
    printf("UUID         : %s\n", uuid);
    printf("Product key  : %s\n", product_key);
    printf("\n");

    int ret = demo_scan_by_device_pair_run(jpg_path, uuid, authkey,
                                           product_key, firmware_key);
    return ret == 0 ? 0 : 1;
}
