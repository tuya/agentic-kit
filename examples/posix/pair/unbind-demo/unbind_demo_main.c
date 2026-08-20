/**
 * @file unbind_demo_main.c
 * @brief Entry point for the cloud device-remove (unbind) demo.
 *
 * Connects an already-activated device to MQTT and listens for the
 * cloud's protocol-11 device-remove notice. Get the credentials from
 * the activation demo's output (examples/posix/pair/api-activate) or
 * from dp_management_demo.
 *
 * Usage:
 *   ./unbind_demo <devid> <secret_key> <local_key>
 *
 * Example:
 *   ./unbind_demo mydevid123 mysecretkey123 mylocalkey123
 *
 * Then remove the device from the Tuya app; the demo prints the
 * reset type and exits.
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
        "The demo connects to MQTT and waits. Remove the device from\n"
        "the Tuya app to trigger the cloud device-remove notice.\n",
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
