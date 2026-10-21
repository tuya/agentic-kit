# Tuya LAN

`tuya-lan` 为已激活设备提供 Tuya LAN 3.5 本地控制：UDP 发现、TCP 会话、三步鉴权、
心跳、DP 查询、本地 DP 控制和显式状态上报。应用调用 `tuya_lan_process()` 驱动服务，
并通过回调复用 IoT Client 的 schema、DP state 和业务处理。

LAN 默认不参与构建；主机通过 `AGENTIC_KIT_ENABLE_LAN`，ESP-IDF 通过
`CONFIG_AGENTIC_KIT_ENABLE_LAN` 开启。LAN 与 MQTT 连接独立；P2P 信令可经扩展命令
传递，P2P 协商与媒体传输由其他模块负责。

## 文档入口

- [架构与功能说明](../../docs-site/docs/guides/tuya-lan-architecture.md)：模块关系、功能边界、数据流和集成方式。
- [接口与集成参考](../../docs-site/docs/reference/tuya-lan.md)：构建开关、配置、回调、生命周期和验证命令。
- [领域模型](CONTEXT.md)：LAN 术语与实现不变量。
- [架构决策：应用驱动并共享 DP 状态](docs/adr/0001-lan-is-application-driven-and-shares-dp-state.md)。
- 公共接口：[tuya_lan.h](include/tuya_lan.h) 与统一平台接口 [pal.h](../../pal/pal.h)。
- [POSIX 示例](../../examples/posix/lan/lan_demo.c)：使用离线 DP registry 运行本地控制服务。
