#ifndef UNBIND_DEMO_H
#define UNBIND_DEMO_H

/**
 * @file unbind_demo.h
 * @brief Cloud device-remove (unbind/reset) demo.
 *
 * Connects an already-activated device to MQTT and listens for the
 * cloud's protocol-11 device-remove notice. When the user removes the
 * device from the app, the callback fires and the demo exits.
 */

/**
 * @brief Run the unbind demo.
 *
 * @param devid       Device ID (from activation).
 * @param secret_key  Device secret key (from activation).
 * @param local_key   Device local key (from activation).
 * @return 0 on success, non-zero on error.
 */
int demo_unbind_run(const char *devid, const char *secret_key,
                    const char *local_key);

#endif /* UNBIND_DEMO_H */
