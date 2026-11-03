# Business Client

Typed device-side cloud services over the IoT Client's public ATOP generic call.
The CMake target is `tuya_biz_client`; the public headers are
`tuya_biz_client.h` and `tuya_ai_role.h`. This module has no platform UI,
storage, background tasks, MQTT loop, or RTC Session ownership.

## Language

- **Business Client**: an instance borrowing one activated IoT Client and its
  PAL. Owns the display request language, not device credentials.
- **Role template**: cloud-provided read-only role. Editing creates a custom
  role with `needBind=true`; the template itself is never overwritten.
- **Custom role**: an editable role. Updating it does not change the device's
  current role binding. Only custom roles may be submitted for deletion.
- **Role binding**: the device's cloud-selected role, queried independently
  from its available role lists. It is not the user-device binding.
- **Reply language**: `useLangCode`. Keep its full cloud code, including any
  dialect suffix. It is distinct from `requestLang`, the label language.
- **Timbre**: a selectable TTS voice (`voiceId` from the market list,
  `useTimbreId` when editing a role).
- **Named wrapper** and **Generic call** retain the IoT Client's meanings.

## Contracts

- Public APIs are synchronous. The app owns scheduling and serializes use
  with client teardown. No request is automatically retried; template creation
  may have succeeded on a timeout, so query before resubmitting.
- Memory comes from the borrowed IoT PAL. cJSON uses the SDK-wide allocator
  installed by `iot_init`; never install a second set of hooks here.
- Output lists must be freed with the same live Business Client. Query outputs
  must be empty on entry; failures leave no partially parsed output.
- Preserve `OPRT_ATOP_BUSINESS_ERROR` and the cloud rejection code/message.
  An accepted envelope is not enough: each result must match the operation's
  shape. Empty lists are valid; absent/null results and false acknowledgements
  are invalid. IDs and strings are never silently truncated.
- Logs use `BIZ_LOG*` through the shared facade. `AGENTIC_KIT_BIZ_LOG_LEVEL`
  defaults to the SDK-wide ceiling and can only lower the business-service level.
- No RTC TCP Client dependency. The app verifies when a new role affects its
  next Event and decides if a new Session is required.

Decision: [ATOP business services remain separate](docs/adr/0001-business-services-use-public-atop.md).
