#ifndef BLE_ONLY_DEMO_H
#define BLE_ONLY_DEMO_H

/**
 * @file ble_only_demo.h
 * @brief BLE-only (single-mode) provisioning demo: App-assisted activation.
 *
 * The device in this flow has no WiFi radio at all -- it is a BLE single-point
 * product. The phone App pairs over BLE, activates the device on the Tuya
 * cloud out of band (with the device's uuid/authkey and a pairing token), and
 * then delivers the Activation information back over BLE as the 发送设备激活
 * 信息 command (0x001B): a JSON payload {"devID","secKey","localKey","schema"}.
 * The SDK parses it, ACKs, installs the credentials and flips into the bound
 * form, after which local DP control runs over the KEY_15 session.
 *
 * This demo plays both sides in one process to make the whole flow runnable
 * on a POSIX host:
 *  - the "cloud leg" performs the App-side activation (it reuses
 *    iot_client_init_on_boarding_with_token with the device's uuid/authkey,
 *    which is the same cloud registration the App triggers; a real device
 *    never makes this call itself);
 *  - the "BLE leg" drives the tuya-ble state machine over an in-process
 *    loopback: device-info handshake, pair, 0x001B delivery, then a bound
 *    device-info + pair + DP write round.
 *
 * @param token       Pairing token from Tuya OpenAPI, or NULL for the offline
 *                    dry run (a synthetic payload is used and the cloud leg
 *                    is skipped).
 * @param uuid        Device UUID (16 bytes, or 20 alphanumeric bytes).
 * @param authkey     Device auth key (32 bytes).
 * @param product_key Product key (16 bytes).
 * @return 0 on success, non-zero on error.
 */
int demo_ble_only_run(const char *token,
                      const char *uuid, const char *authkey,
                      const char *product_key);

#endif /* BLE_ONLY_DEMO_H */
