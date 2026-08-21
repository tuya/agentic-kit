#ifndef OTA_DEMO_H
#define OTA_DEMO_H

#include "iot_client.h"
#include "iot_ota.h"

int demo_ota_run(const char *devid, const char *secret_key, const char *local_key,
                 const char *sw_ver, int auto_download);

int demo_ota_confirm_run(const char *devid, const char *secret_key,
                         const char *local_key, const char *region_name,
                         int download);

int ota_demo_verify_firmware(iot_client_t *client, const char *path,
                             const iot_ota_upgrade_info_t *info);

int ota_demo_download_firmware(const char *url, const char *out_path,
                               long expected_size);

#endif /* OTA_DEMO_H */
