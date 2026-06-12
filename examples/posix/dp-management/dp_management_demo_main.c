/**
 * @file dp_management_demo_main.c
 * @brief Entry point for the DP (Data Point) management demo.
 *
 * Run an activated device's steady-state DP loop: restore persisted state,
 * connect, report on connect, react to downlinks, push local changes, and
 * persist every change. Get the credentials below by running the activation
 * demo first (examples/posix/pair/api-activate) and copying what it prints.
 *
 * Usage:
 *   ./dp_management_demo <devid> <secret_key> <local_key> [schema_id]
 *
 * State is persisted next to the binary as dp_state.json / schema.json, so a
 * second run restores where the first left off.
 */

#include <stdio.h>

#include "dp_management_demo.h"

int main(int argc, char *argv[])
{
    if (argc < 4) {
        fprintf(stderr,
                "Usage: %s <devid> <secret_key> <local_key> [schema_id]\n"
                "  (get these from the activation demo's output)\n",
                argv[0]);
        return 1;
    }

    const char *devid      = argv[1];
    const char *secret_key = argv[2];
    const char *local_key  = argv[3];
    const char *schema_id  = (argc >= 5) ? argv[4] : "";

    printf("=== DP management demo ===\n");
    printf("devid     : %s\n", devid);
    printf("schema_id : %s\n\n", schema_id);

    int ret = demo_dp_management_run(devid, secret_key, local_key, schema_id);
    return ret == 0 ? 0 : 1;
}
