---
title: Biz Client API 参考
sidebar_label: Biz Client
---

# Biz Client API 参考

模块目录 `modules/biz-client`，POSIX 构建目标 `tuya_biz_client`，公开头文件 `tuya_biz_client.h`、`tuya_ai_role.h`。接入步骤见 [AI 角色管理](../guides/ai-role-management.md)。

## 客户端和生命周期 {#lifecycle}

```c
int tuya_biz_client_create(iot_client_t *iot, const char *request_lang,
                           tuya_biz_client_t **out);
void tuya_biz_client_destroy(tuya_biz_client_t *client);
```

借用 IoT Client 及其 PAL。`request_lang=NULL` 默认 `zh`，非空值最多 31 字节。创建失败时 `*out=NULL`。先 `iot_init`，再创建；所有结果必须在销毁客户端前释放，所有调用必须在销毁 IoT Client 前完成。多实例可独立持有客户端和展示语言；调用方负责串行调度。

## 角色 API {#role-api}

下表省略共同首参 `tuya_biz_client_t *` 和末尾可选参数 `iot_atop_rejection_t *`，完整声明以公开头文件为准。

| API | 中间参数 | ATOP v1.0 |
| --- | --- | --- |
| `tuya_ai_role_get_bound` | `tuya_ai_role_t *out` | `thing.ai.agent.role.get-bind-role` |
| `tuya_ai_role_list_templates` | `tuya_ai_role_list_t *out` | `thing.ai.agent.role.role-template.list` |
| `tuya_ai_role_list_custom` | `int page_no, int page_size, tuya_ai_role_list_t *out` | `thing.ai.agent.role.custom-role.page` |
| `tuya_ai_role_bind` | `tuya_ai_role_type_t type, const char *role_id` | `thing.ai.agent.role.bind-with-role` |
| `tuya_ai_role_apply_edit` | `const tuya_ai_role_t *cur, const tuya_ai_role_edit_t *edit, char out_id[TUYA_AI_ROLE_ID_LEN]` | `thing.ai.agent.role.custom-role.create-from-template` 或 `thing.ai.agent.role.custom-role.update` |
| `tuya_ai_role_delete_custom` | `const char *role_id` | `thing.ai.agent.role.custom-role.delete` |
| `tuya_ai_role_list_languages` | `tuya_ai_language_list_t *out` | `thing.ai.agent.config.languages.list` |
| `tuya_ai_role_list_timbres` | `const char *lang, int page_no, int page_size, tuya_ai_timbre_list_t *out` | `thing.ai.timbre.market.page` |

每个请求加入 `t`；列表查询还携带展示语言 `requestLang`。音色查询通过 `lang` 筛选，`NULL` 表示不过滤。分页接口不会自动继续请求下一页。

## 数据类型 {#types}

`tuya_ai_role_t` 包含 `role_id`、`template_id`、`type`、`name`、`description`、`image_url`、`lang_code`、`lang_name`、`timbre_id`、`timbre_name` 和 `speed`。列表接口按来源设置类型，当前绑定查询读取云端类型；缺失类型按 `SINGLE` 处理。

`tuya_ai_role_edit_t` 包含可选 `name`、`lang_code`、`timbre_id` 指针，以及 `set_speed` 和 `speed`。模板分支创建并请求绑定，自定义分支原地更新。`out_id` 至少有 `TUYA_AI_ROLE_ID_LEN` 字节，失败时首字节清零。无修改时应跳过调用。

角色和音色列表包含 `items`、`count`、`total_count`，总数未知为 -1。语言列表包含 `items`、`count`。音色的 `support_langs` 是完整语种编码拼接的逗号串，仅用于展示；筛选仍由云端执行。

响应列表接受裸数组，或 `datas`、`list`、`records`、`rows`、`content`、`data`、`items` 中的数组。未知形状不猜测。角色 ID、语言编码、音色 ID 必须存在且非空；过长字段使整个结果失败，不返回半份列表。

绑定、删除和更新的确认结果保持与现有 TuyaOpen 角色实现兼容：ATOP 信封成功后，将非 null、非 false 的 JSON 结果视为不透明确认，不假定它必须是 Boolean。模板创建则必须返回非空的新 ID 字符串；是否正确绑定仍需回读确认。

## 释放与错误 {#release-and-errors}

```c
void tuya_ai_role_list_free(tuya_biz_client_t *, tuya_ai_role_list_t *);
void tuya_ai_language_list_free(tuya_biz_client_t *, tuya_ai_language_list_t *);
void tuya_ai_timbre_list_free(tuya_biz_client_t *, tuya_ai_timbre_list_t *);
```

使用原分配上下文释放，释放后列表清零。查询失败时输出清空。客户端销毁不是列表批量释放器，调用方先释放每份结果。

| 返回码 | 含义 |
| --- | --- |
| `OPRT_OK` | 接口结果有效；对话效果仍需实际验证 |
| `OPRT_INVALID_PARAMETER` | 空/非法参数、无改动、非法类型或分页参数 |
| `OPRT_UNINITIALIZED` | 设备未激活或系统时间明显未初始化 |
| `OPRT_MALLOC_FAILED` | PAL/cJSON 申请内存失败 |
| `OPRT_INVALID_RESULT` | 云端结果缺失、形状错误、必要 ID 缺失或字段过长 |
| `OPRT_ATOP_BUSINESS_ERROR` | 云端拒绝，读取可选 `rejection.code/msg` |
| 其他 `OPRT_*` | ATOP 传输错误原样返回 |

删除接口不会先验证角色来源；应用只能提交已确认的自定义 ID。模块不下载头像、不保存 NVS、不调用 RTC，不自动重试任何请求。
