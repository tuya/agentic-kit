/**
 * @file scan_by_app_pair_demo_main.c
 * @brief Entry point for the scan-by-app pairing demo.
 *
 * The device displays a QR code in the terminal; the user scans it
 * with the Tuya app to complete activation.
 *
 * Usage:
 *   ./scan_by_app_pair_demo                 QR code mode (scan to activate)
 *   ./scan_by_app_pair_demo <token>         Token mode   (activate directly)
 *   ./scan_by_app_pair_demo -h | --help     Show help
 */

#include <stdio.h>
#include <string.h>

#include "scan_by_app_pair_demo.h"

#define DEFAULT_UUID         "uuid9425d7b7f6060cbc"
#define DEFAULT_AUTHKEY      "XvROCNN7e9HjaEonGBCnb7BLiinNN1jl"
#define DEFAULT_PRODUCT_KEY  "50fdsoli99bkgwri"

static void print_usage(const char *prog)
{
    printf("Usage:\n");
    printf("  %s                  QR code pairing (wait for app scan)\n", prog);
    printf("  %s <token>          Token pairing   (activate directly)\n", prog);
    printf("\n");
    printf("Token format: [region:2][activation_token][secret:4]\n");
    printf("  region  : AY, US, UE, EU, WE, IN, SG\n");
    printf("  Example : AYabcdef12340000\n");
}

int main(int argc, char *argv[])
{
    const char *token = NULL;

    if (argc > 1) {
        if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        }
        token = argv[1];
    }

    printf("=== scan-by-app pair demo ===\n");
    printf("UUID         : %s\n", DEFAULT_UUID);
    printf("Product key  : %s\n", DEFAULT_PRODUCT_KEY);
    printf("Mode         : %s\n\n", token ? "token" : "QR scan");

    return demo_scan_by_app_pair_run(DEFAULT_UUID, DEFAULT_AUTHKEY,
                                     DEFAULT_PRODUCT_KEY, token);
}
