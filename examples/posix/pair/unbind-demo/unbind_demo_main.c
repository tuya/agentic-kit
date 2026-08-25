/**
 * @file unbind_demo_main.c
 * @brief Entry point for the cloud device-remove (unbind) demo.
 *
 * Waits for the cloud's protocol-11 device-remove notice (a user removing the
 * device from the app). The device-initiated direction -- resetting to hand the
 * binding back -- is in pair/api-activate under --release, next to the
 * activation it undoes.
 *
 * Get the credentials from the activation demo's output
 * (examples/posix/pair/api-activate) or from dp_management_demo.
 *
 * Usage:
 *   ./unbind_demo <devid> <secret_key> <local_key>
 */

#include <stdio.h>
#include <string.h>

#include "unbind_demo.h"

static void usage(const char *prog)
{
    fprintf(stderr,
        "Usage: %s <devid> <secret_key> <local_key>\n"
        "\n"
        "Arguments:\n"
        "  devid       Device ID (from activation)\n"
        "  secret_key  Device secret key (from activation)\n"
        "  local_key   Device local key (from activation)\n"
        "\n"
        "The demo connects to MQTT and waits: remove the device from the Tuya\n"
        "app to trigger the cloud device-remove notice. To reset from the\n"
        "device side instead, see pair/api-activate --release.\n",
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

    printf("=== Unbind demo ===\n");
    printf("devid      : %s\n", devid);
    printf("\n");

    int ret = demo_unbind_run(devid, secret_key, local_key);
    return ret == 0 ? 0 : 1;
}
