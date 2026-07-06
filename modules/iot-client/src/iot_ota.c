#include "iot_ota.h"
#include "atop.h"
#include "iot_dp_internal.h"
#include "iot_config_defaults.h"

#include <string.h>

/**
 * @file iot_ota.c
 * @brief Public OTA API implementation — wraps the ATOP-layer functions,
 *        resolving ATOP host/port from the iot_client_t.
 *
 * Each public function follows the same pattern as iot_client_get_session_token():
 * resolve host/port via iot_client_resolve_atop_host(), then call the
 * corresponding atop_* function with client->devid / secret_key / cacert.
 */

int iot_ota_report_version(iot_client_t *client, const char *sw_ver)
{
    if (client == NULL || sw_ver == NULL || sw_ver[0] == '\0') {
        return OPRT_INVALID_PARAMETER;
    }

    char host[64] = {0};
    uint16_t port = IOT_DEFAULT_PORT;
    iot_client_resolve_atop_host(client, host, sizeof(host), &port);

    ota_version_update_request_t req = {
        .devid   = client->devid,
        .key     = client->secret_key,
        .sw_ver  = sw_ver,
        .pv      = IOT_SDK_PV,
        .bv      = IOT_SDK_BV,
        .channel = 0,
        .host    = host[0] ? host : NULL,
        .port    = port,
        .cacert  = client->cacert,
        .cert_bundle_attach = client->cert_bundle_attach,
    };

    return atop_version_update(client->pal, &req);
}

int iot_ota_check_upgrade(iot_client_t *client, int channel, const char *sw_ver,
                          iot_ota_upgrade_info_t *info)
{
    if (client == NULL || info == NULL) {
        return OPRT_INVALID_PARAMETER;
    }

    memset(info, 0, sizeof(iot_ota_upgrade_info_t));

    char host[64] = {0};
    uint16_t port = IOT_DEFAULT_PORT;
    iot_client_resolve_atop_host(client, host, sizeof(host), &port);

    ota_upgrade_request_t req = {
        .devid   = client->devid,
        .key     = client->secret_key,
        .channel = channel,
        .sw_ver  = sw_ver,
        .host    = host[0] ? host : NULL,
        .port    = port,
        .cacert  = client->cacert,
        .cert_bundle_attach = client->cert_bundle_attach,
    };

    ota_upgrade_response_t resp = {0};
    int rt = atop_upgrade_get(client->pal, &req, &resp);
    if (rt != OPRT_OK) {
        return rt;
    }

    /* Map internal response to public struct */
    info->has_upgrade = resp.has_upgrade;
    info->version     = resp.version;
    info->url         = resp.url;
    info->file_size   = resp.file_size;
    info->channel     = resp.channel;
    info->md5         = resp.md5;
    info->hmac        = resp.hmac;

    return OPRT_OK;
}

int iot_ota_report_status(iot_client_t *client, int channel, iot_ota_status_t status)
{
    if (client == NULL) {
        return OPRT_INVALID_PARAMETER;
    }

    char host[64] = {0};
    uint16_t port = IOT_DEFAULT_PORT;
    iot_client_resolve_atop_host(client, host, sizeof(host), &port);

    ota_status_update_request_t req = {
        .devid   = client->devid,
        .key     = client->secret_key,
        .channel = channel,
        .status  = status,
        .host    = host[0] ? host : NULL,
        .port    = port,
        .cacert  = client->cacert,
        .cert_bundle_attach = client->cert_bundle_attach,
    };

    return atop_upgrade_status_update(client->pal, &req);
}

void iot_ota_upgrade_info_free(iot_client_t *client, iot_ota_upgrade_info_t *info)
{
    if (client == NULL || info == NULL) {
        return;
    }
    const pal_t *pal = client->pal;
    if (info->version) pal->free(info->version);
    if (info->url)     pal->free(info->url);
    if (info->md5)     pal->free(info->md5);
    if (info->hmac)    pal->free(info->hmac);
    memset(info, 0, sizeof(iot_ota_upgrade_info_t));
}
