/**
 * @file ota_demo_main.c
 * @brief Entry point for the POSIX OTA demo.
 *
 * Demonstrates the OTA cloud API: report version, check for upgrade, and
 * optionally download the firmware image. Get the credentials from the
 * activation demo's output (examples/posix/pair/api-activate).
 *
 * Usage:
 *   ./ota_demo <devid> <secret_key> <local_key> [sw_ver] [--download]
 *
 * Examples:
 *   # Just check for an upgrade (no download)
 *   ./ota_demo mydevid123 mysecretkey123 mylocalkey123 "1.0.0"
 *
 *   # Check and download the firmware image
 *   ./ota_demo mydevid123 mysecretkey123 mylocalkey123 "1.0.0" --download
 */

#include <stdio.h>
#include <string.h>

#include "ota_demo.h"

static void usage(const char *prog)
{
    fprintf(stderr,
        "Usage: %s <devid> <secret_key> <local_key> [sw_ver] [--download]\n"
        "\n"
        "Arguments:\n"
        "  devid       Device ID (from activation)\n"
        "  secret_key  Device secret key (from activation)\n"
        "  local_key   Device local key (from activation)\n"
        "  sw_ver      Current firmware version (default: \"1.0.0\")\n"
        "  --download  If given, download the firmware image to a local file\n"
        "\n"
        "The cloud-side OTA API calls are always made. Without --download,\n"
        "an ABORTED status is reported. With --download, the image is fetched\n"
        "via curl(1) and UPGRAD_FINI or UPGRD_EXEC is reported.\n",
        prog);
}

int main(int argc, char *argv[])
{
    if (argc < 4) {
        usage(argv[0]);
        return 1;
    }

    const char *devid      = argv[1];
    const char *secret_key = argv[2];
    const char *local_key  = argv[3];
    const char *sw_ver     = "1.0.0";
    int auto_download      = 0;

    /* Parse optional args starting from position 4 */
    for (int i = 4; i < argc; i++) {
        if (strcmp(argv[i], "--download") == 0) {
            auto_download = 1;
        } else if (argv[i][0] != '-') {
            sw_ver = argv[i];
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            usage(argv[0]);
            return 1;
        }
    }

    printf("=== OTA demo ===\n");
    printf("devid      : %s\n", devid);
    printf("sw_ver     : %s\n", sw_ver);
    printf("download   : %s\n\n", auto_download ? "yes" : "no");

    int ret = demo_ota_run(devid, secret_key, local_key, sw_ver, auto_download);
    return ret == 0 ? 0 : 1;
}
