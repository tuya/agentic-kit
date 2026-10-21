---
title: 设备端 AI 角色管理
sidebar_label: AI 角色管理
---

# 设备端 AI 角色管理

`biz-client` 为设备提供有类型的云端业务 API。第一组能力是 AI 角色管理：查询当前角色和角色列表、切换角色、编辑名称/语言/音色/语速、删除自定义角色。带屏设备可以绑定自己的列表和编辑页面，无屏设备也可以通过按键或串口调用。

本模块依赖 `iot-client` 的公开 `iot_atop_call()`。签名、加密和 HTTPS 由 IoT Client 处理，业务调用不需要再次提供设备密钥，也不依赖 RTC TCP Client。

## 接入前提 {#prerequisites}

- 已调用 `iot_init()` 或 `iot_init_default()`，并持有已激活的 `iot_client_t`；MQTT 连接不是角色 ATOP 请求的前提。
- 设备系统时间已校准，客户端配置了正确区域和 CA 证书或平台证书包。模块自动在请求体中加入当前 POSIX 秒数 `t`；时间明显未初始化时返回 `OPRT_UNINITIALIZED`，不发送请求。
- 产品在云端有可用角色。可访问范围、语言和音色以实际设备返回为准。
- ATOP 响应缓冲足够容纳角色列表。默认 `AGENTIC_KIT_RESPONSE_BUFFER_SIZE=4096` 同时容纳 HTTP 头和加密正文，解密后的 JSON 余量更小。模板列表不能分页时，客户应根据真实响应和设备内存增加此编译配置，例如 32768；它必须作用于编译 `iot-client` 的目标，不能只定义在应用源文件中。

## 创建业务客户端 {#client}

```c
#include "tuya_biz_client.h"
#include "tuya_ai_role.h"

tuya_biz_client_t *biz = NULL;
int rc = tuya_biz_client_create(iot, "zh", &biz);
```

`iot` 是应用已有的客户端。第二个参数 `request_lang` 决定角色名称等展示字段的语言，`NULL` 默认为 `zh`；它不同于角色回复语言。业务客户端借用 IoT Client 和其 PAL，退出时先释放列表、销毁业务客户端，再销毁 IoT Client。

POSIX 工程链接 `tuya_biz_client`；仓库自带的 ESP-IDF `agentic_kit` 组件已包含该模块源文件和公开头文件。模块不需要 PSRAM、NVS、LVGL 或特定芯片。

## 查询与切换 {#query-and-bind}

以下示例必须在应用工作任务中执行：

```c
tuya_ai_role_list_t templates = {0};
iot_atop_rejection_t rejection = {0};
int rc = tuya_ai_role_list_templates(biz, &templates, &rejection);
if (rc == OPRT_OK && templates.count > 0) {
    rc = tuya_ai_role_bind(biz, templates.items[0].type,
                          templates.items[0].role_id, &rejection);
}
tuya_ai_role_list_free(biz, &templates);

/* 绑定成功后回读，不以 UI 提示代替云端确认。 */
tuya_ai_role_t bound = {0};
if (rc == OPRT_OK) {
    rc = tuya_ai_role_get_bound(biz, &bound, &rejection);
}
```

使用列表返回的 `role_id` 和类型，不根据名称猜 ID。`template_id` 用于关联模板来源，不能代替接口要求的角色 ID。

自定义角色和音色支持显式分页：

```c
tuya_ai_role_list_t customs = {0};
rc = tuya_ai_role_list_custom(biz, 1, 5, &customs, &rejection);
/* 消费 customs.items，然后释放。 */
tuya_ai_role_list_free(biz, &customs);

tuya_ai_timbre_list_t voices = {0};
rc = tuya_ai_role_list_timbres(biz, "zh_ha", 1, 5, &voices, &rejection);
tuya_ai_timbre_list_free(biz, &voices);
```

`page_no`、`page_size` 必须为正数。`total_count=-1` 表示云端没有提供总数；空数组是正常结果。模块不会自动加载所有页。选择语言后，用云端返回的完整编码重新查询音色，例如保留 `zh_ha`，不要截断成 `zh`。

## 修改与删除 {#edit-and-delete}

```c
/* selected 是已经获取的角色快照，且在调用期间有效。 */
tuya_ai_role_edit_t edit = {
    .name = "学习伙伴",
    .set_speed = true,
    .speed = 1.20,
};
char effective_id[TUYA_AI_ROLE_ID_LEN] = {0};
rc = tuya_ai_role_apply_edit(biz, &selected, &edit,
                            effective_id, &rejection);
```

