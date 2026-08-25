#ifndef UNBIND_DEMO_H
#define UNBIND_DEMO_H


/**
 * @file unbind_demo.h
 * @brief Detect a cloud-initiated device removal.
 *
 * Connect an already-activated device to MQTT and listen for the cloud's
 * protocol-11 device-remove notice, fired when a user removes the device from
 * the app.
 *
 * The device-initiated half -- resetting the device to hand the binding back --
 * is demonstrated in pair/api-activate under --release, next to the activation
 * it undoes.
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
