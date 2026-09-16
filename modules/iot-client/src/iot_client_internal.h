#ifndef __IOT_CLIENT_INTERNAL_H__
#define __IOT_CLIENT_INTERNAL_H__

/*
 * Internal (src-private) hooks for the client core, used by iot_client.c and
 * the host test suites. The public client API lives in include/iot_client.h.
 */

#include "iot_client.h"

/**
 * @brief Issue the two init-time version reports — SDK meta save and firmware
 *        version update — unless config->skip_version_report is set.
 *
 * Non-fatal by design: each failed report logs a warning and client init
 * proceeds, exactly as when this lived inline in iot_client_init(). Split out
 * so host tests can pin the skip / no-skip contract against the ATOP mock
 * with a hand-shaped client, without the DNS / activation path in front of it
 * (iot_client_init() with a non-empty devid queries the real IoT-DNS host).
 *
 * @return OPRT_OK when both reports were skipped or both succeeded;
 *         OPRT_INVALID_PARAMETER for a NULL client or config;
 *         otherwise the first failure (both reports are still attempted).
 */
int iot_client_report_init_versions(iot_client_t *client,
                                    const iot_client_config_t *config);

#endif /* __IOT_CLIENT_INTERNAL_H__ */
