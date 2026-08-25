#ifndef ACTIVATE_DEMO_H
#define ACTIVATE_DEMO_H

#include <stdbool.h>

/**
 * @brief Run the API-based activation demo.
 *
 * The third-party backend obtains a pairing token via Tuya OpenAPI
 * and passes it to the device. The device uses that token to activate
 * itself on Tuya cloud via iot_client_init_on_boarding_with_token().
 *
 * With @p release the demo also runs the other half of the pair: after
 * activating it immediately calls iot_client_reset() to give the binding back.
 * Activation and release are the two ends of one lifecycle -- a device that can
 * bind itself should be able to unbind itself -- so they are demonstrated
 * together rather than in separate programs.
 *
 * @param token        Pairing token obtained from Tuya OpenAPI.
 * @param uuid         Device UUID (from Tuya IoT platform).
 * @param authkey      Device auth key.
 * @param product_key  Product key.
 * @param firmware_key Firmware key (empty string if not used).
 * @param release      true = reset (unbind) the device once activated.
 * @return 0 on success, non-zero on error.
 */
int demo_activate_run(const char *token,
                      const char *uuid, const char *authkey,
                      const char *product_key, const char *firmware_key,
                      bool release);

#endif /* ACTIVATE_DEMO_H */
