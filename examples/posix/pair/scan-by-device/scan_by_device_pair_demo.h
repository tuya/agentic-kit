#ifndef SCAN_BY_DEVICE_PAIR_DEMO_H
#define SCAN_BY_DEVICE_PAIR_DEMO_H

/**
 * @brief Run the scan-by-device pairing demo.
 *
 * The app displays a QR code; the device decodes it from a JPEG image,
 * parses the JSON payload {"s":"<ssid>","p":"<password>","t":"<token>"},
 * and activates via iot_client_init_on_boarding_with_token using the token.
 *
 * @param jpg_path     Path to the JPEG image containing the QR code.
 * @param uuid         Device UUID (from Tuya IoT platform).
 * @param authkey      Device auth key.
 * @param product_key  Product key.
 * @param firmware_key Firmware key.
 * @return 0 on success, non-zero on error.
 */
int demo_scan_by_device_pair_run(const char *jpg_path,
                                 const char *uuid, const char *authkey,
                                 const char *product_key,
                                 const char *firmware_key);

#endif /* SCAN_BY_DEVICE_PAIR_DEMO_H */
