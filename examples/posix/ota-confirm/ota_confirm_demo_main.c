/**
 * @file ota_confirm_demo_main.c
 * @brief Entry point for the APP-confirmed POSIX/macOS OTA demo.
 *
 * Credentials can be supplied as arguments or through environment variables:
 *   OTA_DEVID, OTA_SECRET_KEY, OTA_LOCAL_KEY, OTA_REGION
 *
 * Usage:
 *   ./ota_confirm_demo [devid secret_key local_key [region]] [--download]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ota_demo.h"

static void usage(const char *prog)
{
    fprintf(stderr,
        "Usage: [OTA_DEVID=x] [OTA_SECRET_KEY=x] [OTA_LOCAL_KEY=x] "
        "[OTA_REGION=AY] %s [devid secret_key local_key [region]] [--download]\n"
        "\n"
        "Arguments:\n"
        "  devid       Already-activated device ID\n"
        "  secret_key  Device secret key\n"
        "  local_key   Device local key\n"
        "  region      AY, AZ, UEAZ, EU, WEAZ, IN, SG (default: OTA_REGION or AY)\n"
        "  --download  Download and verify the confirmed image locally\n"
        "\n"
        "Environment variables are useful for avoiding sensitive values in the\n"
        "shell history. Query-only mode does not report OTA status.\n",
        prog);
}

static const char *credential(const char *arg, const char *name)
{
    if (arg && arg[0] != '\0') return arg;
    return getenv(name);
}

int main(int argc, char *argv[])
{
    const char *devid = NULL;
    const char *secret_key = NULL;
    const char *local_key = NULL;
    const char *region = getenv("OTA_REGION");
    int positional = 0;
    int download = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--download") == 0) {
            download = 1;
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            usage(argv[0]);
            return 0;
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "Unknown option: %s\n\n", argv[i]);
            usage(argv[0]);
            return 1;
        } else if (positional == 0) {
            devid = argv[i];
            positional++;
        } else if (positional == 1) {
            secret_key = argv[i];
            positional++;
        } else if (positional == 2) {
            local_key = argv[i];
            positional++;
        } else if (positional == 3) {
            region = argv[i];
            positional++;
        } else {
            fprintf(stderr, "Too many arguments\n\n");
            usage(argv[0]);
            return 1;
        }
    }

    devid = credential(devid, "OTA_DEVID");
    secret_key = credential(secret_key, "OTA_SECRET_KEY");
    local_key = credential(local_key, "OTA_LOCAL_KEY");
    if (region == NULL || region[0] == '\0') region = "AY";

    if (!devid || !secret_key || !local_key) {
        fprintf(stderr,
                "Missing credentials: provide arguments or set "
                "OTA_DEVID, OTA_SECRET_KEY, and OTA_LOCAL_KEY.\n\n");
        usage(argv[0]);
        return 1;
    }

    printf("=== APP-confirmed OTA demo ===\n");
    printf("devid    : %s\n", devid);
    printf("region   : %s\n", region);
    printf("download : %s\n\n", download ? "yes" : "no");

    int ret = demo_ota_confirm_run(devid, secret_key, local_key, region, download);
    return ret == 0 ? 0 : 1;
}
