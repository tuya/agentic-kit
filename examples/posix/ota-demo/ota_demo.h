#ifndef OTA_DEMO_H
#define OTA_DEMO_H

int demo_ota_run(const char *devid, const char *secret_key, const char *local_key,
                 const char *sw_ver, int auto_download);

#endif /* OTA_DEMO_H */
