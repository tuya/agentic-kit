#ifndef APP_CONFIG_H
#define APP_CONFIG_H

/* WiFi credentials */
#define WIFI_SSID            "your-wifi-ssid"
#define WIFI_PASSWORD        "your-wifi-password"

/* Device credentials (from activation or persisted) */
#define DEFAULT_DEVID        "xxxxxxxxxxxxxxxxxxxxxx"
#define DEFAULT_SECRET_KEY   "xxxxxxxxxxxxxxxx"
#define DEFAULT_LOCAL_KEY    "xxxxxxxxxxxxxxxx"

/* Device region: AY, AZ, EU, UEAZ, WEAZ, IN, SG */
#define DEFAULT_REGION       AY
#define DEFAULT_ENV          PROD

/* CA certificate for Tuya cloud TLS (PEM format).
 * Set to NULL to use the bundled Tuya root CA if available. */
#define CLOUD_CACERT         NULL

#endif /* APP_CONFIG_H */
