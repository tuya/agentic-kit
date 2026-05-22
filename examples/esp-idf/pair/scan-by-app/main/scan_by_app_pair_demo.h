/**
 * @file scan_by_app_pair_demo.h
 * @brief Scan-by-app pairing demo: device shows QR, app scans it to activate.
 */

#ifndef SCAN_BY_APP_PAIR_DEMO_H
#define SCAN_BY_APP_PAIR_DEMO_H

/**
 * @brief Run the scan-by-app pairing demo.
 *
 * - QR mode  (token == NULL): fetch QR URL from cloud, render it as ASCII in
 *   the terminal, then wait for the user to scan it with the app.
 * - Token mode (token != NULL): skip QR and activate directly with the token.
 *
 * @param uuid        Device UUID.
 * @param authkey     Device auth key.
 * @param product_key Product key.
 * @param token       Activation token, or NULL for QR-scan mode.
 * @return 0 on success, non-zero on error.
 */
int demo_scan_by_app_pair_run(const char *uuid, const char *authkey,
                              const char *product_key, const char *token);

#endif /* SCAN_BY_APP_PAIR_DEMO_H */
