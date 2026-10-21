# AI 角色管理命令行示例

使用 `biz-client` 的角色、语言和音色 API，适用于 macOS/Linux。无需声卡、屏幕或 RTC 库。默认只显示帮助，只有显式命令才请求云端；编辑、绑定和删除会修改指定设备的真实云端角色。设备必须已正常激活，系统时间正确。

在 SDK 根目录单独编译此示例，避免加载其他示例的 QR/图像依赖：

```sh
cmake -S examples/posix/ai/role-management -B build-role \
  -DPython3_EXECUTABLE="$(command -v python3)" \
  -DCMAKE_C_FLAGS=-DAGENTIC_KIT_RESPONSE_BUFFER_SIZE=32768
cmake --build build-role --target role_management_demo -j 4
./build-role/role_management_demo --help
```

Python 环境需要 SDK 构建依赖 `jsonschema`、`jinja2`。32768 是示例响应缓冲配置，不是所有产品的最低要求。模板列表不能分页时，应以真实最大响应量和内存余量调整；编译定义必须作用于 SDK 本身。

从安全的本地凭证加载方式设置 `TUYA_DEVICE_ID`、`TUYA_SECRET_KEY`、`TUYA_LOCAL_KEY` 和 `TUYA_CA_FILE`。凭证来自该设备激活结果，CA 文件为其区域云端的可信 PEM；可选 `TUYA_REGION` 为 AY/AZ/UE/EU/WE/IN/SG，默认 AY。不要把密钥写进源码或命令行参数。该工具设置 `skip_version_report=true`，不覆盖真实设备的固件版本记录，也不建立 MQTT。

```sh
./build-role/role_management_demo bound
./build-role/role_management_demo templates
./build-role/role_management_demo custom 1 5
./build-role/role_management_demo languages
./build-role/role_management_demo timbres zh_ha 1 5
```

从查询结果选择真实 ID 后，按需执行下面的变更命令。`ROLE_ID`、`CUSTOM_ID`、`VOICE_ID` 为占位符：

```sh
./build-role/role_management_demo bind template ROLE_ID
./build-role/role_management_demo edit template ROLE_ID 'name=学习伙伴' speed=1.20
./build-role/role_management_demo edit custom CUSTOM_ID lang=zh_ha timbre=VOICE_ID
./build-role/role_management_demo bind custom CUSTOM_ID
./build-role/role_management_demo delete custom CUSTOM_ID --confirm
```

模板编辑创建并请求绑定新自定义角色，日志会输出新 ID；自定义编辑只原地更新，不自动绑定。删除命令先回读当前绑定并拒绝删除当前角色，需先切换到其他角色。没有 `--confirm` 不执行删除。

成功日志包括列表、`saved role` 或回读的 `bound`。若云端已接受变更但回读失败，日志会单独指出，退出码非零；先查询，再决定是否重试写入。接口回读不等于 AI 实际生效，需要在设备上发起新一轮真实语音验证。

该示例不自动创建角色缓存、下载头像或重建 AI 会话。API 和线程/内存约束见文档站的 AI 角色管理指南和 Business Client 参考。
