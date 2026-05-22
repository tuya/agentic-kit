#ifndef ACTIVATE_DEMO_H
#define ACTIVATE_DEMO_H

/**
 * @brief Run the API-based activation demo.
 *
 * The third-party backend obtains a pairing token via Tuya OpenAPI
 * and passes it to the device. The device uses that token to activate
 * itself on Tuya cloud via iot_client_init_on_boarding_with_token().
 *
 * @param token        Pairing token obtained from Tuya OpenAPI.
 * @param uuid         Device UUID (from Tuya IoT platform).
 * @param authkey      Device auth key.
 * @param product_key  Product key.
 * @param firmware_key Firmware key (empty string if not used).
 * @return 0 on success, non-zero on error.
 */
int demo_activate_run(const char *token,
                      const char *uuid, const char *authkey,
                      const char *product_key, const char *firmware_key);

#endif /* ACTIVATE_DEMO_H */
