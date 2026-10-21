---
title: Device AI role management
sidebar_label: AI role management
---

# Device AI role management

`biz-client` provides typed device-side cloud business APIs. Its first feature
set queries role templates, custom roles and the bound role; selects roles;
edits name, reply language, timbre and speed; and deletes custom roles.
Applications supply their own display, buttons or serial UI.

It uses the public `iot_atop_call()` API. IoT Client owns credentials, signing,
encryption and HTTPS. Business Client has no RTC TCP Client dependency.

## Prerequisites {#prerequisites}

- Call `iot_init()` or `iot_init_default()` first and supply an activated
  `iot_client_t`. A connected MQTT session is not required for role ATOP calls.
- Synchronize system time and configure the correct region and CA certificate
  or platform certificate bundle. Requests include POSIX seconds in `t`.
  An obviously uninitialized clock returns `OPRT_UNINITIALIZED` before sending.
- Configure available roles in the cloud; availability and access depend on
  the device/product identity.
- Size `AGENTIC_KIT_RESPONSE_BUFFER_SIZE` for actual lists. Its default 4096
  bytes covers HTTP headers and the encrypted response together, leaving less
  space for decrypted JSON. An unpaged template list may require a larger
  value, for example 32768, subject to measured memory. Apply the definition
  to the target compiling iot-client, not just an application source file.

## Create a Business Client {#client}

```c
#include "tuya_biz_client.h"
#include "tuya_ai_role.h"

tuya_biz_client_t *biz = NULL;
int rc = tuya_biz_client_create(iot, "en", &biz);
```

The existing IoT Client is borrowed. `request_lang` selects the language of
display labels, not the role's reply language; NULL defaults to `zh`.
Free all lists, destroy the Business Client, then destroy the IoT Client.
POSIX consumers link `tuya_biz_client`. The bundled ESP-IDF `agentic_kit`
component already includes its sources and public headers. PSRAM, NVS, LVGL
and any particular board are not required by the module.

## Query and bind {#query-and-bind}

Run these synchronous calls in an application-owned worker:

```c
tuya_ai_role_list_t templates = {0};
iot_atop_rejection_t rejection = {0};
int rc = tuya_ai_role_list_templates(biz, &templates, &rejection);
if (rc == OPRT_OK && templates.count > 0) {
    rc = tuya_ai_role_bind(biz, templates.items[0].type,
                          templates.items[0].role_id, &rejection);
}
tuya_ai_role_list_free(biz, &templates);

tuya_ai_role_t bound = {0};
if (rc == OPRT_OK) {
    rc = tuya_ai_role_get_bound(biz, &bound, &rejection);
}
```

Use returned IDs and types, never guess from display names or substitute a
`template_id` for `role_id`. Read back the bound role after a successful change.
Custom roles and timbres have explicit pagination:

```c
tuya_ai_role_list_t customs = {0};
rc = tuya_ai_role_list_custom(biz, 1, 5, &customs, &rejection);
tuya_ai_role_list_free(biz, &customs);

tuya_ai_timbre_list_t voices = {0};
rc = tuya_ai_role_list_timbres(biz, "zh_ha", 1, 5, &voices, &rejection);
tuya_ai_timbre_list_free(biz, &voices);
```

Page and size must be positive. `total_count=-1` means absent metadata;
an empty array is valid. No implicit all-pages load occurs. Pass the full
cloud language code, including dialect suffixes, to query matching timbres.

## Edit and delete {#edit-and-delete}

```c
tuya_ai_role_edit_t edit = {
    .name = "Study buddy", .set_speed = true, .speed = 1.20,
};
char effective_id[TUYA_AI_ROLE_ID_LEN] = {0};
rc = tuya_ai_role_apply_edit(biz, &selected, &edit,
                            effective_id, &rejection);
```

`selected` is a live role snapshot. Editing `TUYA_AI_ROLE_TEMPLATE=1`
creates a custom role with `needBind=true` and returns its new ID.
Editing `TUYA_AI_ROLE_CUSTOM=0` updates that role in place, returns the same
ID, and does not change the current binding. `TUYA_AI_ROLE_SINGLE=2` may
appear in query results but is not accepted by multi-role bind/edit APIs.

NULL string fields and `set_speed=false` preserve cloud values. Empty edits,
empty strings, overlong fields and nonfinite/nonpositive speeds are rejected.
The module serializes speed as a string; the cloud determines supported
ranges. Editable fields are name, language, timbre and speed, not system
prompts, models or knowledge bases.

Before `tuya_ai_role_delete_custom(biz, role_id, &rejection)`, confirm the ID
came from a custom list. Do not submit template IDs. Select and confirm a
different role before deleting the currently bound role; no automatic fallback
is defined by this module.

## Scheduling and memory {#scheduling-and-memory}

The SDK starts no task, retries nothing and caches nothing. All calls block on
HTTPS. Serialize them in an application worker and coordinate IoT teardown.
Never issue them from UI, MQTT or TAI receive callbacks.

Copy values into queued messages. `tuya_ai_role_edit_t` holds borrowed
pointers, so do not queue pointers to temporary UI strings. The ESP-IDF example
uses value-only `role_demo_job_t` messages.

Release role lists with `tuya_ai_role_list_free`, languages with
`tuya_ai_language_list_free`, and timbres with `tuya_ai_timbre_list_free`,
using the same live Business Client that allocated them. Initialize outputs to
zero and free old lists before reuse. Do not retain UI pointers after freeing.
Public string capacities are bytes including NUL; oversized cloud strings
return `OPRT_INVALID_RESULT` rather than silently truncating IDs or codes.

NVS caching, avatar fetching/decoding and UI remain application-owned. Cached
roles are display hints, not proof of current cloud binding; refresh online
and clear them when a device is unbound or transferred to another user.

## Errors and conversation effects {#errors-and-effect}

APIs return `OPRT_*` and clear their optional `iot_atop_rejection_t` on entry.
`OPRT_ATOP_BUSINESS_ERROR` preserves cloud `code`/`msg`; transport failures
retain their original code. An accepted envelope still needs a valid result.
Missing/null/false results or malformed shapes yield `OPRT_INVALID_RESULT`.

Do not blindly retry template creation after a timeout: the cloud may already
have created and bound it. Query bound/custom roles first. Distinguish an
accepted write from a failed subsequent refresh in the UI.

A confirmed binding does not prove the new role affects an existing AI
Session. Switch while idle, read the binding back, then verify a real new
voice Event, including language and timbre. Also check a newly established
Session. The application owns any required Session reconstruction; Business
Client never disconnects RTC itself.

## Examples and acceptance {#examples-and-acceptance}

- `examples/posix/ai/role-management`: explicit CLI commands for queries and
  mutations; no implicit destructive operation.
- `examples/esp-idf/ai/role-management`: reusable worker/queue component for an
  existing device project, not standalone provisioning firmware.
- API and cloud interface mapping: [Business Client reference](../reference/biz-client.md).

Verify lists, both role types, template edits producing a new ID, custom edits
keeping their ID, omitted-field preservation, matching language/timbre, custom
deletion, honest failure reporting, safe teardown, and actual subsequent voice
behavior.
