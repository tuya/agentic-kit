# ESP-IDF 角色管理工作任务示例

这是接入已有设备工程的示例组件，不是独立整机固件。复用现有配网、校时和激活后的 `iot_client_t`，不引入 LVGL、相机、音频驱动或 NVS。云端角色、语言音色 API 位于 SDK 的 `biz-client` 模块。

## 工程接入

在工程顶层 `project()` 之前，将本目录加入 `EXTRA_COMPONENT_DIRS`，并继续包含仓库提供的 `examples/esp-idf/components`。应用组件声明 `REQUIRES agentic_kit role-management`。若复制源码到工程 `components/role_management`，则依赖名改为 `role_management`。

联网、激活并完成校时后，由应用管理任务初始化：

```c
#include "role_management.h"

static void on_role_done(role_demo_operation_t operation, int rc,
                         bool change_accepted,
                         const iot_atop_rejection_t *rejection, void *user_data)
{
    /* 这是角色 worker 回调。将结果按值投给 UI，不在这里调用 stop。
     * change_accepted=true 且 rc!=0 表示写入成功、回读失败，不能直接重试创建。 */
}

if (!role_management_start(iot, on_role_done, NULL)) {
    /* 任务、队列或业务客户端初始化失败。 */
}
```

可在工程的 `agentic_kit_config.h` 中调整 `AGENTIC_KIT_RESPONSE_BUFFER_SIZE`，或把编译定义加到编译 SDK 的组件目标。模板列表可能超过默认 4096 字节的 HTTP/密文共用缓冲；例如从 32768 开始结合实际数据测量。

## 投递操作

UI/按键回调只构造按值消息并调用非阻塞 `role_management_submit()`。返回 false 表示未启动、消息无效或队列满，显示忙碌/失败，不要直接提示操作成功。

```c
role_demo_job_t job = {
    .operation = ROLE_DEMO_LOAD, .page_no = 1, .page_size = 5,
};
bool accepted = role_management_submit(&job);
```

按查询日志/客户 UI 获取的真实 ID，构造绑定或编辑消息：

```c
role_demo_job_t job = {
    .operation = ROLE_DEMO_EDIT,
    .type = TUYA_AI_ROLE_TEMPLATE,
    .role_id = "ROLE_ID_FROM_CLOUD",
    .set_name = true, .name = "学习伙伴",
    .set_speed = true, .speed = 1.20,
};
bool accepted = role_management_submit(&job);
```

支持 `ROLE_DEMO_LOAD`、`ROLE_DEMO_BIND`、`ROLE_DEMO_EDIT`、`ROLE_DEMO_DELETE`、`ROLE_DEMO_LANGUAGES`、`ROLE_DEMO_TIMBRES`。音色查询用 `lang` 和正数 `page_no/page_size`；保留完整语言编码。删除只接受自定义类型，并拒绝删除当前绑定角色。

worker 打印当前页列表与变更后绑定，完成回调提供返回码和云端拒绝原因。若需要向屏幕展示列表，应在消费列表、释放之前复制/转交数据，遵守 UI 的线程与锁约定。SDK 不替应用绘制页面。

## 生命周期与验收

示例为单实例，4 条队列消息和一个 16 KB 栈 worker；实际栈和内存需测量高水位。队列消息内所有字符串均按值复制，不保留 UI 临时指针。

设备解绑或销毁 IoT Client 前，先停止 UI/按键生产者，再由应用管理任务调用 `role_management_stop()`。它会等待队列及正在执行的 HTTPS 完成，释放业务客户端，最后才可销毁 IoT Client；不能从本 worker 或其完成回调调用 stop，也不能与 submit 并发竞态。

验收角色查询、绑定、两类编辑语义、删除、语言音色筛选，以及断网/队列满/退出。绑定成功后，用新一轮真实对话确认语言、音色和角色行为；SDK 不自动重建 TAI Session。
