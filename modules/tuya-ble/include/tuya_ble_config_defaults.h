/*
 * tuya_ble_config_defaults.h -- tuya-ble build-time knobs: the
 * AGENTIC_KIT_TUYA_BLE_* #ifndef defaults, nothing else. Non-knob module
 * data stays where it was (protocol geometry and port-API sizing in
 * include/tuya_ble_prov.h -- see the header comment there for why those
 * are not knobs).
 *
 * It pulls common/log.h FIRST: that header is where integrator overrides
 * (agentic_kit_config.h on the include path, -D, AGENTIC_KIT_USER_CONFIG)
 * are applied -- so overrides win over every knob below. Not a public
 * API header.
 */

#ifndef AGENTIC_KIT_TUYA_BLE_CONFIG_DEFAULTS_H
#define AGENTIC_KIT_TUYA_BLE_CONFIG_DEFAULTS_H

#include "log.h"

/* Optional per-module log ceiling. Defaults to the SDK-wide ceiling and can
 * only LOWER tuya-ble below it: the effective ceiling is the smaller of the
 * two, so a value above AGENTIC_KIT_LOG_LEVEL is clamped to it right below.
 * Gates the SDK-internal TUYA_BLE_HAL_LOG* binding in src/tuya_ble_internal.h.
 * The ceiling applies at the level a line actually emits at, so LOGI (which
 * dispatches at debug) needs 4, not 3. */
#ifndef AGENTIC_KIT_TUYA_BLE_LOG_LEVEL
#define AGENTIC_KIT_TUYA_BLE_LOG_LEVEL AGENTIC_KIT_LOG_LEVEL
#endif
/* Lower-only, enforced ONCE here: the HAL rebind in src/tuya_ble_internal.h
 * keys on this knob directly (including the HEXDUMP body), so clamp it to
 * the SDK-wide ceiling to make a too-high value a true no-op. */
#if AGENTIC_KIT_TUYA_BLE_LOG_LEVEL > AGENTIC_KIT_LOG_LEVEL
#undef AGENTIC_KIT_TUYA_BLE_LOG_LEVEL
#define AGENTIC_KIT_TUYA_BLE_LOG_LEVEL AGENTIC_KIT_LOG_LEVEL
#endif

#endif /* AGENTIC_KIT_TUYA_BLE_CONFIG_DEFAULTS_H */
