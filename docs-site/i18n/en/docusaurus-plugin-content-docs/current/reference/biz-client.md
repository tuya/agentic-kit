---
title: Business Client API reference
sidebar_label: Business Client
---

# Business Client API reference

Module `modules/biz-client`, POSIX target `tuya_biz_client`, public headers
`tuya_biz_client.h` and `tuya_ai_role.h`.
See [AI role management](../guides/ai-role-management.md) for integration.

## Client and lifetime {#lifecycle}

```c
int tuya_biz_client_create(iot_client_t *iot, const char *request_lang,
                           tuya_biz_client_t **out);
void tuya_biz_client_destroy(tuya_biz_client_t *client);
```

Borrows the IoT Client and its PAL. NULL `request_lang` defaults to `zh`;
nonempty values can occupy at most 31 bytes. Failure clears `*out`.
Initialize IoT first; free results before destroying Business Client and finish
all calls before IoT teardown. Instances keep separate contexts and label
languages; the application owns serialized scheduling.

## Role APIs {#role-api}

All operations take a Business Client first and optional
`iot_atop_rejection_t *` last. Complete signatures are in the public header.

| API | Output or operation | ATOP v1.0 |
| --- | --- | --- |
| `tuya_ai_role_get_bound` | `tuya_ai_role_t` | `thing.ai.agent.role.get-bind-role` |
| `tuya_ai_role_list_templates` | `tuya_ai_role_list_t` | `thing.ai.agent.role.role-template.list` |
| `tuya_ai_role_list_custom` | explicit page/size, role list | `thing.ai.agent.role.custom-role.page` |
| `tuya_ai_role_bind` | role type and ID | `thing.ai.agent.role.bind-with-role` |
| `tuya_ai_role_apply_edit` | current role, edit, output ID | `thing.ai.agent.role.custom-role.create-from-template` or `thing.ai.agent.role.custom-role.update` |
| `tuya_ai_role_delete_custom` | custom role ID | `thing.ai.agent.role.custom-role.delete` |
| `tuya_ai_role_list_languages` | `tuya_ai_language_list_t` | `thing.ai.agent.config.languages.list` |
| `tuya_ai_role_list_timbres` | full language code, page/size, timbre list | `thing.ai.timbre.market.page` |

Every request includes `t`; list queries also include display `requestLang`.
Timbre queries use `lang`; NULL means unfiltered. No hidden next-page request.

## Types {#types}

`tuya_ai_role_t` has `role_id`, `template_id`, `type`, `name`, `description`,
`image_url`, `lang_code`, `lang_name`, `timbre_id`, `timbre_name`, `speed`.
List queries stamp the source role type. Bound-role queries read the cloud
type and default an omitted type to SINGLE.

`tuya_ai_role_edit_t` has optional `name`, `lang_code`, `timbre_id` pointers,
plus `set_speed` and `speed`. Templates create and request binding; custom
roles update in place. `out_id` needs `TUYA_AI_ROLE_ID_LEN` bytes and is
cleared on failure. Skip calls when nothing changed.

Role and timbre lists have `items`, `count`, `total_count` (-1 if unknown).
Language lists have `items` and `count`. Timbre `support_langs` is a
comma-separated display field preserving full codes; cloud filtering uses
the original language code.

List responses may be arrays or arrays under `datas`, `list`, `records`,
`rows`, `content`, `data`, `items`. Unknown shapes are rejected. Required
role IDs, language codes and voice IDs must be nonempty; oversized fields
reject the whole result rather than returning a partial list.

Bind, delete and update acknowledgements retain compatibility with the existing
TuyaOpen role implementation: after a successful ATOP envelope, a non-null,
non-false JSON result is opaque acknowledgement, not necessarily a Boolean.
Template creation specifically requires a nonempty new ID string. Read back
the binding to verify the intended role was selected.

## Release and errors {#release-and-errors}

```c
void tuya_ai_role_list_free(tuya_biz_client_t *, tuya_ai_role_list_t *);
void tuya_ai_language_list_free(tuya_biz_client_t *, tuya_ai_language_list_t *);
void tuya_ai_timbre_list_free(tuya_biz_client_t *, tuya_ai_timbre_list_t *);
```

Release through the original live context. Lists are zeroed after release;
failed queries leave empty outputs. Destroying the context does not free
outstanding result lists.

| Return | Meaning |
| --- | --- |
| `OPRT_OK` | Valid result; actual conversation effects need separate validation |
| `OPRT_INVALID_PARAMETER` | Invalid/missing arguments, no edits, type or pagination |
| `OPRT_UNINITIALIZED` | Unactivated device or obviously uninitialized system time |
| `OPRT_MALLOC_FAILED` | PAL/cJSON allocation failed |
| `OPRT_INVALID_RESULT` | Missing/malformed result, missing required ID, oversized fields |
| `OPRT_ATOP_BUSINESS_ERROR` | Cloud rejection; inspect optional code/msg |
| Other `OPRT_*` | Original ATOP transport failure |

Deletion does not query role provenance first; callers submit only confirmed
custom IDs. No avatar download, NVS, RTC calls or automatic retry occur.