| 目标 | 保存行为 |
| --- | --- |
| `TUYA_AI_ROLE_TEMPLATE=1` | 基于模板创建自定义角色，并发送 `needBind=true` 请求绑定；成功后返回新 ID |
| `TUYA_AI_ROLE_CUSTOM=0` | 更新原自定义角色，返回原 ID，不自动切换绑定 |
| `TUYA_AI_ROLE_SINGLE=2` | 可识别为查询结果；不接受本模块的多角色编辑和绑定操作 |

字符串指针为 `NULL`、`set_speed=false` 表示不修改该字段。只提交真正修改的字段；空编辑、空字符串、过长值和非正/非有限语速会被拒绝。`speed` 在云端请求中是字符串，由模块转换；云端决定支持的语速范围。

当前可编辑字段仅有 `name`、`lang_code`、`timbre_id`、`speed`，不包含系统提示词、模型或知识库设置。

调用 `tuya_ai_role_delete_custom(biz, role_id, &rejection)` 删除自定义角色前，应用应确认该 ID 来自自定义列表。不要提交模板 ID；删除当前角色前先绑定其他可用角色并回读确认。模块不提供删除后的自动回退策略。

## 调度与内存 {#scheduling-and-memory}

SDK 不自动创建线程、不重试、不缓存。每次角色操作都同步阻塞到 HTTPS 请求返回。应用在自己的工作任务中串行调用，并协调 IoT 客户端销毁；不要从 UI、MQTT 或 TAI 接收回调直接调用网络接口。

异步消息必须复制参数内容。`tuya_ai_role_edit_t` 内是借用指针，不能直接把指向 UI 临时缓冲的结构体排队。ESP-IDF 示例提供按值复制的 `role_demo_job_t`。

| 分配结果 | 释放接口 |
| --- | --- |
| 模板/自定义角色列表 | `tuya_ai_role_list_free(biz, &list)` |
| 语言列表 | `tuya_ai_language_list_free(biz, &list)` |
| 音色列表 | `tuya_ai_timbre_list_free(biz, &list)` |
| 业务客户端 | `tuya_biz_client_destroy(biz)` |

查询前将输出清零；再次使用输出变量前先释放旧列表。释放时必须使用分配它的同一个、仍有效的业务客户端，列表指针不能在释放后交给 UI。所有容量单位为字节，包含结尾 `NUL`；云端返回字符串过长时返回 `OPRT_INVALID_RESULT`，不会截断 ID 或语言编码。

角色缓存、头像 HTTP 下载/解码和 UI 由应用实现。缓存只能用于展示，联网后应回读云端；设备解绑/换用户时应清理业务缓存。

## 错误与对话生效 {#errors-and-effect}

所有接口返回 `OPRT_*`。可选 `iot_atop_rejection_t` 在调用入口清零，`OPRT_ATOP_BUSINESS_ERROR` 时携带云端 `code`、`msg`；传输错误保持原返回码。成功的云端信封也必须具有符合接口形状的结果，缺失/null/false 或格式错误返回 `OPRT_INVALID_RESULT`。

不自动重试模板创建：超时可能意味着云端已创建并绑定，应先查询当前绑定和自定义列表，避免重复创建。写入已成功但回读失败时，应分别展示“变更已接受”和“状态待刷新”。

云端绑定成功不等于已在当前 AI Session 生效。空闲时切换后，先核对绑定，再发起新一轮真实语音验证角色、语言及音色；同时验证重建 Session 的情况。是否需要新 Session 由客户云端配置和实际对话结果决定，由应用会话管理任务协调，`biz-client` 不会自行断开 RTC。

## 示例与验收 {#examples-and-acceptance}

- POSIX：`examples/posix/ai/role-management`，提供查询、绑定、编辑、删除的命令行入口，真实变更只在显式命令下执行。
- ESP-IDF：`examples/esp-idf/ai/role-management`，可接入已有设备工程的组件，包含工作队列和退出流程；不是独立配网固件。
- API、ATOP 对照及完整签名见 [Biz Client 参考](../reference/biz-client.md)。

至少验收模板/自定义查询、两种角色切换、模板修改产生新 ID、自定义修改保留 ID、未修改字段保留、语言和音色匹配、自定义删除、网络失败不误报成功、退出后无悬空客户端，以及新一轮实际语音效果。
