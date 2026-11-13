# Tuya_BLE_Communication_Protocol（V6.0）



> 本地存档，供离线分析研究使用。内容逐块抓取自飞书文档，为抓取时刻的快照。

>

> - 来源: https://icn602w9tnqf.feishu.cn/wiki/VHHzwC22IiOotak7IFZcDFX0n3e （docx obj: QZNodHN4joP6eoxyKcIc34F4njh）

> - 抓取日期: 2026-11-10 · 块数: 12486（正文 7088 / 表格 291 / 标题 170）

> - 说明: 表格由单元格坐标重建；图片为外链（可能过期）；排版为近似还原，原文以飞书为准。



---

修改记录：

| 版本 | 修订说明 | 修订人 | 修订日期 | 备注 |
|---|---|---|---|---|
| V4.0 | 新建 | 高永会 | 2020/05/17 | 1.<br>重定义广播数据格式，可发送dp点数据<br>2.<br>使用的新的service uuid。<br>3.<br>增加MTU交换机制。<br>4.<br>增加高级加密认证流程和相关指令。<br>5.<br>新增dp点数据下发指令0x0027（单dp点数据长度变为2字节）。<br>6.<br>新增dp点数据上报指令0x8006（单dp点数据长度变为2字节）。<br>7.<br>新增带时间戳dp点数据上报指令0x8007（单dp点数据长度变为2字节）。 |
| V4.1 | 增加和修改 | 高永会 | 2020/12/11 | 1.<br>获取设备信息指令响应帧新增是否需要beaconKey标志。<br>2.<br>配对请求指令新增beaconKey。<br>3.<br>广播包新增设备在线逻辑标志。<br>4.<br>优化读取大数据摘要信息指令。<br>5.<br>8006和8007指令新增是否需要响应帧标志。<br>6.<br>新增设备请求云端实时时间3指令。<br>7.<br>新增设备请求天气数据指令。<br>8.<br>新增下发天气数据指令。<br>9.<br>新增语音传输相关指令。 |
| V4.2 | 增加和修改 | 高永会<br>徐宏 | 2021/03/24 | 1.<br>新增Plug&play相关指令。<br>2.<br>新增zigbee配网相关指令。<br>3.<br>新增Read Characteristic UUID。<br>4.<br>设备信息响应指令新增蓝牙mac地址。<br>5.<br>新增设备能力上报字段。<br>6.<br>新增OTA时dp可控能力上报。<br>7.<br>新增低功耗在线逻辑能力上报。<br>8.<br>更新天气服务参数表。<br>9.<br>新增设备请求天气数据-指定位置指令。<br>10.<br>新增下发天气数据-指定位置指令。<br>11.<br>扫描响应包内容新增WIFI/BLE设备配网优先级标志。<br>12.<br>设备信息响应指令新增WIFI/BLE设备配网优先级标志。<br>13.<br>设备信息响应指令设备能力新增beacon能力标志。<br>14.<br>设备信息响应指令设备能力新增LINK层加密标志。 |
| V4.3 | 增加 |  | 2021/07/01 | 1.<br>设备信息响应指令新增附件通道号及对应版本号信息。<br>2.<br>新增场景请求下发指令。<br>3.<br>优化语音传输指令，新增参数和分包机制。<br>4.<br>新增大数据请求同步指令。 |
| V4.4-beta1 | 增加和修改 |  | 2021/07/06 | 1.<br>设备信息响应指令新增packetMaxSize字段用于应用层分包。<br>2.<br>新增定时能力标志。<br>3.<br>新增语音识别天气、日历、toDOlist数据指令。<br>4.<br>新增语音定时、闹钟、提醒设置取消指令。<br>5.<br>新增语音授权token设置、读取、上报、下发指令。<br>6.<br>新增支持分包的上下行通道。<br>7.<br>新增BLE + X复合通信协议指令。 |
| V4.4-beta2 | 增加和修改 |  | 2021/08/13 | 1.<br>扫描响应包加密方式新增用于扫码配网的加密标志。<br>2.<br>设备信息响应指令设备能力新增BT/BLE双模标志。<br>3.<br>新增0x0060查询BT设备信息指令。<br>4.<br>语音识别天气新增TTS和天气描述字符串。<br>5.<br>语音通用指令新增设置通知状态子指令。<br>6.<br>新增文件下发相关指令。 |
| V4.4-beta3 | 增加和修改 |  | 2021/09/27 | 1.<br>设备信息响应指令中的设备能力新增是否需要强制LINK层加密标志。<br>2.<br>设备信息响应指令新增Flag2字段，且其中bit0定义为当前蓝牙连接是否已LINK层加密标志。<br>3.<br>avs start speech响应指令的status新增无网络、无授权、网络异常错误码。<br>4.<br>新增0x801D主动上报连接状态指令。<br>5.<br>0x0031指令新增audio format 。 |
| V4.4-beta4 | 增加和修改 |  | 2021/11/22 | 1、新增06/07/08加密方式，对应扫描响应包中的ID字段重定义，新增MAC地址。 |
| V4.4-beta5 | 增加和修改 |  | 2022/03/04 | 1、ble+x的0x0050指令新增TLD，包括被动查询/主动上报 |
| V4.4-<br>beta6 | 增加和修改 | 高永会 | 2022/03/23 | 1、设备信息响应指令新增sl字段，用于wifi设备。 |
| V4.4 |  | 高永会 | 2022/08/02 | 1、V4.4正式版。 |
| V4.5-beta1 | 增加和修改 | 曾云 | 2022/08/02 | 1.<br>查询设备信息中增加配件设备能力控制。<br>2.<br>发起配件请求指令中新增数据源字段。<br>3.<br>新增带数据源DP点数据下发指令（0x0028）。<br>4.<br>新增带数据源DP点数据查询指令（0x0029）<br>5.<br>新增带数据源DP点数据上报指令（0x8008）。<br>6.<br>新增带数据源和时间戳DP点数据上报（0x8009）。<br>7.<br>新增配件OTA升级相关指令。<br>8.<br>新增APP读取配件连接信息指令（0x8020）。<br>9.<br>新增设备上报配件连接信息指令（0x8021）<br>10.<br>新增APP下发配件激活信息指令（0x8022）<br>11.<br>设备信息响应指令新增combosflag字段。<br>12.<br>新增801E、801F Combo Wireless Modules透传通道。 |
| V4.5-beta2 | 修订错误 | 曾云 | 2022/08/08 | 1、修订查询设备信息（0x0000）中“设备能力”字段说明错误，bit8为psk，bit9为挂载配件。 |
| V4.5-beta3 | 修改内容 | 曾云 | 2022/09/13 | 1、0x8021指令新增加密认证字段。<br>2、0x0001指令SRC_TYPE字段新增门锁配件类型值。<br>3、0x0000 指令能力值新增差分OTA支持标位。 |
| V4.5-beta4 | 修改内容 | 曾云 | 2022/09/20 | 1、0x0001指令SRC_TYPE字段区分APP类型。 |
| V4.5 | 正式版 | 曾云 | 2022/10/08 | 1、V4.5正式版。 |
| V4.6-beta1 | 增加内容 | 苏钉 | 2022/12/13 | 1、新增Gateway Connect Mode、Connectivity和设备在线逻辑 |
| V4.6-beta2 | 增加内容 | 苏钉 | 2023/01/05 | 1、指令801B添加subCmd |
| V4.6-beta3 | 增加内容 | 苏钉 | 2023/02/02 | 1.<br>添加BLE Beacon遥控器代理认证指令<br>2.<br>添加BLE Beacon遥控器群组订阅指令<br>3.<br>添加BLE Beacon遥控器群组移除指令<br>4.<br>添加BLE Beacon遥控器群组查询指令 |
| V4.6 | 正式版 | 苏钉 | 2023/02/24 | 1、V4.6正式版。 |
| V4.7-beta1 | 增加内容 | 成梦军 | 2023/02/28 | 1.<br>新增一般加密V2版本<br>2.<br>调整广播字段标识，新增支持一般加密V2协议bit与新增已执行V2协议bit。<br>3.<br>新增加密安全算法。<br>4.<br>新增移除配网针对V2协议的改动说明。<br>5.<br>新增密钥16来对设备进行异常解绑。<br>6.<br>新增针对一般加密V2协议数据有效性校验的4字节Verify Key（0001指令）。 |
| V4.7-beta2 | 增加和修改 | 应世友 | 2023/05/31 | 1.<br>0x0001指令新增主从一体的设备能力 bit15<br>2.<br>0x0001指令SRC_TYPE字段新增“主从一体主设备”类型值；<br>3.<br>0x0028、0x0029、0x8008、0x8009指令SRC_TYPE字段更名为DP_SRC_TYPE，并新增“主从一体从设备”类型值； |
| V4.7-beta3 | 增加内容 | 应世友 | 2023/06/14 | 1.<br>0x0028、0x0029、0x0080、0x0081、0x0082、0x0083、0x0084、0x8008、0x8009指令 ID_TYPE 字段新增“主从一体从设备UUID”类型值； |
| V4.7-beta4 | 增加和修改 | 曾云 | 2024/03/20 | 1、0x0000指令设备能力字段新增单区升级能力-bit17和wifi局域网通信能力-bit18；Flag2字段新增是否从BOOT区启动标位；<br>2、新增0x0011 OTA升级准备通知指令；<br>3、新增GPT语音传输指令。 |
| V4.7 | 正式版 |  | 2024/05/29 | 1、V4.7正式版。 |
| V4.8-beta1 | 增加内容 | 高永会 | 2024/05/29 | 1.<br>协议版本升级v4.8。<br>2.<br>0x0000指令设备能力字段新增dp收发通道能力标志。<br>3.<br>0x0001指令新增app flag标志字段。 |
| V4.8-beta2 | 增加内容 | 高永会 | 2024/07/11 | 1、扫描响应包通信能力字段新增bit10 双模设备beacon通信能力标志。 |
| V4.8-beta3 | 更改 | 苏钉 | 2024/07/26 | 1、扫描响应包通信能力字段bit10双模设备beacon通信能力标志移到ID字段FLAG2的bit2，通信能力字段的bit10回收。 |
| V4.8-beta4 | 增加内容 | 苏钉 | 2024/08/12 | 1、新增0x0090~0x0093命令（0x0090~0x0092命令之前在5.0版本中已经存在）。 |
| V4.8-beta5 | 更改 | 苏钉 | 2024/08/26 | 1、扫描响应包Flag字段的bit6新增定义，ID字段Flag2的bit2回收。 |
| V4.8-beta6 | 增加内容 | 苏钉 | 2024/09/02 | 1、0x0000指令中设备能力字段新增bit20、bit21；<br>2、0x0001指令中新增Node_id；<br>3、业务透传通道新增sub_cmd=0x9001，用于BLE群组代理配网； |
| V4.8-beta7 | 增加内容 | 苏钉 | 2024/09/23 | 1、0x0001指令中新增ble_net_key； |
| V4.8-beta8 | 增加内容 | 苏钉 | 2024/09/29 | 1、0x0001指令中新增node_id_len； |
| V4.8-beta9 | 增加内容 | 高永会 | 2024/10/10 | 1、OTA升级指令0x000C响应指令新增flag2及签名信息字段。<br>2、新增0x0018 下发固件签名指令。<br>3、新增0x0019 更新验签公钥指令。 |
| V4.8-beta10 | 增加内容 | 曾云 | 2024/11/19 | 1、扫描响应包-通信能力-bit10 新增定义，表示是否具有BLE long range通信能力。 |
| V4.8-beta11 | 增加内容 | 高永会 | 2025/10/13 | 1、新增NFC+BLE beacon能力标位。<br>2、新增comboflag静态ip配网能力相关标位。 |
| V4.8-beta12 | 增加内容 | 徐宏 | 2025/12/15 | 1、0x0000指令flag2新增bit4标识，用于标识设备是否支持离线解绑v2能力（是否清除数据），作用域PV>=4.8<br>2、新增0x002A 离线解绑v2指令，告知设备是否清除数据<br>3、业务透传通道新增sub_cmd=0x8004，用于AI眼镜 |
| V4.8-beta13 | 增加内容 | 徐宏 | 2026/1/28 | 1、0x0000指令flag2新增bit5标识，用于标识Hey Tuya能力，作用域PV>=4.8<br>2、新增0x0094 用于设备与app基于Hey Tuya语音模式控制信令 |
| V4.8-beta14 | 增加内容 | 徐宏 | 2026/3/11 | 1、增加7.1.1 spp链路传输层协议 |
|  |  |  |  |  |
| V6.0-beta.1 | 增加和重构 | 高永会<br>徐宏 | 2026/8/28 | 协议版本 6<br>1、新增 V6 安全层章节：安全模型概览、密钥体系、数据部分、<br>   通信流程与描述、报文接收校验规则<br>2、新增 0x1000 安全握手、0x1001 查询设备信息（V6）、0x1002 配对请求（V6）<br>3、广播格式与安全层按协议版本分段，文档结构重组，原协议归档进「≤V5」章节 |
| V6.0-beta.2 | 更改 | 高永会<br>徐宏 | 2026/9/3 | 协议版本 6<br>1.<br>AD Length 勘误：V6 0x18/0x0E，V5/≤V4 MAC 形态 0x0D→0x0E 历史勘误）<br>2.<br>0x1001指令增加安全版本号 + SDK版本字符串<br>3.<br>0x1002指令的verify_key挪到最后<br>4.<br>功能码 摘要算法更换(MD5 -->SHA-256）<br>◦<br>0x000D OTA 文件信息<br>◦<br>0x0070 发送文件信息<br>◦<br>0x0081 配件 OTA 文件信息<br>◦<br>0x0030~0x0037 AVS语音类指令，修改VERSION与token摘要描述与定义<br>◦<br>响应status增加摘要类型错误/不支持<br>5.<br>功能码 算法变更<br>◦<br>0x8021 设备上报配件连接信息；扩展ENC_TYPE值以及新算法定<br>6.<br>群组 beacon协议，不在本协议范畴。随mesh安全更新一起做算法更新 |
| V6.0-beta.3 | 更改 | 高永会<br>徐宏 | 2026/9/4 | v1老设备升级后，请求密钥更新标识位定义<br>1.<br>Frame Control字段，bit3定义为Request_keys_update<br>2.<br>0x1001指令 Flag2的bit1定义为Request_keys_update |
| V6.0-beta.4 | 更改 | 高永会<br>徐宏 | 2026/9/17 | 1.<br>0x1001指令安全版本号，2位改3位（与Wi-Fi保持一致）<br>2.<br>AES-CCM-128加解密算法MIC(/TAG)长度变更，从8字节改为16字节 |

---

# 1. 范围与约定

## 1.1 范围

本协议由涂鸦-嵌入式部门-BLE 组编制，定义涂鸦蓝牙设备与手机 App、云端之间的通信协议，内容包括蓝牙广播格式、GATT 服务接口、分包与帧格式、安全层（密钥体系、加密与认证、握手流程）以及各功能码的数据格式。

本文档同时定义两代安全层：V6 与 ≤V5。设备的协议版本由广播中的协议版本字段标识，读者按目标设备的协议版本阅读对应章节，两代不得混用（见「协议版本判定」）。

## 1.2 约定

- • 字节序：本协议定义的数据项默认使用 Big-Endian 编码，即多个字节表示一个数据项时先发送高字节。但以下两类字段除外： ◦ 由蓝牙规范定义的字段，按 Bluetooth Core Spec / Assigned Numbers自身规定的字节序（小端）。例如： ▪ Service UUID  0xFD50 —— 空口顺序 50 FD（AD Type 0x02 与 0x16）    ▪ Company Identifier 0x07D0 —— 空口顺序 D0 07 ◦ 明确标注了字节序的字段，按标注来。例如 0x0001 中的 ble_mac_addr 标注为小端。

- ◦ 由蓝牙规范定义的字段，按 Bluetooth Core Spec / Assigned Numbers自身规定的字节序（小端）。例如： ▪ Service UUID  0xFD50 —— 空口顺序 50 FD（AD Type 0x02 与 0x16）    ▪ Company Identifier 0x07D0 —— 空口顺序 D0 07

- • 位编码方式：bit0 为最低位。跨多字节的位域按该数据项的数值编号，与传输顺序无关：2 字节字段的 bit15 位于先传输字节的最高位，bit0 位于后传输字节的最低位。

- ▪ Company Identifier 0x07D0 —— 空口顺序 D0 07

- ◦ 明确标注了字节序的字段，按标注来。例如 0x0001 中的 ble_mac_addr 标注为小端。

- • 记号约定：‖ 表示字节串拼接；X[n] 表示字段 X 长度为 n 字节；L(x) 表示 x 的长度，以 1 字节表示。

- • 位编码方式：bit0 为最低位。跨多字节的位域按该数据项的数值编号，与传输顺序无关：2 字节字段的 bit15 位于先传输字节的最高位，bit0 位于后传输字节的最低位。

- • 「协议版本 ≥6 的连接使用各指令的 V6 版 Data 格式，≤V5 沿用原格式；V6 设备收到旧版格式按解析失败处理」

•
字段标注约定：M 必备 / O 可选。

### • 记号约定：‖ 表示字节串拼接；X[n] 表示字段 X 长度为 n 字节；L(x) 表示 x 的长度，以 1 字节表示。

### • 要求强度：「必须」/「不得」为强制要求，不满足即不符合本协议；「应」为推荐做法；「可」为允许的可选实现。

| 1、0x0001指令SRC_TYPE字段区分APP类型。 | 全称 | 说明 |
|---|---|---|
| 2023/01/05 | Additional Authenticated Data | 1、新增0x0090~0x0093命令（0x0090~0x0092命令之前在5.0版本中已经存在）。 |
| AD | Advertising Data | 2.<br>设备信息响应指令新增Flag2字段，且其中bit0定义为当前蓝牙连接是否已LINK层加密标志。 |
| 4.<br>新增0x801D主动上报连接状态指令。 | Attribute Protocol | 蓝牙属性协议。ATT MTU 决定单次写操作的最大字节数，见 4.2 |
| 1.<br>0x0001指令新增主从一体的设备能力 bit15 | 3.<br>0x0028、0x0029、0x8008、0x8009指令SRC_TYPE字段更名为DP_SRC_TYPE，并新增“主从一体从设备”类型值； | 低功耗蓝牙 |
| CCM | Counter with CBC-MAC | AES 的认证加密模式，见 NIST SP 800-38C |
| 6.<br>新增带数据源和时间戳DP点数据上报（0x8009）。 | 8.<br>新增APP读取配件连接信息指令（0x8020）。 | 10.<br>新增APP下发配件激活信息指令（0x8022） |
| 12.<br>新增801E、801F Combo Wireless Modules透传通道。 | Data Point | 涂鸦数据点，设备功能的最小数据单元 |
| 2.<br>调整广播字段标识，新增支持一般加密V2协议bit与新增已执行V2协议bit。 | Deterministic Random Bit Generator | 6.<br>新增针对一般加密V2协议数据有效性校验的4字节Verify Key（0001指令）。 |
| ECDH | Elliptic Curve Diffie-Hellman | 椭圆曲线密钥协商，本协议使用 P-256，见 6.2.3 |
| V4.8-beta2 | Generic Attribute Profile | 3.<br>0x0001指令新增app flag标志字段。 |
| HKDF | HMAC-based Key Derivation Function | 密钥派生函数，见 RFC 5869 与 6.2.7 |
| HMAC | Hash-based Message Authentication Code | 基于散列的消息认证码 |
| IKM | 1.<br>0x1001指令安全版本号，2位改3位（与Wi-Fi保持一致） | HKDF 的输入密钥材料 |
| MIC | Message Integrity Check | 消息完整性校验值，即 CCM 的认证标签（Tag） |
| 4.<br>设备信息响应指令新增蓝牙mac地址。 | Maximum Transmission Unit | 8.<br>更新天气服务参数表。 |
| 10.<br>新增下发天气数据-指定位置指令。 | 12.<br>设备信息响应指令新增WIFI/BLE设备配网优先级标志。 | 14.<br>设备信息响应指令设备能力新增LINK层加密标志。 |
| PID | Product ID | 产品标识符，即 product_id |
| 2.<br>0x1001指令增加安全版本号 + SDK版本字符串 | 4.<br>功能码 摘要算法更换(MD5 -->SHA-256）<br>◦<br>0x000D OTA 文件信息<br>◦<br>0x0070 发送文件信息<br>◦<br>0x0081 配件 OTA 文件信息<br>◦<br>0x0030~0x0037 AVS语音类指令，修改VERSION与token摘要描述与定义<br>◦<br>响应status增加摘要类型错误/不支持 | 保留字段/保留位，发送侧置 0、接收侧校验，见 1.2 |
| ◦<br>0x0030~0x0037 AVS语音类指令，修改VERSION与token摘要描述与定义 | 5.<br>功能码 算法变更<br>◦<br>0x8021 设备上报配件连接信息；扩展ENC_TYPE值以及新算法定 | 6.<br>群组 beacon协议，不在本协议范畴。随mesh安全更新一起做算法更新 |
| TRNG | True Random Number Generator | 真随机数发生器，见 6.2.4 |

## 2.2 标识符

| 1.<br>设备信息响应指令新增附件通道号及对应版本号信息。 | 3.<br>优化语音传输指令，新增参数和分包机制。 | 2021/07/01 | 说明 |
|---|---|---|---|
| 1.<br>0x0028、0x0029、0x0080、0x0081、0x0082、0x0083、0x0084、0x8008、0x8009指令 ID_TYPE 字段新增“主从一体从设备UUID”类型值； | 8 或 16 | 通用 | 产品标识符，同型号产品共用。文中亦记作 PID / PRODUCT_KEY |
| device_uuid | 1、0x8021指令新增加密认证字段。 | 3、0x0000 指令能力值新增差分OTA支持标位。 | 设备标识符，与 auth_key 一一对应，由涂鸦产线分配 |
| device_id | 变长，见 0x1002 | V4.8-beta3 | 云端分配的设备身份标识，非加密用途 |
| node_id | 变长，见 0x1002 | 通用 | 1、0x0001指令中新增ble_net_key； |
| ble_net_key | 变长，见 0x1002 | 通用 | 组网密钥，非加密用途 |
| ble_mac_addr |  | 通用 | 蓝牙 MAC 地址，小端格式（见 1.2 字节序例外） |

## 2.3 密钥与密钥材料

| V4.8-beta5 | 1、0x0000指令设备能力字段新增单区升级能力-bit17和wifi局域网通信能力-bit18；Flag2字段新增是否从BOOT区启动标位； | 3、新增GPT语音传输指令。 | 说明 |
|---|---|---|---|
| auth_key | 2、0x0001指令中新增Node_id； | 2024/09/02 | 信任根。产线预置，设备与云端各一份，永不经蓝牙传输 |
| 1、设备信息响应指令新增sl字段，用于wifi设备。 | 6 or 16 | 16是从V4 安全版本起 | 云端签发的绑定密钥。在 0x1002 / 0x0001 中按「长度 + 值」编码传输 |
| secret_key | 1.<br>添加BLE Beacon遥控器代理认证指令 | 3.<br>添加BLE Beacon遥控器群组移除指令 | 云端签发的绑定密钥，与 login_key 配对使用 |
| beacon_key | 见 0x1002 | Counter with CBC-MAC | 广播 DP 数据加密的派生源（Encrypted Type = 2）, 是家庭维度的秘钥 |
| Additional Authenticated Data | 消息完整性校验值，即 CCM 的认证标签（Tag） | V6 | 中心侧 / 设备侧每次连接生成的随机数，经 0x1000 交换 |
| Serial Port Profile | 16 | Message Integrity Check | AES-CCM 的附加认证数据，参与认证但不加密，见 6.3.3 |
| 产品标识符，即 product_id | Bluetooth Low Energy | 广播数据。AD Structure 为其基本单元，AD Type 标识内容类型，见 3.1 | 由 auth_key 与 serverRandom 派生，配网握手的认证值 |
| 密钥派生函数，见 RFC 5869 与 6.2.7 | 低功耗蓝牙 | V6 | 离线解绑授权值，绑定密钥换代即失效 |
| Attribute Protocol | 椭圆曲线密钥协商，本协议使用 P-256，见 6.2.3 | V6 | ECDH 结果点的 x 坐标 |
| transcript_hash | Deterministic Random Bit Generator | V6 | SHA-256( 0x1000 请求 Data ‖ 0x1000 响应 Data )，参与全部会话密钥派生 |
| key21 / 22 / 24 / 25 / 26 | True Random Number Generator | 涂鸦数据点，设备功能的最小数据单元 | 会话密钥，HKDF 输出 24 字节切分为 key[16] ‖ nonce_iv[8] |
| nonce_iv | 产品标识符，同型号产品共用。文中亦记作 PID / PRODUCT_KEY | ble_net_key | 与会话密钥同源派生，参与 CCM Nonce 构造 |
| RegisterKey | ble_mac_addr | 云端分配的设备身份标识，非加密用途 | HMAC-SHA-256_auth_key( serverRandom )，设备向云端证明持有 auth_key |
| verify_key（V6） | node_id | V6 | HMAC-SHA-256_auth_key( L(sRandom) \|\| 0x1002 全部云端签发字段 )，设备校验云端合法性 |
| Verify_key（≤V5） | 8 或 16 | ≤V5 | MD5( login_key + secret_key + auth_key ) 前 4 字节，V2 协议使能校验 |
| session_key | Verify_key（≤V5） | ≤V5 | MD5( login_key, dev_rand )，≤V5 的传输密钥 |
| secret_key_1…16 | 16 | RegisterKey | ≤V5 的各代密钥，生成方式见 7.1 |
| dev_rand | 6 | ≤V5 | 云端生成，用于计算 secret_key_1 / 11 / 16 |
| server_rand | 见 7.1 | 离线解绑授权值，绑定密钥换代即失效 | 云端生成，用于计算 secret_key_1 / 11 / 16 |
| user_rand | ecdh_shared | 16是从V4 安全版本起 | 用户随机数，用于计算 secret_key_3 |

## 2.4 证书体系（≤PV5 高级加密专用）

| 名称 | HMAC-SHA-256_auth_key( serverRandom )，设备向云端证明持有 auth_key | ≤V5 的各代密钥，生成方式见 7.1 |
|---|---|---|
| Tuya CA Certificate | server_rand | 涂鸦 CA 证书 |
| Tuya CA Public Key | 64 | HMAC-SHA-256_auth_key( L(sRandom) \|\| 0x1002 全部云端签发字段 )，设备校验云端合法性 |
| Tuya CA Private Key | auth_key | 涂鸦 CA 私钥，用于签名 server 证书和 device 证书 |
| Device Certificate | — | 设备证书，由涂鸦 CA 签发 |
| Device Public Key | 64 | 设备端证书公钥 |
| Device Private Key | 32 | key21 / 22 / 24 / 25 / 26 |
| Server Certificate | — | 云端证书，由涂鸦 CA 签发 |
| Server Public Key | 64 | 云端证书公钥 |
| Server Private Key | 32 | 云端证书私钥 |

说明

# 3. 蓝牙广播格式

### Additional Authenticated Data

本节依据 Bluetooth Core Spec，广播包与扫描响应包共用此编码；各 AD Type 所承载的内容见后续各节。

![Feishu Docs - Image](blob:https://icn602w9tnqf.feishu.cn/b54770f8-98f0-43de-8955-f119cf79774f)

单个 AD Structure组成结构

| 字段 | 云端证书，由涂鸦 CA 签发 | 设备端证书私钥 |
|---|---|---|
| Length | Server Private Key | 后续字节数，计入 AD Type，不计入 Length 自身 |
| AD Type | 1 | 内容类型，取值见 Bluetooth Assigned Numbers |
| AD Data | Length − 1 | 后续字节数，计入 AD Type，不计入 Length 自身 |

- • 一个广播包（AdvData）或扫描响应包（ScanRspData）由若干 AD Structure 顺序拼接，总长上限 31 字节；解析方按 Length 逐段跳转，遇到不认识的 AD Type 应跳过而非中止。

- • AD Data 中由蓝牙规范定义的字段按小端传输，如 16-bit Service UUID 0xFD50 空口顺序为 50 FD（见 1.2 字节序例外）。

- • 本协议使用的 AD Type： ◦ 0x01 Flags ◦ 0x02 Service UUID（仅 ≤V5） ◦ 0x16 Service Data ◦ 0xFF Manufacturer Specific Data ◦ 0x09 Complete Local Name

## 3.2 协议版本判定

适用对象：扫描方（App / 网关 / 其他中心设备）。设备端只广播自身固件对应的版本，不需要判定。

判定字段：广播包 Service Data（UUID 0xFD50）中 DATA 的前 2 字节 Frame Control，其 bit12–bit15 为 Version。该字段在所有协议版本中的位置与宽度均一致，因此可作为版本判定的共同锚点。

Frame Control 为 2 字节大端。以 FC[0] 表示先传输的字节、FC[1] 表示后传输的字节：

Code block
C
Version = (FC[0] >> 4) & 0x0F

AES 的认证加密模式，见 NIST SP 800-38C

| Version | 含义 | 按哪一节解析 |
|---|---|---|
| 6 | V6协议 | 3.3 V6 广播格式 |
| 基本无存量设备 | 3.4 V5 广播格式 | 3.4 V5 广播格式 |
| 存量设备 | 3.5 ≤V4 广播格式 | 3.5 ≤V4 广播格式 |
| 高于本端支持的版本 | 不予解析，忽略该设备 | 不予解析，忽略该设备 |

### Coded Physical Layer

### BLE long range 物理层

| 广播数据段描述 | 类型 | 说明 |
|---|---|---|
| 设备 LE 物理连接标识 | 0x01 | 长度：0x02   <br>类型：0x01   <br>数据：0x06 |
| Service Data | 0x16 | 长度：LEN（可变,最大 24+3 字节）<br>类型：0x16  <br>UUID：0xFD50<br>DATA：格式见下表 |

DATA格式如下所示（DATA长度最大24字节）：

| 序号 | 长度 | 字段 | 必备(M) / 可选(O) | 说明 |
|---|---|---|---|---|
| 1 | 2 | Frame Control | M | 控制位，大端格式 |
| 2 | 1 | Frame Control  Ext | M | 扩展控制位，大端格式 |
| 3 | 9 or 17 | ID | O | Product id或者Product key |
| 4 | 1 | DP Control | O | DP点数据控制位 |
| 5 | 4 | Frame Counter | O | 帧序号用于防重放，大端格式 |
| 6 | 不加密最大16<br>加密最大12 | DP Data | O | DTLD |
| 7 | 4 | MIC | O | Message Integrity Check，如果dp data加密则为必选字段，大端格式 |

Frame Control：

| Bit | 字段 | 说明 |
|---|---|---|
| 2-0 | Reserved | 保留 |
| 3 | Request_keys_update | 请求long-term keys更新（默认0表示不请求，1表示请求）<br>适用于老设备（v1版)请求更新密钥（比如v1老设备 升级后，设备控制该bit=1请求请求更新密钥，当重连更新后，设备控制该bit=0） |
| 4 | Gateway Connect Mode | 0-短连接（按需连接）；1-长连接，用于网关 |
| 5-6 | Connectivity | 0-同时支持手机和网关；1-只支持网关；2-只支持手机 |
| 7 | Dp Data Include | 是否包含dp data，如果为1需同时包含DP Control 和Frame Counter，如果加密还需要包含MIC字段。 |
| 8 | ID Include | 0-不包含ID字段；1-包含ID字段 |
| 9 | Request Connection | 1-请求连接，0-未请求连接。 |
| 10 | Shared Flag | 共享类标志，1-涂鸦共享类设备，0-非共享类设备。 |
| 11 | Bound Flag | 绑定标志，0-未绑定，1-已绑定。 |
| 12-15 | Version | 协议版本号，当前为6。 |

确定性随机比特发生器，见 6.2.4

| Bit | 字段 | 说明 |
|---|---|---|
| Reserved | Reserved | Dp  Data include V2 |
| 是否包含dp data（末尾带4字节Unix时间戳），如果为1需同时包含DP  Control 和Frame Counter，如果加密还需要包含MIC字段 | 漫游标志，0 - 普通设备；1 - 漫游设备 | 是否包含dp data（末尾带4字节Unix时间戳），如果为1需同时包含DP  Control 和Frame Counter，如果加密还需要包含MIC字段 |
| 设备在线逻辑 | Roaming | 漫游标志，0 - 普通设备；1 - 漫游设备 |
| Roaming | 设备在线逻辑 | 0 - 采用标准功耗设备在线策略；<br>1 - 采用低功耗设备在线策略，目前只适用于蓝牙网关，手机 APP 无需关心此位。 |

Elliptic Curve Diffie-Hellman

ID Type(3 bit) + ID Len(5 bit) + ID

- • ID Type：0 - Product ID；1 - Product key；其他 - 保留。

- • ID Len：5 bit，ID 长度，最大 20。

> 注：PID / PRODUCT_KEY —— 未绑定设备该字段存放明文 PID 或 PRODUCT KEY；已绑定设备该字段存放加密数据，加密数据生成算法为 HMAC-SHA-256_secret_key(device_uuid)，输出长度等于 ID 长度。
> 注：device_uuid为hex raw data，无需像之前V4 V5一样转Ascii + PKCS7再去做AES运算。

HKDF

HMAC-based Key Derivation Function

| Bit | 字段 | 说明 |
|---|---|---|
| 0-4 | Reserved | 保留 |
| 7-5 | Encrypted  Type | 0 - 未加密；<br>1 - login  key；<br>2 - beacon key；<br>3 - xxtea  (wifi_ble S2)；<br>4 - xxtea (wifi_ble S3)；<br>其他-保留。<br><br>注：Encrypted Type：3 和 4 BLE 设备不使用，只用于 beacon 遥控器。 |

Frame Counter：

- • beacon 帧序号 Frame Counter 必须持久化到 Flash（或采用「上电时跳跃一个足够大的步长 + 定期存储」的常见折中，避免每包写 Flash）。

- • 溢出后不得清零。到顶时应停止加密广播 DP 数据（配合持久化 + 合理的广播频率）。

DP Data：

格式为 DTLD [……] [DTLD] [T]，不加密时最大 16 字节，加密时最大 12 字节。

| 符号 | 长度 | 说明 |
|---|---|---|
| D | 1 字节 | dp id |
| T | 4 bit | dp type |
| L | 4 bit | dp len，表示 dp data 的长度 |
| D | dp len 字节 | dp data |
| T | 4 字节 | timestamp，Unix 时间戳，可选，「Dp Data Include V2」为 1 时存在该字段 |

> 注：一次 beacon 广播包含一个或多个 DTLD 数据，不同 DTLD 数据的 dp id 不可相同，多个 DTLD 数据共用一个 timestamp。

Message Integrity Check

消息完整性校验值，即 CCM 的认证标签（Tag）

| 项 | 值 |
|---|---|
| 算法 | AES-CCM-128 |
| 密钥 | 由 Encrypted Type 指定，见下 |
| Nonce | Nonce[13] = mac_addr \|\| Frame Control \|\| Frame Control Ext \|\| Frame Counter；<br>mac_addr： 涂鸦分配的固定 MAC |
| AAD | DP Control（1 字节） |
| MIC | 4 字节 |
| 加密对象 | DP Data |
| 输出 | 密文（长度等于 DP Data）‖ MIC(4) |

加密 key 由 Encrypted Type 指定的 key 做 HKDF。例如 Encrypted Type = 1，那么：

key = HKDF-SHA-256(IKM = login key, info = "tuyaos-ble-beacon-key")

> 备注：key 派生没有使用 salt，广播发送 DP 不同于连接会话。

- • 通过 beacon 发送 DP 数据时，ID 字段必须携带涂鸦 MAC 地址，不能携带 device_uuid。

空中升级

### 3.3.2 扫描响应包内容

| 广播数据段描述 | 类型 | 说明 |
|---|---|---|
| Manufacturer data | 0xFF | 长度：0x18（uuid 形态）/ 0x0E（MAC 形态）<br>类型：0xFF <br>数据 ：Manufacturer data |
|  |  |  |
| Custom Complete Local Name | 0x09 | 长度：len(最大15个字节)<br>类型：0x09<br>数据：自定义 |

Manufacturer data 格式如下所示：

| 2字节 | 2字节 | 4bit | 20bit | 6或者16字节 |
|---|---|---|---|---|
| COMPANY ID | 通信能力 | 加密方式 | Flag | ID |

Reserved for Future Use

保留字段/保留位，发送侧置 0、接收侧校验，见 1.2

| Bit | 含义 |
|---|---|
| bit0 | 是否通过 BLE 注册。1 - 通过 BLE 注册绑定设备；0 - 其他通道 |
| bit1 | 是否支持 MESH。1 - 支持；0 - 不支持 |
| bit2 | 是否具有 WiFi 2.4G 通信能力【仅适用于双模】。1 - 有；0 - 没有（App 代码有判断） |
| 是否具有 WiFi 2.4G 通信能力【仅适用于双模】。1 - 有；0 - 没有（App 代码有判断） | 是否具有 WiFi 5G 通信能力【仅适用于双模】。1 - 有；0 - 没有（App 代码有判断） |
| 是否具有 WiFi 5G 通信能力【仅适用于双模】。1 - 有；0 - 没有（App 代码有判断） | 是否具有 Zigbee 通信能力。1 - 有；0 - 没有 |
| 是否具有 Zigbee 通信能力。1 - 有；0 - 没有 | 是否具有 NB 通信能力。1 - 有；0 - 没有 |
| bit6 | 是否具有 cellular 通信能力。1 - 有；0 - 没有 |
| bit7 | 是否具有 WiFi 2.4G 通信能力【仅适用于 BLE+X 设备形态】。1 - 有；0 - 没有 |
| bit8 | 是否具有 WiFi 2.4G 通信能力【仅适用于 BLE+X 设备形态】。1 - 有；0 - 没有 |
| bit9 | 是否具有 WiFi 5G 通信能力【仅适用于 BLE+X 设备形态】。1 - 有；0 - 没有 |
| bit10 | 设备是否具有 BLE long range（Coded PHY）通信能力。1 - 有（同时表明设备存在扩展广播）；0 - 没有 |
| 设备是否具有 BLE long range（Coded PHY）通信能力。1 - 有（同时表明设备存在扩展广播）；0 - 没有 | 保留 |

> 注：bit8 – bit15 任何标志位的使用都不能被 517 之前的公版 App 扫描到。

串口规范，本协议指其链路上的传输层，见 4.3

加密方式（security_mode）

| 取值 | 含义 |
|---|---|
| 0 | 标准加密模式 |
| 1 | 配网带 ECDH，重连不带 |
| 2 | 配网及重连都带 ECDH |
| 其他 | 保留 |

真随机数发生器，见 6.2.4

| Bit | 含义 |
|---|---|
| bit0 | DEVICE UUID 是否是压缩的。1 - 压缩；0 - 非压缩 |
| bit1 | 保留 |
| bit2 | 设备在线逻辑标志。0 - 采用标准功耗设备在线策略；1 - 采用低功耗设备在线策略，目前只适用于蓝牙网关，手机 APP 无需关心此位 |
| bit3 | WIFI-BLE combo 设备是否支持 Plug&play 配网协议标志。0 - 不支持；1 - 支持 |
| bit4 | WIFI-BLE combo 设备配网优先级标志。0 - 优先通过 WiFi 配网；1 - 优先通过蓝牙配网 |
| bit5 | 是否支持获取设备的 WiFi 列表。0 - 不支持；1 - 支持 |
| bit6 | wifi/BLE 设备是否具有 beacon 通信能力。1 - 有；0 - 没有 |
| bit7 | 绑定标志位，手机 APP 无需关心此位 |
| bit8 | 支持扫码配网 |
| bit10 – bit9 | ID flag |
| bit19 – bit11 | 保留 |

名称
	
字节数
	
适用版本
	
说明


product_id
	
8 或 16
	
通用
	
产品标识符，同型号产品共用。文中亦记作 PID / PRODUCT_KEY


device_uuid
	
16 或 20
	
通用
	
设备标识符，与 auth_key 一一对应，由涂鸦产线分配


device_id
	
变长，见 0x1002
	
通用
	
云端分配的设备身份标识，非加密用途


node_id
	
变长，见 0x1002
	
通用
	
组网节点标识，非加密用途


ble_net_key
	
变长，见 0x1002
	
通用
	
组网密钥，非加密用途


ble_mac_addr
	
6
	
通用
	
蓝牙 MAC 地址，小端格式（见 1.2 字节序例外）

> security_mode(4b) │ Flag[19:16](4b) │ Flag[15:8](8b) │ Flag[7:0](8b)

字节数

| ID flag | ID 字段内容 |
|---|---|
| 0 | ID 字段为混淆的 16 字节 device_uuid。这是为避免明文 uuid 直接出现在抓包工具的可读输出中的编码变换，不构成任何安全措施，不防枚举、不防跟踪，混淆算法为：<br>ID = device_uuid ⊕ （SHA-256(obfuscation_data)）[0…15]，其中：<br>obfuscation_data = DATA[0]&0xFD \|\| DATA[1]&0xF0 \|\| DATA[2]&0xE0 \|\| DATA[3...N] ，其中DATA为：<br>广播包中Service Data中的DATA字段。 |
| 1 | ID 字段为涂鸦分配的明文 MAC 地址（大端格式） |
| 其他 | 保留 |

说明

### product_id

### 3.4.1 广播包内容

| 广播数据段描述 | 类型 | 说明 |
|---|---|---|
| 设备 LE 物理连接标识 | 0x01 | 长度：0x02   <br>类型：0x01   <br>数据：0x06 |
| Service Data | 0x16 | 长度：LEN（可变,最大 24+3 字节）<br>类型：0x16  <br>UUID：0xFD50<br>DATA：格式见下表 |

产品标识符，同型号产品共用。文中亦记作 PID / PRODUCT_KEY

| 序号 | 长度 | 必备(M) / 可选(O) | 必备(M) / 可选(O) | 说明 |
|---|---|---|---|---|
| Frame Control | 控制位，大端格式 | Frame Control | M | 控制位，大端格式 |
| 9 or 17 | 1 | Frame Control  Ext | M | Product id或者Product key |
| Product id或者Product key | 9 or 17 | ID | DP Control | Product id或者Product key |
| DP点数据控制位 | DP点数据控制位 | DP Control | O | Frame Counter |
| 5 | 4 | Frame Counter | O | 不加密最大16<br>加密最大12 |
| DP Data | 不加密最大16<br>加密最大12 | DP Data | DTLD | DTLD |
| 7 | 4 | MIC | O | Message Integrity Check，如果dp data加密则为必选字段，大端格式 |

Frame Control：

| Bit | 字段 | 说明 |
|---|---|---|
| 0-1 | Reserved | 保留 |
| 2 | Security_V2 | 支持一般加密 V2 协议 |
| 3 | Security_V2_Confirmed | 执行一般加密V2协议状态（默认0表示未执行V2协议，1表示已执行V2协议） |
| 4 | Gateway Connect Mode | 0-短连接（按需连接）；1-长连接，用于网关 |
| 5-6 | Connectivity | 0-同时支持手机和网关；1-只支持网关；2-只支持手机 |
| 7 | Dp Data Include | 是否包含dp data，如果为1需同时包含DP Control 和Frame Counter，如果加密还需要包含MIC字段。 |
| 8 | ID Include | 0-不包含ID字段；1-包含ID字段 |
| 9 | Request Connection | 1-请求连接，0-未请求连接。 |
| 10 | Shared Flag | 共享类标志，1-涂鸦共享类设备，0-非共享类设备。 |
| 11 | Bound Flag | 绑定标志，0-未绑定，1-已绑定。 |
| 12-15 | Version | 协议版本号，当前为5。 |

设备标识符，与 auth_key 一一对应，由涂鸦产线分配

| Bit | 字段 | 说明 |
|---|---|---|
| 0-4 | Reserved | 保留 |
| 5 | Dp  Data include V2 | 是否包含dp data（末尾带4字节Unix时间戳），如果为1需同时包含DP  Control 和Frame Counter，如果加密还需要包含MIC字段 |
| 6 | Roaming | 漫游标志，0 - 普通设备；1 - 漫游设备 |
| 7 | 设备在线逻辑 | 0 - 采用标准功耗设备在线策略；<br>1 - 采用低功耗设备在线策略，目前只适用于蓝牙网关，手机 APP 无需关心此位。 |

变长，见 0x1002

ID Type(3 bit) + ID Len(5 bit) + ID

- • ID Type：0 - Product ID；1 - Product key；其他 - 保留。

- • ID Len：5 bit，ID 长度，最大 20。

> 注：PID/PRODUCT_KEY：未绑定设备该字段存放明文PID或PRODUCT KEY，已绑定设备该字段存放加密数据，加密数据生成算法为AES-ECB（device_uuid），AES-ECB key由MD5(16字节secret_key)组成。

通用

组网节点标识，非加密用途

| Bit | 字段 | 说明 |
|---|---|---|
| 0-4 | Reserved | 保留 |
| 7-5 | Encrypted  Type | 0 - 未加密；<br>1 - login  key；<br>2 - beacon key；<br>3 - xxtea  (wifi_ble S2)；<br>4 - xxtea (wifi_ble S3)；<br>其他-保留。<br><br>注：Encrypted Type：3 和 4 BLE 设备不使用，只用于 beacon 遥控器。 |

Frame Counter：

- • beacon 帧序号 Frame Counter 必须持久化到 Flash（或采用「上电时跳跃一个足够大的步长 + 定期存储」的常见折中，避免每包写 Flash）。

- • 溢出后不得清零。到顶时应停止加密广播 DP 数据（配合持久化 + 合理的广播频率）。

ble_mac_addr

格式为 DTLD [……] [DTLD] [T]，不加密时最大 16 字节，加密时最大 12 字节。

| 符号 | 长度 | 说明 |
|---|---|---|
| D | 1 字节 | dp id |
| T | 4 bit | dp type |
| L | 4 bit | dp len，表示 dp data 的长度 |
| D | dp len 字节 | dp data |
| T | 4 字节 | timestamp，Unix 时间戳，可选，「Dp Data Include V2」为 1 时存在该字段 |

> 注：一次 beacon 广播包含一个或多个 DTLD 数据，不同 DTLD 数据的 dp id 不可相同，多个 DTLD 数据共用一个 timestamp。

MIC：MIC 为加密认证字段。

名称
	
字节数
	
适用版本
	
说明


auth_key
	
32
	
通用
	
信任根。产线预置，设备与云端各一份，永不经蓝牙传输


login_key
	
6 or 16
	
16是从V4 安全版本起
	
云端签发的绑定密钥。在 0x1002 / 0x0001 中按「长度 + 值」编码传输



secret_key
	
16
	
V4 安全版本起
	
云端签发的绑定密钥，与 login_key 配对使用


beacon_key
	
见 0x1002
	
通用
	
广播 DP 数据加密的派生源（Encrypted Type = 2）, 是家庭维度的秘钥


mRandom / sRandom
	
各 16
	
V6
	
中心侧 / 设备侧每次连接生成的随机数，经 0x1000 交换


serverRandom
	
16
	
V6
	
云端生成，一次授权有效，有有效期


authValue
	
32
	
V6
	
由 auth_key 与 serverRandom 派生，配网握手的认证值


resetAuthValue
	
32
	
V6
	
离线解绑授权值，绑定密钥换代即失效


ecdh_shared
	
32
	
V6
	
ECDH 结果点的 x 坐标


transcript_hash
	
32
	
V6
	
SHA-256( 0x1000 请求 Data ‖ 0x1000 响应 Data )，参与全部会话密钥派生


key21 / 22 / 24 / 25 / 26
	
各 16
	
V6
	
会话密钥，HKDF 输出 24 字节切分为 key[16] ‖ nonce_iv[8]


nonce_iv
	
8
	
V6
	
与会话密钥同源派生，参与 CCM Nonce 构造


RegisterKey
	
32
	
V6
	
HMAC-SHA-256_auth_key( serverRandom )，设备向云端证明持有 auth_key


verify_key（V6）
	
32
	
V6
	
HMAC-SHA-256_auth_key( L(sRandom) || 0x1002 全部云端签发字段 )，设备校验云端合法性


Verify_key（≤V5）
	
4
	
≤V5
	
MD5( login_key + secret_key + auth_key ) 前 4 字节，V2 协议使能校验


session_key
	
16
	
≤V5
	
MD5( login_key, dev_rand )，≤V5 的传输密钥


secret_key_1…16
	
16
	
≤V5
	
≤V5 的各代密钥，生成方式见 7.1


dev_rand
	
6
	
≤V5
	
设备每次连接产生的随机数


server_rand
	
见 7.1
	
≤V5
	
云端生成，用于计算 secret_key_1 / 11 / 16


user_rand
	
见 7.1
	
≤V5
	
用户随机数，用于计算 secret_key_3

### 3.4.2 扫描响应包内容

| 广播数据段描述 | 类型 | 说明 |
|---|---|---|
| Manufacturer data | 0xFF | 长度：0x17或者0x0E<br>类型：0xFF <br>数据 ：<br> - COMPANY ID:0x07D0<br> - 加密方式：0x00<br> - 通信能力：0x0000<br> - FLAG：0x00<br> - ID字段:  见下述介绍 |
|  |  |  |
| Custom Complete Local Name | 0x09 | 长度：len(最大15个字节)<br>类型：0x09   <br>数据：自定义 |

适用版本

0x00-基于auth key和device uuid的一般加密；

0x01-基于ECC算法加密；

0x02-不加密透传通道；

0x03-废弃；

信任根。产线预置，设备与云端各一份，永不经蓝牙传输

0x05-用于扫码配网的基于auth key和device uuid的一般加密；

0x06-基于auth key和device uuid的一般加密（ID字段带FLAG2）；

0x07-高级加密（ID字段带FLAG2）；

0x08-用于扫码配网的基于auth key和device uuid的一般加密（ID字段带FLAG2）；

secret_key

FLAG:

bit0：DEVICE UUID是否是压缩的，1表示压缩，0表示非压缩。

bit1：已配网的高级加密设备每次连接时是否需要和云端进行双向认证，1-需要，0-不需要。

bit2：设备在线逻辑标志，0 – 采用标准功耗设备在线策略，1- 采用低功耗设备在线策略，目前只

适用于蓝牙网关，手机 APP 无需关心此位。

bit3：WIFI-BLE combo设备是否支持Plug&play配网协议标志，0 – 不支持，1- 支持。

bit4：WIFI-BLE combo设备配网优先级标志，0 – 优先通过WIFI配网，1 – 优先通过蓝牙配网。

bit5：是否支持获取设备的WIFI列表，0 – 不支持，1 – 支持。

bit6：表示wifi/BLE设备是否具有beacon通信能力，1表示有，0表示没有。

bit7： 绑定标志位，手机APP无需关心此位。

中心侧 / 设备侧每次连接生成的随机数，经 0x1000 交换

serverRandom

通信能力(2个字节大端格式，高字节在前)：

bit0-表示是否通过BLE注册，1表示通过BLE注册绑定设备，0表示其他通道。

Bit1-表示是否支持MESH，1表示支持，0表示不支持。

Bit2-表示是否具有wifi2.4G通信能力【仅适用于双模】，1表示有，0表示没有（App代码有判断）。

Bit3-表示是否具有wifi5G通信能力【仅适用于双模】，1表示有，0表示没有（App代码有判断）。

Bit4-表示是否具有zigbee通信能力，1表示有，0表示没有。

由 auth_key 与 serverRandom 派生，配网握手的认证值

Bit6-表示是否具有 cellular 通信能力，1 表示有，0 表示没有其他位保留。其他位保留。

Bit7-表示是否具有wifi2.4G通信能力【仅适用于ble+x设备形态】，1表示有，0表示没有。

Bit8-表示是否具有wifi5G通信能力【仅适用于ble+x设备形态】，1表示有，0表示没有。

Bit9-表示是否具有有线通信（Ethernet，以太网）能力，1表示有，0表示没有。

ecdh_shared

ID字段：

当加密方式是0x00、0x04、0x05时，该字段存放加密的16字节DEVCIE UUID， 加密算法AES-CBC，加密key为service data字段中的全部data（不包含service uuid）计算MD5，IV等于key。

当加密方式是0x06、0x07、0x08时，该字段存放1字节FLAG2 再加 n字节data （取决于FLAG2的bit1-bit0）：

transcript_hash

bit1 – bit0 （值0-3）：为0时，data部分为涂鸦分配的明文MAC地址（大端格式），其他值保留。

bit7-bit2保留。

SHA-256( 0x1000 请求 Data ‖ 0x1000 响应 Data )，参与全部会话密钥派生

### key21 / 22 / 24 / 25 / 26

### 3.5.1 广播包内容

| 广播数据段描述 | 类型 | 说明 |
|---|---|---|
| 设备 LE 物理连接标识 | 0x01 | 长度：0x02   <br>类型：0x01   <br>数据：0x06 |
| Service UUID | 0x02 | 长度：0x03   <br>类型：0x02   <br>数据：0xFD50 |
| Service Data | 0x16 | 长度：LEN（可变,最大 20+3 字节）<br>类型：0x16  <br>UUID：0xFD50<br>DATA：格式见下表 |

会话密钥，HKDF 输出 24 字节切分为 key[16] ‖ nonce_iv[8]

| 序号 | 长度 | 字段 | 必备(M) / 可选(O) | 必备(M) / 可选(O) |
|---|---|---|---|---|
| 说明 | 2 | Frame Control | Frame Control | 控制位，大端格式 |
| 控制位，大端格式 | 10 or 18 | 10 or 18 | ID | Product id或者Product key |
| 3 | Product id或者Product key | DP Control | O | DP Control |
| 4 | DP点数据控制位 | Frame Counter | O | 帧序号用于防重放，大端格式 |
| 5 | 帧序号用于防重放，大端格式 | DP Data | DP Data | DTLD |
| DTLD | DTLD | MIC | MIC | Message Integrity Check，如果dp data加密则为必选字段，大端格式 |

Frame Control定义：

| Bit | 字段 | 说明 |
|---|---|---|
| 0-1 | Reserved | 保留 |
| 2 | Security_V2 | 支持一般加密V2协议 |
| 3 | Security_V2_Confirmed | 执行一般加密V2协议状态（默认0表示未执行V2协议，1表示已执行V2协议） |
| 4 | Gateway Connect Mode | 0-短连接（按需连接）；1-长连接，用于网关 |
| 5-6 | Connectivity | 0-同时支持手机和网关；1-只支持网关；2-只支持手机 |
| 7 | Dp Data Include | 是否包含dp data，如果为1需同时包含DP Control 和Frame Counter，如果加密还需要包含MIC字段。 |
| 8 | ID Include | 0-不包含ID字段；1-包含ID字段 |
| 9 | Request Connection | 1-请求连接，0-未请求连接。 |
| 10 | Shared Flag | 共享类标志，1-涂鸦共享类设备，0-非共享类设备。 |
| 11 | Bound Flag | 绑定标志，0-未绑定，1-已绑定。 |
| 12-15 | Version | 协议版本号，当前为V4。 |

ID字段：ID Type(1字节) + ID Len(1字节)  + ID（8或者16字节）。

ID Type：0-Product ID；1-Product key；

ID Len：1字节，目前为8或者16；

> 注：PID/PRODUCT_KEY：未绑定设备该字段存放明文PID或PRODUCT KEY，已绑定设备该字段存放加密数据，加密数据生成算法为AES-ECB（device_uuid），AES-ECB key由MD5(16字节secret_key)组成。

HMAC-SHA-256_auth_key( serverRandom )，设备向云端证明持有 auth_key

| Bit | 字段 | 说明 | 备注 |
|---|---|---|---|
| 0-4 | Reserved | 保留 |  |
| 7-5 | Encrypted Type | 0-未加密；<br>1-login key；<br>2-beacon key；<br>3-xxtea (wifi_ble S2)；<br>4-xxtea (wifi_ble S3)；<br>其他-保留。 | 3/4介绍见遥控器协议文档 |

Frame Counter：beacon帧序号，溢出清零，用于防重放。

DP Data：格式为DTLD (DP ID一个字节、DP TYPE和DP LEN总共一个字节（各4bits）、DP DATA加密时最大7个字节，不加密时最大11字节)。

HMAC-SHA-256_auth_key( L(sRandom) || 0x1002 全部云端签发字段 )，设备校验云端合法性

Verify_key（≤V5）

加密算法为AES-CCM-128，加密内容为DP Data，iv取2次Frame Control和Frame Counter组成12字节，加密key取Encrypted Type指定的加密元素的MD5。

≤V5

### MD5( login_key + secret_key + auth_key ) 前 4 字节，V2 协议使能校验

| 广播数据段描述 | 类型 | 说明 |
|---|---|---|
| Manufacturer data | 0xFF | 长度：0x17或者0x0E<br>类型：0xFF <br>数据 ：<br> - COMPANY ID:0x07D0<br> - 加密方式：0x00<br> - 通信能力：0x0000<br> - FLAG：0x00<br> - ID字段:  见下述介绍 |
|  |  |  |
| Custom Complete Local Name | 0x09 | 长度：len(最大15个字节)<br>类型：0x09   <br>数据：自定义 |

加密方式：

0x00-基于auth key和device uuid的一般加密；

MD5( login_key, dev_rand )，≤V5 的传输密钥

secret_key_1…16

0x03-废弃；

0x04-高级加密。

0x05-用于扫码配网的基于auth key和device uuid的一般加密；

0x06-基于auth key和device uuid的一般加密（ID字段带FLAG2）；

0x07-高级加密（ID字段带FLAG2）；

0x08-用于扫码配网的基于auth key和device uuid的一般加密（ID字段带FLAG2）；

设备每次连接产生的随机数

server_rand

bit0：DEVICE UUID是否是压缩的，1表示压缩，0表示非压缩。

bit1：已配网的高级加密设备每次连接时是否需要和云端进行双向认证，1-需要，0-不需要。

bit2：设备在线逻辑标志，0 – 采用标准功耗设备在线策略，1- 采用低功耗设备在线策略，目前只

适用于蓝牙网关，手机 APP 无需关心此位。

bit3：WIFI-BLE combo设备是否支持Plug&play配网协议标志，0 – 不支持，1- 支持。

bit4：WIFI-BLE combo设备配网优先级标志，0 – 优先通过WIFI配网，1 – 优先通过蓝牙配网。

bit5：是否支持获取设备的WIFI列表，0 – 不支持，1 – 支持。

bit6：表示wifi/BLE设备是否具有beacon通信能力，1表示有，0表示没有。

名称
	
字节数
	
说明


Tuya CA Certificate
	
—
	
涂鸦 CA 证书


Tuya CA Public Key
	
64
	
涂鸦 CA 公钥，用于验证 server 证书和 device 证书


Tuya CA Private Key
	
32
	
涂鸦 CA 私钥，用于签名 server 证书和 device 证书


Device Certificate
	
—
	
设备证书，由涂鸦 CA 签发


Device Public Key
	
64
	
设备端证书公钥


Device Private Key
	
32
	
设备端证书私钥


Server Certificate
	
—
	
云端证书，由涂鸦 CA 签发


Server Public Key
	
64
	
云端证书公钥


Server Private Key
	
32
	
云端证书私钥

其他位保留。

字节数

通信能力(2个字节大端格式，高字节在前)：

bit0-表示是否通过BLE注册，1表示通过BLE注册绑定设备，0表示其他通道。

Bit1-表示是否支持MESH，1表示支持，0表示不支持。

Bit2-表示是否具有wifi2.4G通信能力【仅适用于双模】，1表示有，0表示没有（App代码有判断）。

Bit3-表示是否具有wifi5G通信能力【仅适用于双模】，1表示有，0表示没有（App代码有判断）。

Bit4-表示是否具有zigbee通信能力，1表示有，0表示没有。

涂鸦 CA 公钥，用于验证 server 证书和 device 证书

Bit6-表示是否具有 cellular 通信能力，1 表示有，0 表示没有其他位保留。其他位保留。

Bit7-表示是否具有wifi2.4G通信能力【仅适用与ble+x设备形态】，1表示有，0表示没有。

Bit8-表示是否具有wifi5G通信能力【仅适用与ble+x设备形态】，1表示有，0表示没有。

Bit9-表示是否具有有线通信（Ethernet，以太网）能力，1表示有，0表示没有。

Bit10-表示设备是否具有BLE long range（Coded PHY）通信能力，1表示有（同时表明设备存在扩展广播），0表示没有。

注：bit8-bit15任何标位的使用都不能被517之前的公版app扫描到。

Device Public Key

ID字段：

当加密方式是0x00、0x04、0x05时，该字段存放加密的16字节DEVCIE UUID， 加密算法AES-CBC，加密key为service data字段中的全部data（不包含service uuid）计算MD5，IV等于key。

当加密方式是0x06、0x07、0x08时，该字段存放1字节FLAG2 再加 n字节data （取决于FLAG2的bit1-bit0）：

FLAG2：

bit1 – bit0 （值0-3）：为0时，data部分为涂鸦分配的明文MAC地址（大端格式），其他值保留。

Server Certificate

—

### 云端证书，由涂鸦 CA 签发

### Server Public Key

SERVICE UUID：0xFD50

包含 Write Characteristic 和 Notify Characteristic 。

| 项 | Write Characteristic | Notify Characteristic |
|---|---|---|
| UUID | 00000001-0000-1001-8001-00805F9B07D0 | 00000002-0000-1001-8001-00805F9B07D0 |
| 说明 | 中心侧 → 设备 | 设备 → 中心侧 |
| 属性 | Write Without Response | Notify |
| 数据长度 | ATT MTU − 3 | ATT MTU − 3 |

Read Characteristic UUID：00000003-0000-1001-8001-00805F9B07D0

- • 适用：可选，只有需要使能标准蓝牙配对绑定机制的设备才需要

- • 属性： Read

- • 数据长度：可变长度，不受ATT MTU的限制

- • 数据格式：TLD [TLD] [TLD]…(至少一组TLD)， 如下表所示：

| 符号 | 长度 | 说明 |
|---|---|---|
| T | 1 字节 | Type |
| L | 1 字节 | Length |
| D | Length 字节 | Data |

当T = 0时，D的内容是当前广播数据的全部内容，包括广播包和扫描响应包。T = 其他值未定义，保留。

### 单个 AD Structure组成结构

字段
	
长度
	
说明


Length
	
1
	
后续字节数，计入 AD Type，不计入 Length 自身


AD Type
	
1
	
内容类型，取值见 Bluetooth Assigned Numbers


AD Data
	
Length − 1
	
该类型承载的内容

MTU 交换自 ATT 协议引入即支持，通常在连接建立后立即进行，主从双方取较小值作为本次连接的 ATT_MTU。Bluetooth 4.2 引入 Data Length Extension，链路层单包上限提升后，较大的 ATT_MTU 才具备实际吞吐收益。

MTU交换通常发生在主从双方建立连接关系后，如下图所示：

![Feishu Docs - Image](blob:https://icn602w9tnqf.feishu.cn/0435e4ed-dd53-4060-bda7-7ea5e3759db4)

手机端蓝牙MTU交换在蓝牙建立连接后会自动进行，无需上层应用干预，但是上层应用需要拿到交换的结果ATT MTU值，然后通过获取设备信息指令将（ATT MTU - 3）发送给设备，之后的通信就采用该值作为每次写操作的最大值。

# 5. 分包数据协议

### 后续字节数，计入 AD Type，不计入 Length 自身

![Feishu Docs - Image](blob:https://icn602w9tnqf.feishu.cn/91315196-ac90-42c2-a827-b3624064e29a)

- • 协议版本标识：1 / 2 / 3 等。

- • 每包最大数据长度为当次连接通过V6版本 0x1000 指令 / 其他版本为 0x0000指令 下发的（ATT MTU − 3）值。

- • 当数据在分包处理传输时，帧数据长度、帧标识和加密方式只有是第一包时才有意义，其它包的情况下为空。

- • 包序号以及包长度，单字节最大值为 01111111(127)，若第八位(最高位)为 1，表明还有后续字节存在。包序号或包长度最多只有 4 字节长度表示，所以最大四字节包序号或包长度： 0xff 0xff 0xff 0x7f(十进制:268435455 包或字节数)。

| 字节数 | 起始 | 末尾 |
|---|---|---|
| 1 | 0(00) | 127(7f) |
| 2 | 128(80 01) | 16383(ff 7f) |
| 3 | 16384(80 80 01) | 2097151(ff ff 7f) |
| 4 | 2097152(80 80 80 01) | 268435455(ff ff ff 7f) |

> 注：128进制倒序表示法，例如0x8001 = 0 + 1x(0x80) = 128;  0xff7f = 0x7f + 0x7f x 0x80 = 16383。

•
AD Data 中由蓝牙规范定义的字段按小端传输，如 16-bit Service UUID 0xFD50 空口顺序为 50 FD（见 1.2 字节序例外）。

•
本协议使用的 AD Type：
◦
0x01 Flags
◦
0x02 Service UUID（仅 ≤V5）
◦
0x16 Service Data
◦
0xFF Manufacturer Specific Data
◦
0x09 Complete Local Name

## 5.2 SPP链路传输层协议

适用对象：扫描方（App / 网关 / 其他中心设备）。设备端只广播自身固件对应的版本，不需要判定。

判定字段：广播包 Service Data（UUID 0xFD50）中 DATA 的前 2 字节 Frame Control，其 bit12–bit15 为 Version。该字段在所有协议版本中的位置与宽度均一致，因此可作为版本判定的共同锚点。

在实际传输中，应用程序多次调用发送函数写入的数据，可能被操作系统或协议栈在缓冲区中合并为单个数据块进行发送：同样的，接收端一次读取操作可能只获取到完整数据包的一部分，或者包含多个数据包。由于边界不明确，可能出现：

- Code block C Version = (FC[0] >> 4) & 0x0F

- • 粘包问题：多个数据包合并成一次接收

- Version 	 含义 	 按哪一节解析   6 	 V6协议 	 3.3 V6 广播格式   5 	 基本无存量设备 	 3.4 V5 广播格式   ≤4 	 存量设备 	 3.5 ≤V4 广播格式   大于 6 	 高于本端支持的版本 	 不予解析，忽略该设备

3.3
V6 广播格式

原分包协议中，因sub_pkg中仅有序号+数据，没有长度字段，在SPP链路上对帧的重组就会遇到问题。因此在SPP传输之上增加一层应用协议成为必然选择，这层协议需要解决：

- 广播数据段描述  	 类型  	 说明    设备 LE 物理连接标识   	 0x01   	 长度：0x02    类型：0x01    数据：0x06   Service Data  	 0x16 	 长度：LEN（可变,最大 24+3 字节） 类型：0x16   UUID：0xFD50 DATA：格式见下表

- DATA格式如下所示（DATA长度最大24字节）：

序号
	
长度
	
字段
	
必备(M) / 可选(O)
	
说明


1
	
2
	
Frame Control
	
M
	
控制位，大端格式


2
	
1
	
Frame Control  Ext
	
M
	
扩展控制位，大端格式


3
	
9 or 17
	
ID
	
O
	
Product id或者Product key


4
	
1
	
DP Control 
	
O
	
DP点数据控制位


5
	
4
	
Frame Counter
	
O
	
帧序号用于防重放，大端格式


6
	
不加密最大16
加密最大12
	
DP Data
	
O
	
DTLD


7
	
4
	
MIC
	
O
	
Message Integrity Check，如果dp data加密则为必选字段，大端格式

Frame Control：

Bit
	
字段
	
说明


2-0
	
Reserved
	
保留



3

	
Request_keys_update
	
请求long-term keys更新（默认0表示不请求，1表示请求）
适用于老设备（v1版)请求更新密钥（比如v1老设备 升级后，设备控制该bit=1请求请求更新密钥，当重连更新后，设备控制该bit=0）


4
	
Gateway Connect Mode
	
0-短连接（按需连接）；1-长连接，用于网关


5-6
	
Connectivity
	
0-同时支持手机和网关；1-只支持网关；2-只支持手机


7
	
Dp Data Include
	
是否包含dp data，如果为1需同时包含DP Control 和Frame Counter，如果加密还需要包含MIC字段。


8
	
ID Include
	
0-不包含ID字段；1-包含ID字段


9
	
Request Connection
	
1-请求连接，0-未请求连接。


10
	
Shared Flag
	
共享类标志，1-涂鸦共享类设备，0-非共享类设备。


11
	
Bound Flag
	
绑定标志，0-未绑定，1-已绑定。


12-15
	
Version
	
协议版本号，当前为6。

1. 1. 要发送的空中帧按分包协议分成了 n 个小包发送；

1. Bit 	 字段 	 说明   0-4 	 Reserved 	 保留   5  	 Dp  Data include V2 	 是否包含dp data（末尾带4字节Unix时间戳），如果为1需同时包含DP  Control 和Frame Counter，如果加密还需要包含MIC字段   6 	 Roaming 	 漫游标志，0 - 普通设备；1 - 漫游设备   7 	 设备在线逻辑 	 0 - 采用标准功耗设备在线策略； 1 - 采用低功耗设备在线策略，目前只适用于蓝牙网关，手机 APP 无需关心此位。

如下图示意： 原始数据被分为 P1、P2、P3；每个小包在链路上封装为：55AA + 对应数据 + sum8。

![Feishu Docs - Image](blob:https://icn602w9tnqf.feishu.cn/c1853775-411e-4450-a878-4995f9a357e9)

•
ID Type：0 - Product ID；1 - Product key；其他 - 保留。

•
ID Len：5 bit，ID 长度，最大 20。

| 偏移 | 长度 | 字段 | 说明 |
|---|---|---|---|
| 0 | 帧同步头1 | 帧同步头1 | 0x55 |
| 帧同步头2 | 1 | 帧同步头2 | 0xAA |
| 版本号 | 1 | 协议升级扩展用，默认0x00 | 协议升级扩展用，默认0x00 |
| 3 | LEN_H | 数据长度 高8位 | 数据长度 高8位 |
| 4 | 数据长度 低8位 | LEN_L | 数据长度 低8位 |
| hdr_check | hdr_check | 头部校验信息，XOR+Magic | 头部校验信息，XOR+Magic |
| … | N | 数据Data | - |
| … | 1 | sum8 | 和检验 |

说明：

- • 所有大于 1 个字节的数据均采用大端模式传输;

- Bit 	 字段 	 说明   0-4 	 Reserved 	 保留   7-5 	 Encrypted  Type 	 0 - 未加密； 1 - login  key； 2 - beacon key； 3 - xxtea  (wifi_ble S2)； 4 - xxtea (wifi_ble S3)； 其他-保留。  注：Encrypted Type：3 和 4 BLE 设备不使用，只用于 beacon 遥控器。

- • 数据长度：仅指数据Data部分的长度，不包含hdr_check与sum8;

- • beacon 帧序号 Frame Counter 必须持久化到 Flash（或采用「上电时跳跃一个足够大的步长 + 定期存储」的常见折中，避免每包写 Flash）。

```
UINT8_T tal_util_check_sum8(UINT8_T* buf, UINT32_T size)
{
    UINT8_T sum = 0;
    for (UINT32_T idx=0; idx<size; idx++) {
    sum += buf[idx];
    }
    return sum;
}
```

DP Data：

### 格式为 DTLD [……] [DTLD] [T]，不加密时最大 16 字节，加密时最大 12 字节。

### 符号 长度 说明 D 1 字节 dp id T 4 bit dp type L 4 bit dp len，表示 dp data 的长度 D dp len 字节 dp data T 4 字节 timestamp，Unix 时间戳，可选，「Dp Data Include V2」为 1 时存在该字段

### 注：一次 beacon 广播包含一个或多个 DTLD 数据，不同 DTLD 数据的 dp id 不可相同，多个 DTLD 数据共用一个 timestamp。

security_mode 只决定一件事：ECDH 加在哪一次连接上。 三档的报文格式、认证方式、防重放机制、流程条数完全相同。加入 ECDH 即具备前向保密 —— 长期密钥日后泄露，也无法解密此前录制的流量（还需要那对早已销毁的临时私钥）。

| security_mode | 配网 | 重连 |
|---|---|---|
| 0 | 无 | 无 |
| 1 | 有 | 无 |
| 2 | 有 | 有 |

项
	
值


算法
	
AES-CCM-128


密钥
	
由 Encrypted Type 指定，见下


Nonce
	
Nonce[13] = mac_addr || Frame Control || Frame Control Ext || Frame Counter；
mac_addr： 涂鸦分配的固定 MAC


AAD
	
DP Control（1 字节）


MIC
	
4 字节


加密对象
	
DP Data


输出
	
密文（长度等于 DP Data）‖ MIC(4)

| 步骤 | 判断 | 结果 |
|---|---|---|
| 1 | 设备端能否实现 P-256？<br>- 有 ECC 硬件加速的 SoC 直接算能；<br>- 纯软件实现需实测标量乘耗时、栈深与 flash 占用后再定 | 不能 → mode_0（唯一可选）<br>能 → 进入第 2 步 |
| 2 | 是否同时满足下列两条：<br>① 日常重连流量不敏感（不含门锁开关记录、人体存在状态等可推断隐私的数据）<br>② 重连极频繁，且对连接建立时延敏感 | 同时满足 → mode_1<br>否则 → mode_2（默认） |

> ⚠ mode_1 的 ECC 代码尺寸与 mode_2 相同 —— 其配网同样要跑 ECDH，故设备端同样需实现完整 P-256，节省的只是每次重连的运算时间，不是空间。

备注：key 派生没有使用 salt，广播发送 DP 不同于连接会话。

•
通过 beacon 发送 DP 数据时，ID 字段必须携带涂鸦 MAC 地址，不能携带 device_uuid。

- • 设备固件：唯一权威。设备只接受本档的流程，收到其它档的 Key_type 或 Flags 组合一律拒收断连。

- • 广播 Manufacturer data（4 bit）：仅作连接前提示，不得作为发起依据。广播中该值为明文且不可认证，可被伪造。但这不构成降级 —— 设备只认自身固件配置，攻击者伪造广播只能造成握手失败（可用性），无法使双方回落到较弱流程。中心侧不得因握手失败而自动回落到更低模式重试。

### 广播数据段描述 类型 说明 Manufacturer data 0xFF 长度：0x18（uuid 形态）/ 0x0E（MAC 形态） 类型：0xFF 数据 ：Manufacturer data Custom Complete Local Name 0x09 长度：len(最大15个字节) 类型：0x09 数据：自定义

Manufacturer data 格式如下所示：

2字节
	
2字节
	
4bit
	
20bit
	
6或者16字节


COMPANY ID
	
通信能力
	
加密方式
	
Flag
	
ID

|  | 从 | 经 | 到 | 内容 | 说明 |
|---|---|---|---|---|---|
| 根 | — | — | 设备 + 云端 | auth_key | 出厂预置，两侧各持一份，终生不变。永不经蓝牙传输，永不下发给手机或网关 |
| 分支一 | 云端 | — | 手机 / 网关 | authValue | HKDF(auth_key, salt=serverRandom)。中心侧拿到的是派生物，无法反推根 |
| 分支二 | 云端 | 手机 | 设备 | login_key·<br>secret_key·<br>beacon_key·<br>… | 配网时由 0x1002 转交。设备此后凭它们重连，不再需要云端参与 |
| 凭证 ① | 设备 | 手机 | 云端 | RegisterKey | 设备证明本机在场且持有 auth_key（经 0x1001 响应转呈） |
| 凭证 ② | 云端 | 手机 | 设备 | verify_key | 云端证明 0x1002 所载密钥确由云端签发 |

「经」列只有一个值：手机。auth_key 那一行是唯一不经过手机的 —— 这就是整个信任模型的支点。

### Bit 含义 bit0 是否通过 BLE 注册。1 - 通过 BLE 注册绑定设备；0 - 其他通道 bit1 是否支持 MESH。1 - 支持；0 - 不支持 bit2 是否具有 WiFi 2.4G 通信能力【仅适用于双模】。1 - 有；0 - 没有（App 代码有判断） bit3 是否具有 WiFi 5G 通信能力【仅适用于双模】。1 - 有；0 - 没有（App 代码有判断） bit4 是否具有 Zigbee 通信能力。1 - 有；0 - 没有 bit5 是否具有 NB 通信能力。1 - 有；0 - 没有 bit6 是否具有 cellular 通信能力。1 - 有；0 - 没有 bit7 是否具有 WiFi 2.4G 通信能力【仅适用于 BLE+X 设备形态】。1 - 有；0 - 没有 bit8 是否具有 WiFi 5G 通信能力【仅适用于 BLE+X 设备形态】。1 - 有；0 - 没有 bit9 是否具有有线通信（Ethernet，以太网）能力。1 - 有；0 - 没有 bit10 设备是否具有 BLE long range（Coded PHY）通信能力。1 - 有（同时表明设备存在扩展广播）；0 - 没有 其他 保留

### 注：bit8 – bit15 任何标志位的使用都不能被 517 之前的公版 App 扫描到。

存于非易失存储，跨连接、跨上下电保持。

| 密钥 | 来源 | 生命周期 | auth_key |
|---|---|---|---|
| 产线预置，设备与云端各一份 | 产线预置，设备与云端各一份 | 终生不变，离线解绑亦保留 | 派生 authValue、resetAuthValue；计算 RegisterKey、verify_key。永不经蓝牙传输，永不下发给手机 |
| login_key | 云端签发，配网时经 0x1002 下发 | 云端签发，配网时经 0x1002 下发 | key24/key25 的 IKM 组成；广播 DP 密钥派生源；resetAuthValue 的 IKM 组成 |
| key24/key25 的 IKM 组成；广播 DP 密钥派生源；resetAuthValue 的 IKM 组成 | secret_key | 同上 | 同 login_key；另用作已绑定设备广播 ID 字段的 HMAC 密钥 |
| 同 login_key；另用作已绑定设备广播 ID 字段的 HMAC 密钥 | beacon_key | 同上 | 广播 DP 加密（Encrypted Type = 2） |
| verify_key | 同上，32 字节 | 同上 | 仅用于设备校验 0x1002 所载数据确由云端签发，本身不作为加密密钥 |
| 仅用于设备校验 0x1002 所载数据确由云端签发，本身不作为加密密钥 | 同上 | 同上 | 身份标识与组网，非加密用途 |

### 取值 含义 0 标准加密模式 1 配网带 ECDH，重连不带 2 配网及重连都带 ECDH 其他 保留

单次连接或单次授权有效，均不落盘。

| 材料 | 生成方 | 有效范围 | 说明 |
|---|---|---|---|
| mRandom / sRandom | 中心侧 / 设备侧各自生成，16 字节 | 一次连接 | 经 0x1000 交换，见指令 |
| serverRandom | 云端，16 字节 | 一次授权 | 一次性使用、有有效期 |
| authValue[32] | 云端派生后下发中心侧；设备自行算出 | 一次配网 | 设备持有 auth_key、又从 0x1000 收到 serverRandom，故无需他方下发 |
| resetAuthValue[32] | 同上 | 一次解绑授权 | IKM 含 login_key‖secret_key，故绑定到当前这一代绑定密钥 |
| ecdh_shared[32] | 双方各自计算 | 一次连接 | ECDH 结果点的 x 坐标。算出后立即清零临时私钥，见 6.2.3 ECDH |
| key21/22/24/25/26及各自 nonce_iv | 双方各自派生 | 一次连接 | 连接结束即失效。云端不持有这些密钥 |
| transcript_hash[32] | 双方各自计算 | 一次连接 | SHA-256( 0x1000 请求的 Data ‖ 0x1000 响应的 Data )，详见握手记录哈希 |

### 注意：security_mode + Flag是组合字节使用，一起3字节。先 4 bit 加密方式，紧接 20 bit Flag。Flag 内部高位在前，字节流空中顺序：

| 项 | 内容 |
|---|---|
| 曲线 | NIST P-256（secp256r1） |
| 公钥格式 | 64 字节，为未压缩格式。去掉 0x04 前缀后的裸 X ‖ Y，X 与 Y 各 32 字节大端 |
| 共享秘密 | ecdh_shared[32] = ECDH 结果点的 x 坐标，32 字节大端。只取 x 坐标，不得使用 X ‖ Y |
| 公钥合法性校验（强制） | 收到的点必须满足 P-256 曲线方程、且不是无穷远点。校验失败直接断开连接。不做该校验会导致 invalid-curve attack |
| 临时密钥 | 每次连接必须重新生成临时密钥对，绝不复用；算出 ecdh_shared 后立即将临时私钥清零。临时私钥复用等于作废前向保密 |

实现提示（非规范性）：设备端在发出 0x1000 明文响应之后即可立即开始计算 ecdh_shared，无需等待对端的下一个报文到达，可把一次标量乘的耗时隐藏在空口往返里。

### ID flag ID 字段内容 0 ID 字段为混淆的 16 字节 device_uuid。这是为避免明文 uuid 直接出现在抓包工具的可读输出中的编码变换，不构成任何安全措施，不防枚举、不防跟踪，混淆算法为： ID = device_uuid ⊕ （SHA-256(obfuscation_data)）[0…15]，其中： obfuscation_data = DATA[0]&0xFD || DATA[1]&0xF0 || DATA[2]&0xE0 || DATA[3...N] ，其中DATA为： 广播包中Service Data中的DATA字段。 1 ID 字段为涂鸦分配的明文 MAC 地址（大端格式） 其他 保留

本节为规范性要求。随机性不足会直接导致 AES-CCM 的 nonce 复用，从而破坏机密性，因此以下各条均为强制。

3.4
V5 广播格式

| 随机数 | 生成方 |
|---|---|
| mRandom | 中心侧，每次连接 |
| sRandom | 设备侧，每次连接 |
| 临时 P-256 密钥对<br>（m_private / s_private） | 双方各自，每次连接（仅 mode_1 / mode_2） |
| serverRandom | 云端，每次授权请求 |

广播数据段描述 
	
类型 
	
说明 


设备 LE 物理连接标识 

	
0x01 

	
长度：0x02   
类型：0x01   
数据：0x06   


Service Data

	
0x16
	
长度：LEN（可变,最大 24+3 字节）
类型：0x16  
UUID：0xFD50
DATA：格式见下表

上述随机数必须来自真随机数发生器（TRNG），或来自经熵源评估的确定性随机比特发生器（DRBG）（建议符合 NIST SP 800-90A / 90B）。

序号
	
长度
	
字段
	
必备(M) / 可选(O)
	
说明


1
	
2
	
Frame Control
	
M
	
控制位，大端格式


2
	
1
	
Frame Control  Ext
	
M
	
扩展控制位，大端格式


3
	
9 or 17
	
ID
	
O
	
Product id或者Product key


4
	
1
	
DP Control 
	
O
	
DP点数据控制位


5
	
4
	
Frame Counter
	
O
	
帧序号用于防重放，大端格式


6
	
不加密最大16
加密最大12
	
DP Data
	
O
	
DTLD


7
	
4
	
MIC
	
O
	
Message Integrity Check，如果dp data加密则为必选字段，大端格式

| 禁用方式 | 原因 |
|---|---|
| 标准库 rand() / srand() | 可预测，种子空间小 |
| 系统时间戳 | 可预测，分辨率有限 |
| MAC 地址或 device_uuid 的变换 | 静态值，完全可预测 |
| 上电计数器 | 单调可预测 |

Bit
	
字段
	
说明


0-1
	
Reserved
	
保留


2
	
Security_V2
	
支持一般加密 V2 协议


3
	
Security_V2_Confirmed
	
执行一般加密V2协议状态（默认0表示未执行V2协议，1表示已执行V2协议）


4
	
Gateway Connect Mode
	
0-短连接（按需连接）；1-长连接，用于网关


5-6
	
Connectivity
	
0-同时支持手机和网关；1-只支持网关；2-只支持手机


7
	
Dp Data Include
	
是否包含dp data，如果为1需同时包含DP Control 和Frame Counter，如果加密还需要包含MIC字段。


8
	
ID Include
	
0-不包含ID字段；1-包含ID字段


9
	
Request Connection
	
1-请求连接，0-未请求连接。


10
	
Shared Flag
	
共享类标志，1-涂鸦共享类设备，0-非共享类设备。


11
	
Bound Flag
	
绑定标志，0-未绑定，1-已绑定。


12-15
	
Version
	
协议版本号，当前为5。

- • mRandom、sRandom、临时密钥对必须每次连接重新生成；serverRandom 必须每次云端请求重新生成。任何形式的复用均不允许。

- Bit 	 字段 	 说明   0-4 	 Reserved 	 保留   5  	 Dp  Data include V2 	 是否包含dp data（末尾带4字节Unix时间戳），如果为1需同时包含DP  Control 和Frame Counter，如果加密还需要包含MIC字段   6 	 Roaming 	 漫游标志，0 - 普通设备；1 - 漫游设备   7 	 设备在线逻辑 	 0 - 采用标准功耗设备在线策略； 1 - 采用低功耗设备在线策略，目前只适用于蓝牙网关，手机 APP 无需关心此位。

### 6.2.5 密钥派生链

ID Type(3 bit) + ID Len(5 bit) + ID

•
ID Type：0 - Product ID；1 - Product key；其他 - 保留。

```
authValue[32]      = HKDF( IKM  = auth_key,
                           salt = serverRandom,
                           info = "tuyaos-ble-auth-value",  L = 32 )

resetAuthValue[32] = HKDF( IKM  = auth_key ‖ login_key ‖ secret_key,
                           salt = device_uuid ‖ serverRandom,
                           info = "tuyaos-ble-reset-auth-value",  L = 32 )
```

注：PID/PRODUCT_KEY：未绑定设备该字段存放明文PID或PRODUCT KEY，已绑定设备该字段存放加密数据，加密数据生成算法为AES-ECB（device_uuid），AES-ECB key由MD5(16字节secret_key)组成。

五把密钥同一个模子，salt 与 L 完全相同，仅 IKM 与 info 不同：

```
HKDF(
    IKM  = 〔每把不同，见下表〕
    salt = mRandom ‖ sRandom ‖ transcript_hash
    info = 〔每把不同，见下表〕
    L    = 24
) 

L=24 的输出统一切分为 key[16] ‖ nonce_iv[8]
```

| Key_type | IKM | info |
|---|---|---|
| 21 | authValue | "tuyaos-ble-key21" |
| "tuyaos-ble-key21" | authValue ‖ ecdh_shared | "tuyaos-ble-key22" |
| "tuyaos-ble-key22" | login_key ‖ secret_key | "tuyaos-ble-key24_session" |
| "tuyaos-ble-key24_session" | login_key ‖ secret_key ‖ ecdh_shared | login_key ‖ secret_key ‖ ecdh_shared |
| "tuyaos-ble-key25_session" | resetAuthValue | "tuyaos-ble-key26" |

握手记录哈希 transcript_hash

•
beacon 帧序号 Frame Counter 必须持久化到 Flash（或采用「上电时跳跃一个足够大的步长 + 定期存储」的常见折中，避免每包写 Flash）。

- • 其中「Data」指 0x1000 请求 / 响应中 Data 字段的完整字节序列，按实际传输的字节取值（含 Flags、dataMTU、随机数以及所有存在的可选字段），不含功能码、DataLength 与 CRC16。10

- • 收发双方在握手阶段各自计算该值，并将其纳入本连接后续所有密钥派生的 salt。

- • 作用：0x1000 为明文报文，其中的 Flags、dataMTU、公钥等字段不参与 AES-CCM 保护，也不在 AAD 中，可被中间人无声篡改。将 transcript_hash 纳入密钥派生后，握手报文中任何一个字节被改动都会导致双方派生出不同的密钥，后续第一个加密报文的 MIC 校验即失败，从而使篡改与降级攻击立即暴露。

- 符号 	 长度 	 说明   D 	 1 字节 	 dp id   T 	 4 bit 	 dp type   L 	 4 bit 	 dp len，表示 dp data 的长度   D 	 dp len 字节 	 dp data   T 	 4 字节 	 timestamp，Unix 时间戳，可选，「Dp Data Include V2」为 1 时存在该字段

### 注：一次 beacon 广播包含一个或多个 DTLD 数据，不同 DTLD 数据的 dp id 不可相同，多个 DTLD 数据共用一个 timestamp。

以下三项均为 HMAC 计算结果，不参与任何加解密。

| 凭证 | 算法 | 用途 |
|---|---|---|
| RegisterKey | HMAC-SHA-256_auth_key( serverRandom ) | 设备向云端证明本机在场且持有 auth_key。经 0x1001 响应交给中心侧转呈云端 |
| verify_key | HMAC-SHA-256_auth_key( L(sRandom) \|\| 0x1002 中全部云端签发字段 ) | 云端向设备证明 0x1002 所载密钥确由云端签发。<br>完整字段顺序与编码规则见「配对请求：0x1002（V6）」 |
| 广播 ID 字段 | HMAC-SHA-256_secret_key( device_uuid ) | 已绑定设备的广播标识，避免外泄明文 PID，静态值 |

## 6.3 数据部分（分包协议中组装后的数据部分）

广播数据段描述
	
类型
	
说明


Manufacturer data

	

0xFF
	
长度：0x17或者0x0E
类型：0xFF 
数据 ：
 - COMPANY ID:0x07D0
 - 加密方式：0x00
 - 通信能力：0x0000
 - FLAG：0x00
 - ID字段:  见下述介绍



Custom Complete Local Name

	
0x09

	
长度：len(最大15个字节)
类型：0x09   
数据：自定义

![Feishu Docs - Image](blob:https://icn602w9tnqf.feishu.cn/d176080a-e016-49f2-a25d-1ac02e8cd05a)

### 0x00-基于auth key和device uuid的一般加密；

![Feishu Docs - Image](blob:https://icn602w9tnqf.feishu.cn/6a3b5299-12ab-4dd5-a691-bfb765b3819f)

- • Key_type：   Key_type 	 密钥 	 用途 	 适用流程 	 mode_0 	 mode_1 	 mode_2   0 	 —  	 明文，无 MIC，仅允许承载 0x1000,其它功能码以 Key_type=0 到达一律拒收 	 握手 	 ✓ 	 ✓ 	 ✓   1–16 	 — 	 废弃（6.0 之前版本使用） 	 — 	 ✗ 	 ✗ 	 ✗   17–20 · 23 · 27 	 — 	 保留 	 — 	 ✗ 	 ✗ 	 ✗   21 	 secret_key_21 	 配网握手密钥（不含ECDH） 	 配网 	 ✓ 	 ✗ 	 ✗   22 	 secret_key_22 	 配网握手密钥（含 ECDH） 	 配网 	 ✗ 	 ✓ 	 ✓   24 	 secret_key24_session 	 会话密钥（不含 ECDH） 	 mode_0 全程使用；mode_1 重连时使用 	 ✓ 	 ✓ 	 ✗   25 	 secret_key25_session 	 会话密钥（含 ECDH） 	 mode_1配网完成后使用；mode_2全程使用 	 ✗ 	 ✓ 	 ✓   26 	 secret_key26 	 离线解绑密钥（不含ECDH） 	 离线解绑；mode_0/1/2通用 	 ✓ 	 ✓ 	 ✓

> 注解：
> 1.
> Key_type 按「密钥的派生方式」编号，不按模式编号 —— 21 / 22 是配网握手密钥的无 ECDH / 含 ECDH 两种形态，24 / 25 是会话密钥的两种形态，26 是离线解绑密钥。
> 2.
> security_mode_1 的设备必须同时支持 24 与 25 —— 配网那次连接结束后使用 25（含 ECDH），之后每次重连使用 24（不含 ECDH）。

- • Key_type 白名单

接收侧仅允许 0（且功能码必须为 0x1000）、21、22、24、25、26 六种取值。其余取值（1-16、17-20、23、27 以及任何未定义值）一律丢弃报文并断开连接。

并且必须按本机配置的 security_mode 进一步收窄，收到不属于本模式的 Key_type 一律拒收并断开连接：

| security_mode | 允许的 Key_type |
|---|---|
| 0 | 0（仅 0x1000）、21、24、26 |
| 1 | 0（仅 0x1000）、22、25、24、26 |
| 2 | 0（仅 0x1000）、22、25、26 |

0x08-用于扫码配网的基于auth key和device uuid的一般加密（ID字段带FLAG2）；

1. 1. Key_type=0 是握手所必需的合法明文通道，若不限制其可承载的功能码，攻击者可用明文报文直接下发业务指令，从而完全绕过加密与认证层。

1. 2. 按模式收窄是「固件配置好了加密方式就绝不能接受其它模式的流程」这一原则在报文层面的落地 —— mode_0 设备必须拒收 22 与 25（那是含 ECDH 的形态），mode_2 设备必须拒收 21 与 24（那是不含 ECDH 的形态）。

- • SeqNum：包序号，4 字节大端序。发送端和接收端各自独立维护一个 SeqNum。 ◦ 发送 SeqNum：每次新的连接初始值为 1，每发送一包指令依次滚动加 1，溢出直接断开连接。 ◦ 接收 SeqNum：记录已成功接收的最大包序号，每次新的连接初始值为 0。收到一包时，必须校验报文中的 SeqNum 严格大于本地接收 SeqNum，否则丢弃该包（判定为重放或乱序）；校验通过并解密成功后，将本地接收 SeqNum 更新为该值。因发送侧初始值为 1、接收侧初始值为 0，故连接建立后的第一包恰好满足该条件。 ◦ 明文报文与接收 SeqNum：Key_type=0 的明文报文没有 MIC，其 Header 中的 SeqNum 可被中间人任意改写（若照常用它更新本地接收 SeqNum，攻击者只需把 0x1000 报文的 SeqNum 改成一个极大值，后续所有合法加密报文都会因「不大于本地值」而被丢弃，握手必然失败）。因此：本地接收 SeqNum 只能由解密并通过 MIC 校验的加密报文推进，Key_type=0 的明文报文不参与接收 SeqNum 的校验，也不得更新本地接收 SeqNum。发送侧的 SeqNum 仍照常占号：0x1000 握手报文使用 SeqNum=1，本连接的首个加密报文使用 SeqNum=2。

- bit1：已配网的高级加密设备每次连接时是否需要和云端进行双向认证，1-需要，0-不需要。

- • Length：长度字段，2 字节大端序，Payload + MIC（如果存在）总长度，不含 Header。接收侧必须在解密之前校验 Length 与实际收到的报文长度一致，不一致直接丢弃，不得先按 Length 切分或拷贝数据再校验（否则存在越界读风险）。

### 适用于蓝牙网关，手机 APP 无需关心此位。

![Feishu Docs - Image](blob:https://icn602w9tnqf.feishu.cn/410cfa92-7b94-4e2a-95c6-d8ef6f72955c)

bit4：WIFI-BLE combo设备配网优先级标志，0 – 优先通过WIFI配网，1 – 优先通过蓝牙配网。

- • 功能码：除配网和重连流程使用以下新增指令外，其他业务复用原有指令码及数据格式。

- bit6：表示wifi/BLE设备是否具有beacon通信能力，1表示有，0表示没有。

- • CRC16：计算从功能码到 Data 所有字节的校验值；复用老版本协议算法。

### 6.3.3 加密与认证参数

| 项 | 值 |
|---|---|
| 加密算法 | AES-CCM-128 |
| 密钥 | 由 Header.Key_type 指定，详见密钥派生方式 |
| Nonce[13] | 共13字节，构造：nonce_iv[8] ‖ Dir[1] ‖ SeqNum[4] |
| AAD[4] | 共4字节，构造：Key_type[1] ‖ RFU[1] ‖ Length[2] |
| nonce_iv | 8 字节，「与密钥同源派生，见密钥派生一节」 |
| MIC | Message Integrity Check，16 字节 |
| 加密对象 | Payload 全部字节（功能码 ‖ 数据长度 ‖ Data ‖ CRC16） |
| 输出 | 密文（长度等于 Payload）‖ MIC(16) |

Nonce 构造：Nonce[13] = nonce_iv[8] ‖ Dir[1] ‖ SeqNum[4]；同一把密钥下，Nonce 在三个维度（跨连接、收发方向、连接内）上同时唯一。

| 字段 | 长度 | 来源 |
|---|---|---|
| nonce_iv | 8 | 与密钥同源派生：HKDF 输出 24 字节切分为 key[16] ‖ nonce_iv[8]，见密钥派生 |
| Dir | 1 | 0x01 下行（Central → Peripheral）；<br>0x02 上行（Peripheral → Central） |
| SeqNum | 4 | 取自本报文 Header 中的 SeqNum，大端序 |

AAD 构造：AAD[4] = Key_type[1] ‖ RFU[1] ‖ Length[2]

SeqNum 已通过 Nonce 参与认证，不重复计入 AAD。至此 Header 的全部 8 个字节均在认证保护范围内 —— 篡改其中任一字节都将导致 MIC 校验失败。

Bit3-表示是否具有wifi5G通信能力【仅适用于双模】，1表示有，0表示没有（App代码有判断）。

- Bit4-表示是否具有zigbee通信能力，1表示有，0表示没有。

- Bit5-表示是否具有NB通信能力，1表示有，0表示没有。

Bit6-表示是否具有 cellular 通信能力，1 表示有，0 表示没有其他位保留。其他位保留。

### Bit7-表示是否具有wifi2.4G通信能力【仅适用于ble+x设备形态】，1表示有，0表示没有。

接收侧在进入任何流程之前，必须先按 0x1000 的 Flags 与本机绑定状态判定本次属于哪条流程，

判定表与规范性要求见「安全握手：0x1000（V6）」。该判定必须在任何密钥派生与 ECC 运算之前完成。

### 6.4.1 Security_mode_0 配网流程

![Feishu Docs - Image](blob:https://icn602w9tnqf.feishu.cn/ed6976b9-5955-47c6-a315-acccbc278662)

### 当加密方式是0x00、0x04、0x05时，该字段存放加密的16字节DEVCIE UUID， 加密算法AES-CBC，加密key为service data字段中的全部data（不包含service uuid）计算MD5，IV等于key。

![Feishu Docs - Image](blob:https://icn602w9tnqf.feishu.cn/134f864f-bd73-42d8-ad71-15c88c8bd277)

### 6.4.3 Security_mode_0/1 重连流程

![Feishu Docs - Image](blob:https://icn602w9tnqf.feishu.cn/e970a234-eb4c-4049-82f0-e78859387190)

### 6.4.4 Security_mode_2 重连流程

![Feishu Docs - Image](blob:https://icn602w9tnqf.feishu.cn/0870718b-b39a-4a84-9645-2864e7615a55)

### 6.4.5 Security_mode_0/1/2 离线解绑流程

![Feishu Docs - Image](blob:https://icn602w9tnqf.feishu.cn/f1376292-66b5-4ada-ba61-a3f06faac22f)

- 广播数据段描述  	 类型  	 说明    设备 LE 物理连接标识   	 0x01  	 长度：0x02    类型：0x01    数据：0x06      Service UUID  	 0x02  	 长度：0x03    类型：0x02    数据：0xFD50   Service Data 	 0x16 	 长度：LEN（可变,最大 20+3 字节） 类型：0x16   UUID：0xFD50 DATA：格式见下表

- • 云端只在该 device_uuid 处于「已解绑待清除」状态时才下发 resetAuthValue；serverRandom 一次性使用、有有效期。

- 序号 	 长度 	 字段 	 必备(M) / 可选(O) 	 说明   1 	 2 	 Frame Control 	 M 	 控制位，大端格式   2 	 10 or 18 	 ID 	 O 	 Product id或者Product key   3 	 1 	 DP Control  	 O 	 DP点数据控制位   4 	 4 	 Frame Counter 	 O 	 帧序号用于防重放，大端格式   5 	 最大13 	 DP Data 	 O 	 DTLD   6 	 4 	 MIC 	 O 	 Message Integrity Check，如果dp data加密则为必选字段，大端格式

- • 解绑响应发送完成后由设备断开连接；「手机 / 网关」侧超时未收到响应或状态为失败也要断开。

- Bit 	 字段 	 说明   0-1 	 Reserved 	 保留   2 	 Security_V2 	 支持一般加密V2协议   3 	 Security_V2_Confirmed 	 执行一般加密V2协议状态（默认0表示未执行V2协议，1表示已执行V2协议）   4 	 Gateway Connect Mode 	 0-短连接（按需连接）；1-长连接，用于网关   5-6 	 Connectivity 	 0-同时支持手机和网关；1-只支持网关；2-只支持手机   7 	 Dp Data Include 	 是否包含dp data，如果为1需同时包含DP Control 和Frame Counter，如果加密还需要包含MIC字段。   8 	 ID Include 	 0-不包含ID字段；1-包含ID字段   9 	 Request Connection 	 1-请求连接，0-未请求连接。   10 	 Shared Flag 	 共享类标志，1-涂鸦共享类设备，0-非共享类设备。   11 	 Bound Flag 	 绑定标志，0-未绑定，1-已绑定。   12-15 	 Version 	 协议版本号，当前为V4。

### ID字段：ID Type(1字节) + ID Len(1字节)  + ID（8或者16字节）。

ID Type：0-Product ID；1-Product key；

| # | 校验项 | 内容 |
|---|---|---|
| 1 | 长度校验（必须在解密之前） | Header.Length 与实际收到的报文长度一致；不得先解密、或先按 Length 切分拷贝再校验 |
| 2 | Key_type 与明文指令白名单 | Key_type 必须在白名单内且属于本机模式；Key_type=0 时功能码必须为 0x1000 |
| 3 | SeqNum 校验 | 加密报文 SeqNum 必须严格大于本地接收 SeqNum，且只在解密与 MIC 校验全部通过后才更新；明文报文不参与 |
| 4 | 解密与 MIC 校验 | 21、22、24、25、26 必须通过 AES-CCM 与 MIC 校验 |
| 5 | Payload 长度校验 | Header.Length = 2+2+DataLength+2+16(MIC)；0x1000 还须校验 Flags 声明的可选字段与实际 Data 长度自洽 |
| 6 | 握手流程合法性校验 | 按分发表用 (Bit7, Bit6, 绑定状态) 判定流程；组合非法或与本机 security_mode 不符即拒收。须在密钥派生与 ECC 之前完成 |
| 7 | 连接状态与允许的 Key_type | 握手前仅 0 / 0x1000；握手后按分发结果限制；认证完成时点 = 成功解密并校验通过本连接首个加密报文 |
| 8 | 功能码限制 | Key_type=26 仅允许承载功能码 0x0014 / 0x002A（设备异常解绑V2指令） |
| 9 | 公钥合法性校验 | 强制 |
| 10 | 临时私钥处理 | 用完立即清零，每连接重新生成，绝不复用 |

### 注：PID/PRODUCT_KEY：未绑定设备该字段存放明文PID或PRODUCT KEY，已绑定设备该字段存放加密数据，加密数据生成算法为AES-ECB（device_uuid），AES-ECB key由MD5(16字节secret_key)组成。

### DP Control定义：

- Bit 	 字段 	 说明 	 备注   0-4 	 Reserved 	 保留 	    7-5 	 Encrypted Type 	 0-未加密； 1-login key； 2-beacon key； 3-xxtea (wifi_ble S2)； 4-xxtea (wifi_ble S3)； 其他-保留。  	 3/4介绍见遥控器协议文档

- • 秘钥2：secret_key_2= MD5（secret_key_1,dev_rand），dev_rand：设备每次连接产生的随机数。

- DP Data：格式为DTLD (DP ID一个字节、DP TYPE和DP LEN总共一个字节（各4bits）、DP DATA加密时最大7个字节，不加密时最大11字节)。

- 注：一次beacon广播只能包含一个DP点数据，如果有多个DP点数据需要广播，需要分多次广播，每次广播持续10次，间隔100ms。

- • 秘钥5：session_key = MD5(login_key,dev_rand)，传输密钥。

- 加密算法为AES-CCM-128，加密内容为DP Data，iv取2次Frame Control和Frame Counter组成12字节，加密key取Encrypted Type指定的加密元素的MD5。

- • 密钥12：secret_key_12 = MD5(secret_key_11+dev_rand)

- 3.5.2 扫描响应包内容

- 广播数据段描述 	 类型 	 说明   Manufacturer data  	  0xFF 	 长度：0x17或者0x0E 类型：0xFF  数据 ：  - COMPANY ID:0x07D0  - 加密方式：0x00  - 通信能力：0x0000  - FLAG：0x00  - ID字段:  见下述介绍    Custom Complete Local Name  	 0x09  	 长度：len(最大15个字节) 类型：0x09    数据：自定义

- • 密钥15：session_key_enc = MD5(16字节login_key + 16字节secret_key + dev_rand)

- • 密钥16：secret_key_16 = MD5(auth key+ md5(device uuid) + server rand)

设备ID（device_uuid）和授权码(auth_key)分别储存在设备端和云端，并且具有一一映射关系，在设备绑定前，设备会以广播的形式将设备UUID的通知给APP，APP通过解密后的设备UUID向云端请求secret_key_1/ secret_key_11。

dev_rand：设备随机数，每次蓝牙连接时，设备产生一组随机数，app使用secret_key_1/secret_key_11 (未绑定)或者secret_key_4/secret_key_14 (已绑定)加密的查询设备信息指令获取该dev_rand。

Verify_key:验证V2下发的密钥正确性，验证失败将无法使能V2协议使能位。加密算法如下：

Verify_key = MD5(16字节login_key+16字节secret_key+authkey)前四字节

对于已绑定的设备，每次新连接，app通过secret_key_4/secret_key_14加密“获取设备信息指令”获取dev_rand，之后所有的通信采用session_key加密。

### 0x06-基于auth key和device uuid的一般加密（ID字段带FLAG2）；

通信数据加密采用AES128-CBC NoPadding 。

### 0x08-用于扫码配网的基于auth key和device uuid的一般加密（ID字段带FLAG2）；

数据部分包括1个字节的加密标识，16个字节的加密IV以及“3.1节描述的帧数据”的明文或者密文。

| 1字节 | 16字节 | N字节 |
|---|---|---|
| 加密FLAG | AES-CBC IV | Payload的明文或者密文 |

bit0：DEVICE UUID是否是压缩的，1表示压缩，0表示非压缩。

bit1：已配网的高级加密设备每次连接时是否需要和云端进行双向认证，1-需要，0-不需要。

bit2：设备在线逻辑标志，0 – 采用标准功耗设备在线策略，1- 采用低功耗设备在线策略，目前只

适用于蓝牙网关，手机 APP 无需关心此位。

bit3：WIFI-BLE combo设备是否支持Plug&play配网协议标志，0 – 不支持，1- 支持。

bit4：WIFI-BLE combo设备配网优先级标志，0 – 优先通过WIFI配网，1 – 优先通过蓝牙配网。

bit5：是否支持获取设备的WIFI列表，0 – 不支持，1 – 支持。

bit6：表示wifi/BLE设备是否具有beacon通信能力，1表示有，0表示没有。

bit7： 绑定标志位，手机APP无需关心此位。

12-secret_key_12

13-secret_key_13

通信能力(2个字节大端格式，高字节在前)：

bit0-表示是否通过BLE注册，1表示通过BLE注册绑定设备，0表示其他通道。

Bit1-表示是否支持MESH，1表示支持，0表示不支持。

Bit2-表示是否具有wifi2.4G通信能力【仅适用于双模】，1表示有，0表示没有（App代码有判断）。

Bit3-表示是否具有wifi5G通信能力【仅适用于双模】，1表示有，0表示没有（App代码有判断）。

### Bit4-表示是否具有zigbee通信能力，1表示有，0表示没有。

| 序号 | 长度 | 字段 | 说明 |
|---|---|---|---|
| 1 | 4 | SN | 发送方SN |
| 2 |  |  |  |
| 3 |  |  |  |
| 4 |  |  |  |
| 5 | 4 | ACK_SN |  |
| 6 |  |  |  |
| 7 |  |  |  |
| 8 |  |  |  |
| 9 | 2 | 功能码 |  |
| 10 |  |  |  |
| 11 | 2 | 数据长度len |  |
| 12 |  |  |  |
| 13~(13+len) | len | 数据 |  |
| 13+len | 2 | CRC16 | 检验 |
| 14+len |  |  |  |

Bit6-表示是否具有 cellular 通信能力，1 表示有，0 表示没有其他位保留。其他位保留。

- • SN：app和设备端各维护自己的SN，每次新的连接初始值为1，每发送一包指令依次滚动加1，溢出复位重新赋值1。

- Bit8-表示是否具有wifi5G通信能力【仅适用与ble+x设备形态】，1表示有，0表示没有。

- Bit9-表示是否具有有线通信（Ethernet，以太网）能力，1表示有，0表示没有。

- Bit10-表示设备是否具有BLE long range（Coded PHY）通信能力，1表示有（同时表明设备存在扩展广播），0表示没有。

- • 校验字：本协议采用CRC16校验，校验内容包括SN、ACK_SN、功能码、数据长度、数据4个部分。

## 7.5 通信流程与描述

### 7.5.1 设备发现

设备发现过程如下：BLE 设备在未连接状态下，会间断发送广播包。手机 APP 只对广播包中含涂鸦特定服务 UUID的蓝牙设备发送扫描请求处理，蓝牙设备在扫描广播应答包中发送设备的product_id ，device_id 以及绑定标志和版本信息，手机APP 通过device_id 从服务端拉取信息，通过product_id从服务端拉取UI 以及一些产品信息。

### 当加密方式是0x06、0x07、0x08时，该字段存放1字节FLAG2 再加 n字节data （取决于FLAG2的bit1-bit0）：

#### 7.5.2.1 一般加密（V1 & V2）

bit1 – bit0 （值0-3）：为0时，data部分为涂鸦分配的明文MAC地址（大端格式），其他值保留。

![Feishu Docs - Image](blob:https://icn602w9tnqf.feishu.cn/4fd6a5f8-e3f2-432e-8456-8ae890c9df25)

V2

![Feishu Docs - Image](blob:https://icn602w9tnqf.feishu.cn/4ceae14f-2e5d-49fe-abc1-76298b675806)

1. 1. 手机app扫描设备广播包，并通过广播包中解密出来的device_uuid从服务器拉取对应的secret_key_1/ secret_key_11，之后与设备建立连接。

1. 2. app发送通过secret_key_1/secret_key_11加密的“获取设备信息”指令到设备。

1. 3. 设备根据自身绑定状态选用对应的解密方式解密指令，如果是已绑定状态选用secret_key_4 /secret_key_14解密指令，如果是未绑定状态选用secret_key_1/secret_key_11解密指令，之后响应指令，响应指令采用secret_key_1/secret_key_11或者secret_key_4/secret_key_14加密，响应内容包含设备产生的dev_rand、设备版本号，协议版本号、设备安全等级以及设备绑定标志和auth_key的hmac值等。

1. 项 	 Write Characteristic 	 Notify Characteristic   UUID 	 00000001-0000-1001-8001-00805F9B07D0 	 00000002-0000-1001-8001-00805F9B07D0   说明 	 中心侧 → 设备 	 设备 → 中心侧   属性 	 Write Without Response 	 Notify   数据长度 	 ATT MTU − 3 	 ATT MTU − 3

1. 5. 设备使用secret_key_2/secret_key_12解密配对请求指令，首先判断其中的device_uuid是否和自身匹配，如果不匹配回复失败，并断开蓝牙连接，如果匹配回复绑定成功，响应指令采用session_key加密。

> 注：已绑定的设备通过secret_key_4/secret_key_14加密发送“获取设备信息指令”来得到设备的dev_rand 。

#### 7.5.2.2 高级加密（安全芯片）

高级加密主要用于要求级别高的设备，比如蓝牙门锁。

![Feishu Docs - Image](blob:https://icn602w9tnqf.feishu.cn/b938eee5-0f11-4060-a6ce-1065772b1606)

1. 符号 	 长度 	 说明   T 	 1 字节 	 Type   L 	 1 字节 	 Length   D 	 Length 字节 	 Data

1. 当T = 0时，D的内容是当前广播数据的全部内容，包括广播包和扫描响应包。T = 其他值未定义，保留。

1. 3. 设备根据自身绑定状态选用对应的解密方式解密指令，如果是已绑定状态选用secret_key_4解密指令，如果是未绑定状态选用secret_key_1解密指令，之后响应指令，响应指令采用secret_key_4或者secret_key_1加密，响应内容包含设备产生的dev_rand、设备版本号，协议版本号、register key等，设备安全等级以及设备绑定标志等。

1. ATT_MTU 指一次 ATT 操作可传输的最大数据量，默认值 23 字节。扣除 ATT opcode 1 字节与 attribute handle 2 字节后，单包可用 20 字节 —— 这就是未经协商时单包最大数据长度为 20 的原因。

1. MTU 交换自 ATT 协议引入即支持，通常在连接建立后立即进行，主从双方取较小值作为本次连接的 ATT_MTU。Bluetooth 4.2 引入 Data Length Extension，链路层单包上限提升后，较大的 ATT_MTU 才具备实际吞吐收益。

1. 6. 设备端收到云端证书公钥和对应签名后，使用TUYA HARDWARE CA公钥验证，验证通过后生成6字节随机数发送给app。

1. 7. app收到随机数后转给云端进行签名，并返回随机数和对应签名给设备。

1. 手机端蓝牙MTU交换在蓝牙建立连接后会自动进行，无需上层应用干预，但是上层应用需要拿到交换的结果ATT MTU值，然后通过获取设备信息指令将（ATT MTU - 3）发送给设备，之后的通信就采用该值作为每次写操作的最大值。

1. 9. 设备发送设备证书到app，app发给云端校验，云端校验成功后生成6字节随机数发给app，app将随机数发给送设备。

1. 10. 设备使用设备证书私钥对随机数进行签名，并回传给app，app发送给云端进行验证，云端使用设备证书的公钥进行验证。

1. 11. 云端验证通过后，app使用register key注册设备，获取login key。

1. 12. app发起配对请求指令，配对请求指令中包含设备的device_uuid和login_key，配对指令采用secret_key_2加密。

1. 13. 设备使用secret_key_2解密配对请求指令，首先判断其中的device_uuid是否和自身匹配，如果不匹配回复失败，并断开蓝牙连接，如果匹配回复绑定成功，响应指令采用session_key加密。

> 注：已绑定的设备通过secret_key_4加密发送“获取设备信息指令”来得到设备的dev_rand 。

### • 包序号以及包长度，单字节最大值为 01111111(127)，若第八位(最高位)为 1，表明还有后续字节存在。包序号或包长度最多只有 4 字节长度表示，所以最大四字节包序号或包长度： 0xff 0xff 0xff 0x7f(十进制:268435455 包或字节数)。

字节数 
	
起始 
	
末尾 


1 
	
0(00) 
	
127(7f) 


2 
	
128(80 01) 
	
16383(ff 7f) 


3 
	
16384(80 80 01) 
	
2097151(ff ff 7f) 


4 
	
2097152(80 80 80 01) 
	
268435455(ff ff ff 7f)

注：128进制倒序表示法，例如0x8001 = 0 + 1x(0x80) = 128;  0xff7f = 0x7f + 0x7f x 0x80 = 16383。

3、设备在绑定状态，而app处于解绑状态：物理解绑或者app查询已存储在云端的信息，然后发送异常解绑指令解绑，其中针对V2协议的异常解绑采用Key16进行加解密并解绑设备。

4、针对支持一般加密V2协议设备，解绑移除后需清除当前执行V2协议状态（如：通过支持V2协议App切换密钥后，我们将执行状态bit置1，当任意方式移除该设备后，我们需清除该bit为0）

## 5.2 SPP链路传输层协议

整个加密链路使用device_uuid与auth_key成对这种特质进行加密，避免了密码传输这一过程。

SPP（Serial Port Profile）基于蓝牙的RFCOMM协议实现，本质上提供的是面向流的串行数据传输服务。这种流式传输机制决定了数据在传输过程中没有天然的消息边界概念。

在实际传输中，应用程序多次调用发送函数写入的数据，可能被操作系统或协议栈在缓冲区中合并为单个数据块进行发送：同样的，接收端一次读取操作可能只获取到完整数据包的一部分，或者包含多个数据包。由于边界不明确，可能出现：

为了防止截取复制内容攻击，每次连接会有随机码加严。每次连接都会有不同的数据内容。

### • 粘包问题：多个数据包合并成一次接收

本章各功能码指令仅描述 Data 字段部分，完整Payload格式见各协议版本安全层中的「Payload格式」定义：

- • V6 格式

- 原分包协议中，因sub_pkg中仅有序号+数据，没有长度字段，在SPP链路上对帧的重组就会遇到问题。因此在SPP传输之上增加一层应用协议成为必然选择，这层协议需要解决：

•
帧定界：通过特定的帧头标识或长度字段明确每个数据帧的边界

### • 完整性校验：添加校验机制确保数据传输的完整性

这种协议层的添加，将SPP的"字节流"传输转换为"消息流"传输，为上层应用提供可靠、有序、完整的数据传输服务。

Data：

| Flags<br>1字节 | dataMTU<br>2字节 | mRandom<br>16字节 | serverRandom<br>16字节，可选 | mPublicKey<br>64字节，可选 |
|---|---|---|---|---|

1.
要发送的空中帧按分包协议分成了 n 个小包发送；

2.
在 SPP 流式通信链路上，对每个小包再套一层协议封装后进行传输;

如下图示意： 原始数据被分为 P1、P2、P3；每个小包在链路上封装为：55AA + 对应数据 + sum8。

bit5-0：保留，必须为0。

各流程的 Flags 取值如下表所示（APP主机 侧据此填写；设备侧的流程判定见下）：

| 流程 | 适用security_mode | Flags | bit7 | bit6 | Data组成 |
|---|---|---|---|---|---|
| 配网 | 0 | 0x80 | 1 | 0 | Flags \|\| dataMTU \|\| mRandom \|\| serverRandom |
| 配网 | 1、2 | 0xC0 | 1 | 1 | Flags \|\| dataMTU \|\| mRandom \|\| serverRandom \|\| mPublicKey |
| 重连 | 0、1 | 0x00 | 0 | 0 | Flags \|\| dataMTU \|\| mRandom |
| 重连 | 2 | 0x40 | 0 | 1 | Flags \|\| dataMTU \|\| mRandom \|\| mPublicKey |
| 离线解绑 | 0、1、2 | 0x80 | 1 | 0 | Flags \|\| dataMTU \|\| mRandom \|\| serverRandom |

偏移
	
长度
	
字段
	
说明


0
	
1
	
帧同步头1
	
0x55


1
	
1
	
帧同步头2
	
0xAA


2
	
1
	
版本号
	
协议升级扩展用，默认0x00


3
	
1
	
LEN_H
	
数据长度 高8位


4
	
1
	
LEN_L
	
数据长度 低8位


5
	
1
	
hdr_check
	
头部校验信息，XOR+Magic


…
	
N
	
数据Data
	
-


…
	
1
	
sum8
	
和检验

接收侧必须用 (Flags.Bit7, Flags.Bit6, 本机绑定状态) 三者共同判定本次握手属于哪条流程，并据此选择密钥派生路径：

![Feishu Docs - Image](blob:https://icn602w9tnqf.feishu.cn/da0358f1-b81b-446d-870e-805cdd93081c)

•
hdr-check：headr头部信息校验，对头部5个数据做异或，算法：0x55 ^ 0xAA ^ 版本号^LEN_H ^ LEN_L ^ 0x42;

1. 1. 配网与离线解绑的 Flags 取值相同（Bit7=1、Bit6 随模式），二者只能靠本机绑定状态区分：未绑定视为配网，已绑定视为离线解绑。

1. 2. 离线解绑流程不使用 ECDH，三种模式共用同一条流程与同一把 key26。因此 security_mode_2 的设备在离线解绑流程中也必须接受 Bit6=0 的 0x1000 —— 这是 mode_2 唯一允许 Bit6=0 的场合。

1. Code block C UINT8_T tal_util_check_sum8(UINT8_T* buf, UINT32_T size) {     UINT8_T sum = 0;     for (UINT32_T idx=0; idx<size; idx++) {     sum += buf[idx];     }     return sum; }

1. 4. 该判定必须在任何密钥派生与 ECC 运算之前完成，避免为一个伪造的 0x1000 白做椭圆曲线运算。

6.
V6 安全层

响应帧 BLE->APP：

6.1.1
三档 security_mode

| Flags<br>1字节 | sRandom<br>16字节 | sPublicKey<br>64字节，可选 |
|---|---|---|

security_mode
	
配网
	
重连


0
	
无
	
无


1
	
有
	
无


2
	
有
	
有

bit7：描述sPublicKey字段是否存在标志位。

步骤
	
判断
	
结果


1
	
设备端能否实现 P-256？
- 有 ECC 硬件加速的 SoC 直接算能；
- 纯软件实现需实测标量乘耗时、栈深与 flash 占用后再定
	
不能 → mode_0（唯一可选）
能 → 进入第 2 步


2
	
是否同时满足下列两条：
① 日常重连流量不敏感（不含门锁开关记录、人体存在状态等可推断隐私的数据）
② 重连极频繁，且对连接建立时延敏感
	
同时满足 → mode_1
否则 → mode_2（默认）

⚠ mode_1 的 ECC 代码尺寸与 mode_2 相同 —— 其配网同样要跑 ECDH，故设备端同样需实现完整 P-256，节省的只是每次重连的运算时间，不是空间。

| 流程 | 适用security_mode | Flags | Data组成 |
|---|---|---|---|
| 配网 | 0 | 0x00 | Flags \|\| sRandom |
| 配网 | 1、2 | 0x80 | Flags \|\| sRandom \|\| sPublicKey |
| 重连 | 0、1 | 0x00 | Flags \|\| sRandom |
| 重连 | 2 | 0x80 | Flags \|\| sRandom \|\| sPublicKey |
| 离线解绑 | 0、1、2 | 0x00 | Flags \|\| sRandom |

## 8.2 查询设备信息：0x1001（PV6）

•
设备固件：唯一权威。设备只接受本档的流程，收到其它档的 Key_type 或 Flags 组合一律拒收断连。

•
广播 Manufacturer data（4 bit）：仅作连接前提示，不得作为发起依据。广播中该值为明文且不可认证，可被伪造。但这不构成降级 —— 设备只认自身固件配置，攻击者伪造广播只能造成握手失败（可用性），无法使双方回落到较弱流程。中心侧不得因握手失败而自动回落到更低模式重试。

| device_uuid<br>16字节 | SRC_TYPE<br>1字节 | AppFlagLength<br>1字节 | AppFlag<br>n字节 |
|---|---|---|---|

- • SRC_TYPE：发送源。 ◦ 0 – 不支持； ◦ 1 – 安卓APP； ◦ 2 – 网关； ◦ 3 – 小程序； ◦ 4 – 门锁配件； ◦ 5 – IOS APP； ◦ 6 – 主从一体主设备； ◦ 其他保留。

- 出厂预置，设备端与云端密钥库各持一份，终生不变，离线解绑时亦须保留。永不经蓝牙传输，永不下发给手机或网关。

- 从 	 经 	 到 	 内容 	 说明   根 	 — 	 — 	 设备 + 云端 	 auth_key 	 出厂预置，两侧各持一份，终生不变。永不经蓝牙传输，永不下发给手机或网关   分支一 	 云端 	 — 	 手机 / 网关 	 authValue 	 HKDF(auth_key, salt=serverRandom)。中心侧拿到的是派生物，无法反推根   分支二 	 云端 	 手机 	 设备 	 login_key· secret_key· beacon_key· … 	 配网时由 0x1002 转交。设备此后凭它们重连，不再需要云端参与   凭证 ① 	 设备 	 手机 	 云端 	 RegisterKey 	 设备证明本机在场且持有 auth_key（经 0x1001 响应转呈）   凭证 ② 	 云端 	 手机 	 设备 	 verify_key 	 云端证明 0x1002 所载密钥确由云端签发

「经」列只有一个值：手机。auth_key 那一行是唯一不经过手机的 —— 这就是整个信任模型的支点。

6.2
密钥体系

| 协议版本号<br>2字节 | 安全版本号<br>3字节 |  | 加密方式<br>1字节 | Flag<br>1字节 | Flag2<br>1字节 | Flagx长度<br>1字节 | Flagx-预留<br>n字节 | Bond<br>1字节 | RegisterKey<br>32字节 |
|---|---|---|---|---|---|---|---|---|---|
| 固件版本号<br>3字节 | SDK版本字符长度  1字节 | SDK版本字符长度  1字节 | SDK版本字符<br>n字节 | mcu固件版本号<br>3字节 | mcu硬件版本号<br>3字节 | mcu硬件版本号<br>3字节 | Device virtual id length<br>1字节 | 通信能力 | Device virtual id length<br>1字节 |
| WiFi_flag<br>1字节 | Device virtual id | WiFi_flag | 设备能力长度<br>1字节 |  | 设备能力<br>n字节 | n字节 | addr_type<br>1字节 | ble_mac_addr<br>6字节 | addr_type |
| fw_key_len<br>1字节，如果为0表示后面没有fw_key |  | fw_key_len<br>1字节，如果为0表示后面没有fw_key | 1字节，如果为0表示后面没有fw_key |  | fw_key<br>n字节 |  |  |  |  |
| zigbee_mac_len<br>1字节，如果为0表示后面没有zigbee_mac |  | zigbee_mac_len<br>1字节，如果为0表示后面没有zigbee_mac | zigbee_mac_len |  | zigbee_mac<br>n字节 |  |  | zigbee_mac<br>n字节 | n字节 |
| attach_len<br>1字节，如果为0表示后面没有attach_data |  |  |  | attach_len | attach_data<br>n字节 |  |  |  |  |
| PacketMaxSize_len<br>1字节，如果为0表示后面没有PacketMaxSize |  |  | PacketMaxSize_len<br>1字节，如果为0表示后面没有PacketMaxSize | 1字节，如果为0表示后面没有PacketMaxSize | PacketMaxSize<br>n字节 |  |  | PacketMaxSize |  |
| Sl_len<br>1字节，如果为0表示后面没有Sl_value |  | Sl_len<br>1字节，如果为0表示后面没有Sl_value |  |  | Sl_value<br>n字节 | n字节 |  |  |  |
| CombosFlag Length<br>1字节，如果为0表示后面没有CombosFlag | 1字节，如果为0表示后面没有CombosFlag |  |  |  | CombosFlag<br>n字节 | CombosFlag<br>n字节 | n字节 |  |  |

协议版本号：目前是0x0600 (pv6.0)。

密钥
	
来源
	
生命周期
	
用途


auth_key
	
产线预置，设备与云端各一份
	
终生不变，离线解绑亦保留
	
派生 authValue、resetAuthValue；计算 RegisterKey、verify_key。永不经蓝牙传输，永不下发给手机


login_key
	
云端签发，配网时经 0x1002 下发
	
绑定期内不变，重新绑定即更换
	
key24/key25 的 IKM 组成；广播 DP 密钥派生源；resetAuthValue 的 IKM 组成


secret_key
	
同上
	
同上
	
同 login_key；另用作已绑定设备广播 ID 字段的 HMAC 密钥


beacon_key
	
同上
	
同上
	
广播 DP 加密（Encrypted Type = 2）


verify_key
	
同上，32 字节
	
同上
	
仅用于设备校验 0x1002 所载数据确由云端签发，本身不作为加密密钥


device_id
node_id
ble_net_key
	
同上

	
同上
	
身份标识与组网，非加密用途

加密方式security_mode：定义同ADV章节的加密方式

单次连接或单次授权有效，均不落盘。

材料
	
生成方
	
有效范围
	
说明


mRandom / sRandom
	
中心侧 / 设备侧各自生成，16 字节
	
一次连接
	
经 0x1000 交换，见指令


serverRandom
	
云端，16 字节
	
一次授权
	
一次性使用、有有效期


authValue[32]
	
云端派生后下发中心侧；设备自行算出
	
一次配网
	
设备持有 auth_key、又从 0x1000 收到 serverRandom，故无需他方下发


resetAuthValue[32]
	
同上
	
一次解绑授权
	
IKM 含 login_key‖secret_key，故绑定到当前这一代绑定密钥


ecdh_shared[32]
	
双方各自计算
	
一次连接
	
ECDH 结果点的 x 坐标。算出后立即清零临时私钥，见 6.2.3 ECDH


key21/22/24/25/26及各自 nonce_iv
	
双方各自派生
	
一次连接
	
连接结束即失效。云端不持有这些密钥



transcript_hash[32]
	
双方各自计算
	
一次连接
	
SHA-256( 0x1000 请求的 Data ‖ 0x1000 响应的 Data )，详见握手记录哈希

6.2.3
ECDH 参数（security_mode_1/2）

项
	
内容


曲线
	
NIST P-256（secp256r1）


公钥格式
	
64 字节，为未压缩格式。去掉 0x04 前缀后的裸 X ‖ Y，X 与 Y 各 32 字节大端


共享秘密
	
ecdh_shared[32] = ECDH 结果点的 x 坐标，32 字节大端。只取 x 坐标，不得使用 X ‖ Y


公钥合法性校验（强制）
	
收到的点必须满足 P-256 曲线方程、且不是无穷远点。校验失败直接断开连接。不做该校验会导致 invalid-curve attack


临时密钥
	
每次连接必须重新生成临时密钥对，绝不复用；算出 ecdh_shared 后立即将临时私钥清零。临时私钥复用等于作废前向保密

实现提示（非规范性）：设备端在发出 0x1000 明文响应之后即可立即开始计算 ecdh_shared，无需等待对端的下一个报文到达，可把一次标量乘的耗时隐藏在空口往返里。

MCU固件版本号：例如0x010000代表v1.1.0，没有外部mcu或者有外部mcu但是没获取到版本号，则填0 。

本节为规范性要求。随机性不足会直接导致 AES-CCM 的 nonce 复用，从而破坏机密性，因此以下各条均为强制。

适用范围

随机数
	
生成方


mRandom
	
中心侧，每次连接


sRandom
	
设备侧，每次连接


临时 P-256 密钥对
（m_private / s_private）
	
双方各自，每次连接（仅 mode_1 / mode_2）



serverRandom
	
云端，每次授权请求

RegisterKey：32字节，设备向云端证明持有 auth_key，算法详见认证凭证。

上述随机数必须来自真随机数发生器（TRNG），或来自经熵源评估的确定性随机比特发生器（DRBG）（建议符合 NIST SP 800-90A / 90B）。

Device virtual id：设备虚拟id，设备如果没有此虚拟ID，长度字段填0发送。

禁用方式
	
原因


标准库 rand() / srand()
	
可预测，种子空间小


系统时间戳
	
可预测，分辨率有限


MAC 地址或 device_uuid 的变换
	
静态值，完全可预测


上电计数器
	
单调可预测

通信能力：2个字节大端格式，高字节在前，格式详见广播格式章节。

•
mRandom、sRandom、临时密钥对必须每次连接重新生成；serverRandom 必须每次云端请求重新生成。任何形式的复用均不允许。

•
不得预先生成一批随机数缓存起来供多次连接使用。 若 RNG 存在预热或唤醒开销，应在需要时现取，不得以缓存规避。

- • bit6：是否支持语音服务。

- • bit5：WIFI-BLE combo设备是否支持Plug&play配网协议标志，0 – 不支持，1- 支持。

- • bit4：是否需要beaconKey，0-不需要，1-需要（配对请求指令中含beaconKey）。

- Code block C authValue[32]      = HKDF( IKM  = auth_key,                            salt = serverRandom,                            info = "tuyaos-ble-auth-value",  L = 32 )  resetAuthValue[32] = HKDF( IKM  = auth_key ‖ login_key ‖ secret_key,                            salt = device_uuid ‖ serverRandom,                            info = "tuyaos-ble-reset-auth-value",  L = 32 )

- • bit2：0-使用固件版本号1, 1-使用固件版本号2。保留，默认0（已强制使用3位版本号）

- 五把密钥同一个模子，salt 与 L 完全相同，仅 IKM 与 info 不同：

- Code block C HKDF(     IKM  = 〔每把不同，见下表〕     salt = mRandom ‖ sRandom ‖ transcript_hash     info = 〔每把不同，见下表〕     L    = 24 )   L=24 的输出统一切分为 key[16] ‖ nonce_iv[8]

Key_type
	
IKM
	
info


21
	
authValue
	
"tuyaos-ble-key21"


22
	
authValue ‖ ecdh_shared
	
"tuyaos-ble-key22"


24
	
login_key ‖ secret_key
	
"tuyaos-ble-key24_session"


25
	
login_key ‖ secret_key ‖ ecdh_shared
	
"tuyaos-ble-key25_session"


26
	
resetAuthValue
	
"tuyaos-ble-key26"

- • bit7：1 – 内部方案；0 – 外部方案；

- transcript_hash[32] = SHA-256( 0x1000 请求的 Data ‖ 0x1000 响应的 Data )

- • 其中「Data」指 0x1000 请求 / 响应中 Data 字段的完整字节序列，按实际传输的字节取值（含 Flags、dataMTU、随机数以及所有存在的可选字段），不含功能码、DataLength 与 CRC16。10

- • bit4：1 – 支持离线解绑升级方案（可根据前序操作区分是否清除数据）；0 – 不支持；

- • 作用：0x1000 为明文报文，其中的 Flags、dataMTU、公钥等字段不参与 AES-CCM 保护，也不在 AAD 中，可被中间人无声篡改。将 transcript_hash 纳入密钥派生后，握手报文中任何一个字节被改动都会导致双方派生出不同的密钥，后续第一个加密报文的 MIC 校验即失败，从而使篡改与降级攻击立即暴露。

- • 实现提示：无需缓存完整报文，可在收到 / 发出 Data 时增量更新 SHA-256 上下文，握手结束时 finalize。

- • bit1：请求更新long-term keys（默认0表示不请求，1表示请求）。适用于v1老设备升级后主动请求更新密钥。

- 以下三项均为 HMAC 计算结果，不参与任何加解密。

凭证
	
算法
	
用途


RegisterKey
	
HMAC-SHA-256_auth_key( serverRandom )
	
设备向云端证明本机在场且持有 auth_key。经 0x1001 响应交给中心侧转呈云端


verify_key
	
HMAC-SHA-256_auth_key( L(sRandom) || 0x1002 中全部云端签发字段 )
	
云端向设备证明 0x1002 所载密钥确由云端签发。
完整字段顺序与编码规则见「配对请求：0x1002（V6）」


广播 ID 字段
	
HMAC-SHA-256_secret_key( device_uuid )
	
已绑定设备的广播标识，避免外泄明文 PID，静态值

6.3
数据部分（分包协议中组装后的数据部分）

字节序：协议中所有多字节数据均为大端序（高字节在前）。注：MIC是16字节。

设备能力长度：1字节，设备能力长度字段

设备能力：n字节，按bits方式使用 。

- • Key_type：   Key_type 	 密钥 	 用途 	 适用流程 	 mode_0 	 mode_1 	 mode_2   0 	 —  	 明文，无 MIC，仅允许承载 0x1000,其它功能码以 Key_type=0 到达一律拒收 	 握手 	 ✓ 	 ✓ 	 ✓   1–16 	 — 	 废弃（6.0 之前版本使用） 	 — 	 ✗ 	 ✗ 	 ✗   17–20 · 23 · 27 	 — 	 保留 	 — 	 ✗ 	 ✗ 	 ✗   21 	 secret_key_21 	 配网握手密钥（不含ECDH） 	 配网 	 ✓ 	 ✗ 	 ✗   22 	 secret_key_22 	 配网握手密钥（含 ECDH） 	 配网 	 ✗ 	 ✓ 	 ✓   24 	 secret_key24_session 	 会话密钥（不含 ECDH） 	 mode_0 全程使用；mode_1 重连时使用 	 ✓ 	 ✓ 	 ✗   25 	 secret_key25_session 	 会话密钥（含 ECDH） 	 mode_1配网完成后使用；mode_2全程使用 	 ✗ 	 ✓ 	 ✓   26 	 secret_key26 	 离线解绑密钥（不含ECDH） 	 离线解绑；mode_0/1/2通用 	 ✓ 	 ✓ 	 ✓

- 注解： 1. Key_type 按「密钥的派生方式」编号，不按模式编号 —— 21 / 22 是配网握手密钥的无 ECDH / 含 ECDH 两种形态，24 / 25 是会话密钥的两种形态，26 是离线解绑密钥。 2. security_mode_1 的设备必须同时支持 24 与 25 —— 配网那次连接结束后使用 25（含 ECDH），之后每次重连使用 24（不含 ECDH）。

- • bit22: 1 - 该设备是否支持通过BLE链路去管理WiFi信息（例如：WiFi名称和密码，仅双模使用）

- 接收侧仅允许 0（且功能码必须为 0x1000）、21、22、24、25、26 六种取值。其余取值（1-16、17-20、23、27 以及任何未定义值）一律丢弃报文并断开连接。

- 并且必须按本机配置的 security_mode 进一步收窄，收到不属于本模式的 Key_type 一律拒收并断开连接：

- • bit19：1 – 设备通过透传通道收发DP；0 – 设备通过原有通道（0002、0027等）收发DP，适用于蓝牙单点、Combo设备，其中蓝牙单点和Combo的透传通道不同，格式也不同，单点暂不使用，详见“蓝牙直控网关下子设备技术方案”。

- • bit18: 1 – BLE设备支持wifi局域网通信能力；0 – BLE设备不支持wifi局域网通信能力。

- 1. Key_type=0 是握手所必需的合法明文通道，若不限制其可承载的功能码，攻击者可用明文报文直接下发业务指令，从而完全绕过加密与认证层。

- 2. 按模式收窄是「固件配置好了加密方式就绝不能接受其它模式的流程」这一原则在报文层面的落地 —— mode_0 设备必须拒收 22 与 25（那是含 ECDH 的形态），mode_2 设备必须拒收 21 与 24（那是不含 ECDH 的形态）。

- • SeqNum：包序号，4 字节大端序。发送端和接收端各自独立维护一个 SeqNum。 ◦ 发送 SeqNum：每次新的连接初始值为 1，每发送一包指令依次滚动加 1，溢出直接断开连接。 ◦ 接收 SeqNum：记录已成功接收的最大包序号，每次新的连接初始值为 0。收到一包时，必须校验报文中的 SeqNum 严格大于本地接收 SeqNum，否则丢弃该包（判定为重放或乱序）；校验通过并解密成功后，将本地接收 SeqNum 更新为该值。因发送侧初始值为 1、接收侧初始值为 0，故连接建立后的第一包恰好满足该条件。 ◦ 明文报文与接收 SeqNum：Key_type=0 的明文报文没有 MIC，其 Header 中的 SeqNum 可被中间人任意改写（若照常用它更新本地接收 SeqNum，攻击者只需把 0x1000 报文的 SeqNum 改成一个极大值，后续所有合法加密报文都会因「不大于本地值」而被丢弃，握手必然失败）。因此：本地接收 SeqNum 只能由解密并通过 MIC 校验的加密报文推进，Key_type=0 的明文报文不参与接收 SeqNum 的校验，也不得更新本地接收 SeqNum。发送侧的 SeqNum 仍照常占号：0x1000 握手报文使用 SeqNum=1，本连接的首个加密报文使用 SeqNum=2。

- • bit14：设备在线逻辑，0-采用标准功耗设备在线策略；1- 采用低功耗设备在线策略，目前只适用于     蓝牙网关，手机 APP 无需关心此位。

- • Length：长度字段，2 字节大端序，Payload + MIC（如果存在）总长度，不含 Header。接收侧必须在解密之前校验 Length 与实际收到的报文长度一致，不一致直接丢弃，不得先按 Length 切分或拷贝数据再校验（否则存在越界读风险）。

- • bit12-11：Connectivity ，0-同时支持手机和网关；1-只支持网关；2-只支持手机。

- • bit10: 1 – 支持差分OTA；0  – 不支持差分OTA。

- • bit9：1 – 支持挂载配件；0 – 不支持。

- • bit8：是否支持psk，用于wifi多协议设备，1 – 支持；0 – 不支持。

- • bit7：1 – 需要强制LINK层加密；0 – 无需强制LINK层加密。

- • CRC16：计算从功能码到 Data 所有字节的校验值；复用老版本协议算法。

- • bit5：1 – 支持定时；0 – 不支持定时。

- 项 	 值   加密算法 	 AES-CCM-128   密钥 	 由 Header.Key_type 指定，详见密钥派生方式   Nonce[13] 	 共13字节，构造：nonce_iv[8] ‖ Dir[1] ‖ SeqNum[4]   AAD[4] 	 共4字节，构造：Key_type[1] ‖ RFU[1] ‖ Length[2]   nonce_iv 	 8 字节，「与密钥同源派生，见密钥派生一节」   MIC 	 Message Integrity Check，16 字节   加密对象 	 Payload 全部字节（功能码 ‖ 数据长度 ‖ Data ‖ CRC16）   输出 	 密文（长度等于 Payload）‖ MIC(16)

- Nonce 构造：Nonce[13] = nonce_iv[8] ‖ Dir[1] ‖ SeqNum[4]；同一把密钥下，Nonce 在三个维度（跨连接、收发方向、连接内）上同时唯一。

- 字段 	 长度 	 来源   nonce_iv 	 8 	 与密钥同源派生：HKDF 输出 24 字节切分为 key[16] ‖ nonce_iv[8]，见密钥派生   Dir 	 1 	 0x01 下行（Central → Peripheral）； 0x02 上行（Peripheral → Central）   SeqNum 	 4 	 取自本报文 Header 中的 SeqNum，大端序

- AAD 构造：AAD[4] = Key_type[1] ‖ RFU[1] ‖ Length[2]

- SeqNum 已通过 Nonce 参与认证，不重复计入 AAD。至此 Header 的全部 8 个字节均在认证保护范围内 —— 篡改其中任一字节都将导致 MIC 校验失败。

明文报文（Key_type = 0）

Wifi_flag ：适用于wifi/BLE双协议设备。

- • bit0（暂未使用） ： ◦ 0 – 发送配网信息前不发送查询wifi设备当前配网状态指令。 ◦ 1 – 发送配网信息前发送查询wifi设备当前配网状态指令。

- • bit1：  ◦ 0 – 优先通过WIFI配网。 ◦ 1 – 优先通过蓝牙配网。

addr_type：蓝牙mac地址类型，0 – Public Device Address ；1 – Random Device Address；

接收侧在进入任何流程之前，必须先按 0x1000 的 Flags 与本机绑定状态判定本次属于哪条流程，

判定表与规范性要求见「安全握手：0x1000（V6）」。该判定必须在任何密钥派生与 ECC 运算之前完成。

fw_key_len：固件key长度，如果为0表示后面没有固件key。

fw_key：固件key，主要用于涂鸦通用模组固件，一般为8或16字节。

6.4.2
Security_mode_1/2 配网流程

zigbee_mac_len：zigbee mac长度，如果为0表示后面没有zigbee mac。

zigbee_mac：zigbee设备mac，一般为8个字节。

6.4.4
Security_mode_2 重连流程

attach_data：附件信息数据，4字节一个单元，格式如下所示：


附件信息1
	
~
	
附件信息n


channel
1字节
	
channel_attach_version
3字节
	
~
	
channel
1字节
	
channel_attach_version
3字节


◦
channel：通道号，范围是10-19 。
◦
channel_attach_version：对应通道号的附件的当前版本，例如0x010200代表v1.2.0 。
◦
根据attach_len的值来计算有多少个附件以及对应的附件通道号和版本号。

PacketMaxSize_len: 用于表示PacketMaxSize字段长度，如果为0表示后面没有PacketMaxSize。

PacketMaxSize: 应用层分包的最大长度, 需分包发送的命令由该值决定一包传输的大小。

•
resetAuthValue 的 IKM 含 login_key‖secret_key，因此设备一旦重新绑定、密钥换代，旧的解绑授权自动失效，避免「一次合法授权被长期缓存、日后任意时刻重放摧毁绑定关系」。

•
云端只在该 device_uuid 处于「已解绑待清除」状态时才下发 resetAuthValue；serverRandom 一次性使用、有有效期。

•
设备清除 login_key、secret_key、device_id、beacon_key、node_id、ble_net_key 及绑定标志；auth_key、device_uuid、product_id 等出厂预置信息必须保留，否则设备将永久无法再配网。

Sl_value：用于wifi设备，0 – psk ; 1 – ecc ; 2 – mtls（双向认证）; 3 – mtls + 安全芯片 。

•
ADV 中的 encryptedValue 为静态值，只能作为「当前绑定世代」的查询线索，不得作为认证凭据。

CombosFlagLength：用于表示后跟的CombosFlag字段占用的字节数。

CombosFlag：多协议设备能力标志字段，长度取决于CombosFlagLength字段，其中：

- # 	 校验项 	 内容   1 	 长度校验（必须在解密之前） 	 Header.Length 与实际收到的报文长度一致；不得先解密、或先按 Length 切分拷贝再校验   2 	 Key_type 与明文指令白名单 	 Key_type 必须在白名单内且属于本机模式；Key_type=0 时功能码必须为 0x1000   3 	 SeqNum 校验 	 加密报文 SeqNum 必须严格大于本地接收 SeqNum，且只在解密与 MIC 校验全部通过后才更新；明文报文不参与   4 	 解密与 MIC 校验 	 21、22、24、25、26 必须通过 AES-CCM 与 MIC 校验   5 	 Payload 长度校验 	 Header.Length = 2+2+DataLength+2+16(MIC)；0x1000 还须校验 Flags 声明的可选字段与实际 Data 长度自洽   6 	 握手流程合法性校验 	 按分发表用 (Bit7, Bit6, 绑定状态) 判定流程；组合非法或与本机 security_mode 不符即拒收。须在密钥派生与 ECC 之前完成   7 	 连接状态与允许的 Key_type 	 握手前仅 0 / 0x1000；握手后按分发结果限制；认证完成时点 = 成功解密并校验通过本连接首个加密报文   8 	 功能码限制 	 Key_type=26 仅允许承载功能码 0x0014 / 0x002A（设备异常解绑V2指令）   9 	 公钥合法性校验 	 强制   10 	 临时私钥处理 	 用完立即清零，每连接重新生成，绝不复用

- • bit4：1 - 支持查询多网络连接能力；0 - 不支持。

- • bit3：1 - 支持查询设备AP名称；0 - 不支持。

- • 秘钥1：secret_key_1 = AES_CBC(auth_key, server_rand),AES_CBC的IV为全0，key为auth_key的前16个字节，server_rand由云端生成，并在绑定时发送给设备，用于设备计算secret_key_1。

- • 秘钥2：secret_key_2= MD5（secret_key_1,dev_rand），dev_rand：设备每次连接产生的随机数。

- • 秘钥3：secret_key_3 = MD5(secret_key_1,dev_rand,user_rand)，user_rand：用户随机数。

- • bit8 - bit15：静态ip支持的连接类型。 ◦ bit8：1 - 支持有线； 0 - 不支持。 ◦ bit9：1 - 支持无线；0 - 不支持。 ◦ bit10：1 - 支持CAT1；0 - 不支持。 ◦ 其他bit保留。

•
秘钥5：session_key = MD5(login_key,dev_rand)，传输密钥。

### • 密钥11：secret_key_11 = MD5(auth key+device uuid+server rand)

•
密钥12：secret_key_12 = MD5(secret_key_11+dev_rand)

•
密钥13：预留位

| devide_uuid<br>16字节 | device_id_len<br>1字节 | device_id<br>n字节 | beacon_key_len<br>1字节 | beacon_key<br>n字节 | login_key_len<br>1字节 | login_key<br>n字节 |
|---|---|---|---|---|---|---|
| secret_key_len<br>1字节 | secret_key<br>n字节 | node_id_len<br>1字节 | node_id<br>n字节 | ble_net_key_len<br>1字节 | ble_net_key<br>n字节 | verify_key<br>32字节 |

verify_key[32] = HMAC-SHA-256_auth_key( L(sRandom) || L(device_uuid) || L(device_id) || L(beacon_key)

|| L(login_key) || L(secret_key) || L(node_id)

设备ID（device_uuid）和授权码(auth_key)分别储存在设备端和云端，并且具有一一映射关系，在设备绑定前，设备会以广播的形式将设备UUID的通知给APP，APP通过解密后的设备UUID向云端请求secret_key_1/ secret_key_11。

dev_rand：设备随机数，每次蓝牙连接时，设备产生一组随机数，app使用secret_key_1/secret_key_11 (未绑定)或者secret_key_4/secret_key_14 (已绑定)加密的查询设备信息指令获取该dev_rand。

Verify_key:验证V2下发的密钥正确性，验证失败将无法使能V2协议使能位。加密算法如下：

字段顺序严格按上述顺序，不得调整；长度为 0 的缺省字段也必须参与计算，编码为单字节 0x00；verify_key 自身不参与自身的计算。采用「长度 || 值」逐字段拼接是为了避免变长字段首尾相接产生的拼接歧义。

对于已绑定的设备，每次新连接，app通过secret_key_4/secret_key_14加密“获取设备信息指令”获取dev_rand，之后所有的通信采用session_key加密。

作用：校验云端的合法性。0x1002 报文的 MIC 由 secret_key_21 保证，而 secret_key_21 是 手机/网关 自行派生的，手机/网关 完全可以伪造 0x1002 的内容并生成合法的 MIC；verify_key 由 auth_key 计算，手机/网关 不掌握 auth_key，因此无法伪造。设备必须重算并比对 verify_key，才能确认这批数据确实由云端签发。

计算范围覆盖 0x1002 中全部由云端签发的字段和设备随机数，防止 手机/网关 篡改 beacon_key、node_id、ble_net_key、device_id 等字段（这些字段仅有 MIC 保护，而 MIC 对 手机/网关 不构成约束），同时加入设备随机数也可以防止手机/网关不经过云端而重放之前申请的激活信息。

7.3
数据部分（分包协议中组装后的数据部分）

数据部分包括1个字节的加密标识，16个字节的加密IV以及“3.1节描述的帧数据”的明文或者密文。

1字节
	
16字节
	
N字节


加密FLAG
	
AES-CBC IV
	
Payload的明文或者密文

| status<br>1字节 |
|---|

status：0-成功；1-失败；2-已经在绑定状态

1- secret_key_1；

## 8.4 查询设备信息：0x0000（PV≤5）

3- secret_key_3；

4- secret_key_4；

| Data length<br>2字节 |  |
|---|---|

Data length：本次连接协商的ATT MTU – 3 。

11-secret_key_11

12-secret_key_12

13-secret_key_13

| 1 | 2 | 2 | 4 | 3 | 4 | 5 | 5 | 7-12 | 7-12 | 13 | 14 | 15-46 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 固件版本号1 |  |  |  | 协议版本号 |  |  | Flag | Bond | Srand | 硬件版本1 |  | register_key+16字节0 |
| 47 | 48 |  |  | 49 | 50 | 50 | 51 | 52 | 53-54 | 55 | 56-77 | 固件版本号2 |
| 固件版本号2 |  |  |  |  | 硬件版本号2 |  | 通信能力 | Flag2 | Device virtual id(22字节) | Device virtual id(22字节) | Device virtual id(22字节) | 78 |
| 78 | 79 | 79 | 80 | 80 | 81 |  | 82 | 83 | 84 | 85 | 86-88 | 86-88 |
| mcu固件版本号 | mcu固件版本号 |  |  |  | mcu硬件版本号 | mcu硬件版本号 |  |  | Wifi_flag | Wifi_flag | 设备能力 | addr_type |
| 90 - 95 |  |  | 96 | 96 |  | 97~ | 97~ |  | … | … | … |  |
| ble_mac_addr | ble_mac_addr | ble_mac_addr | fw_key_len |  | fw_key_len | fw_key | fw_key |  | zigbee_mac_len | zigbee_mac | attach_len | attach_data |
| attach_data |  |  |  |  |  |  |  |  |  |  |  |  |
| PacketMaxSize_len | PacketMaxSize_len |  | PacketMaxSize | PacketMaxSize |  | Sl_len |  |  | Sl_value | CombosFlag<br>Length | CombosFlag | CombosFlag |

硬件版本号2：例如0x010000代表v1.1.0；硬件版本号1：取硬件版本号2的高2位，例如v1.1。

固件版本号2：例如0x010200代表v1.2.0；固件版本号1：取固件版本号2的高2位，例如V1.2。

MCU固件版本号：例如0x010000代表v1.1.0，没有外部mcu或者有外部mcu但是没获取到版本号，则填0 。

AES-CBC IV：每次加密用随机数生成，明文传输，如果加密FLAG为0，则没有IV域存在。

该指令协议版本号：目前是0x0408 (v4.8)。

序号
	
长度
	
字段
	
说明


1
	
4
	
SN
	
发送方SN


2


3


4


5
	
4
	
ACK_SN
	



6


7


8


9
	
2
	
功能码
	



10


11
	
2
	
数据长度len
	



12


13~(13+len)
	
len
	
数据
	



13+len
	
2

	
CRC16
	
检验


14+len

bit6：是否支持语音服务。

•
SN：app和设备端各维护自己的SN，每次新的连接初始值为1，每发送一包指令依次滚动加1，溢出复位重新赋值1。

bit4：是否需要beaconKey，0-不需要，1-需要（配对请求指令中含beaconKey）。

bit3：对于高级加密已绑定设备是否需要进行云端验证，0-不需要，1-需要。

bit2：0-使用固件版本号1, 1-使用固件版本号2。

•
校验字：本协议采用CRC16校验，校验内容包括SN、ACK_SN、功能码、数据长度、数据4个部分。

bit0: 0-使用硬件版本号1,  1-使用硬件版本号2。

7.5.1
设备发现

设备发现过程如下：BLE 设备在未连接状态下，会间断发送广播包。手机 APP 只对广播包中含涂鸦特定服务 UUID的蓝牙设备发送扫描请求处理，蓝牙设备在扫描广播应答包中发送设备的product_id ，device_id 以及绑定标志和版本信息，手机APP 通过device_id 从服务端拉取信息，通过product_id从服务端拉取UI 以及一些产品信息。

bit6：1 – 支持BR/EDR通道(SPP & GOBR)；0 – 不支持；

bit5：1 – 支持Hey Tuya；0 – 不支持；

bit4：1 – 支持离线解绑升级方案（可根据前序操作区分是否清除数据）；0 – 不支持；

bit3：1 – 当前程序运行在Bootloader区；0 – 当前程序运行在程序区。

bit2：执行一般加密V2协议状态（默认0表示未执行V2协议，1表示已执行V2协议）

bit1：支持一般加密V2协议

1.
手机app扫描设备广播包，并通过广播包中解密出来的device_uuid从服务器拉取对应的secret_key_1/ secret_key_11，之后与设备建立连接。

register_key：AES-ECB(auth_key(前16字节),server_rand)，本条指令加密方式为key_1时才有效，其他填0。

3.
设备根据自身绑定状态选用对应的解密方式解密指令，如果是已绑定状态选用secret_key_4 /secret_key_14解密指令，如果是未绑定状态选用secret_key_1/secret_key_11解密指令，之后响应指令，响应指令采用secret_key_1/secret_key_11或者secret_key_4/secret_key_14加密，响应内容包含设备产生的dev_rand、设备版本号，协议版本号、设备安全等级以及设备绑定标志和auth_key的hmac值等。

4.
app发起配对请求指令，配对请求指令中包含设备的device_uuid和login_key，配对指令采用secret_key_2/secret_key_12加密。

5.
设备使用secret_key_2/secret_key_12解密配对请求指令，首先判断其中的device_uuid是否和自身匹配，如果不匹配回复失败，并断开蓝牙连接，如果匹配回复绑定成功，响应指令采用session_key加密。

注：已绑定的设备通过secret_key_4/secret_key_14加密发送“获取设备信息指令”来得到设备的dev_rand 。

Device virtual id：22字节，设备虚拟id，设备如果没有此虚拟ID，填0发送。

Wifi_flag ：适用于wifi/BLE双协议设备。

bit0（暂未使用） ：

1.
手机app扫描设备广播包，并通过广播包中解密出来的device_uuid从服务器拉取对应的secret_key_1，之后与设备建立连接。

2.
app发送通过secret_key_1加密的“获取设备信息”指令到设备，该指令包含MTU值。

3.
设备根据自身绑定状态选用对应的解密方式解密指令，如果是已绑定状态选用secret_key_4解密指令，如果是未绑定状态选用secret_key_1解密指令，之后响应指令，响应指令采用secret_key_4或者secret_key_1加密，响应内容包含设备产生的dev_rand、设备版本号，协议版本号、register key等，设备安全等级以及设备绑定标志等。

4.
APP根据设备响应包中的“设备安全等级是否是高级加密”来向云端请求证书公钥和对应签名。

5.
app发送云端证书公钥及对应签名到设备端。

6.
设备端收到云端证书公钥和对应签名后，使用TUYA HARDWARE CA公钥验证，验证通过后生成6字节随机数发送给app。

bit23: 1 - 设备支持NFC+BLE beacon通讯能力（例：门锁快速解锁）；0 - 设备不支持

bit22: 1 - 该设备是否支持通过BLE链路去管理WiFi信息（例如：WiFi名称和密码，仅双模使用）

9.
设备发送设备证书到app，app发给云端校验，云端校验成功后生成6字节随机数发给app，app将随机数发给送设备。

10.
设备使用设备证书私钥对随机数进行签名，并回传给app，app发送给云端进行验证，云端使用设备证书的公钥进行验证。

bit19：1 – 设备通过透传通道收发DP；0 – 设备通过原有通道（0002、0027等）收发DP，适用于蓝牙单点、Combo设备，其中蓝牙单点和Combo的透传通道不同，格式也不同，单点暂不使用，详见“蓝牙直控网关下子设备技术方案”。

12.
app发起配对请求指令，配对请求指令中包含设备的device_uuid和login_key，配对指令采用secret_key_2加密。

13.
设备使用secret_key_2解密配对请求指令，首先判断其中的device_uuid是否和自身匹配，如果不匹配回复失败，并断开蓝牙连接，如果匹配回复绑定成功，响应指令采用session_key加密。

注：已绑定的设备通过secret_key_4加密发送“获取设备信息指令”来得到设备的dev_rand 。

bit15：1 – 支持主从一体；0 – 不支持主从一体。

bit14：设备在线逻辑，0-采用标准功耗设备在线策略；1- 采用低功耗设备在线策略，目前只适用于     蓝牙网关，手机 APP 无需关心此位。

bit13：Gateway Connect Mode，网关连接模式，仅适用于网关，0-短连接（按需连接）；1-长连接。

3、设备在绑定状态，而app处于解绑状态：物理解绑或者app查询已存储在云端的信息，然后发送异常解绑指令解绑，其中针对V2协议的异常解绑采用Key16进行加解密并解绑设备。

4、针对支持一般加密V2协议设备，解绑移除后需清除当前执行V2协议状态（如：通过支持V2协议App切换密钥后，我们将执行状态bit置1，当任意方式移除该设备后，我们需清除该bit为0）

bit9：1 – 支持挂载配件；0 – 不支持。

整个加密链路使用device_uuid与auth_key成对这种特质进行加密，避免了密码传输这一过程。

通信中传输的是auth_key和随机数的算法变化值，并不是原始值，保证了auth_key的安全。也避免了需要传输auth_key的历史问题。

bit6：1 – 支持蓝牙BT/BLE双模；0 – 不支持。

为了防止截取复制内容攻击，每次连接会有随机码加严。每次连接都会有不同的数据内容。

bit4：1 – 支持扩展模块；0 – 不支持扩展模块。

本章各功能码指令仅描述 Data 字段部分，完整Payload格式见各协议版本安全层中的「Payload格式」定义：

bit2：1 – 具备通过beacon能力；0 – 没有beacon能力。

bit1：1 – 网关和app对于该设备使用低功耗在线逻辑；0 – 使用标准功耗在线逻辑。

bit0：1 – OTA时DP可控；0 – OTA时DP不可控。

addr_type：蓝牙mac地址类型，0 – Public Device Address ；1 – Random Device Address；

ble_mac_addr：蓝牙mac地址，6字节，小端格式，低地址存低字节。

fw_key_len：固件key长度，如果为0表示后面没有固件key。

Flags
1字节
	
dataMTU
2字节
	
mRandom
16字节
	
serverRandom
16字节，可选
	
mPublicKey
64字节，可选

zigbee_mac_len：zigbee mac长度，如果为0表示后面没有zigbee mac。

zigbee_mac：zigbee设备mac，一般为8个字节。

bit6：描述mPublicKey字段是否存在标志位。

attach_data：附件信息数据，4字节一个单元，格式如下所示：

| 1 | 2 | 3 | 4 | ~ | n | n+1 | n+2 | n+3 |
|---|---|---|---|---|---|---|---|---|
| channel | channel_attach_version |  |  | ~ | channel | channel_attach_version |  |  |

流程
	
适用security_mode
	
Flags
	
bit7
	
bit6
	
Data组成


配网
	
0
	
0x80
	
1
	
0
	
Flags || dataMTU || mRandom || serverRandom


配网
	
1、2
	
0xC0
	
1
	
1
	
Flags || dataMTU || mRandom || serverRandom || mPublicKey


重连
	
0、1
	
0x00
	
0
	
0
	
Flags || dataMTU || mRandom


重连
	
2
	
0x40
	
0
	
1
	
Flags || dataMTU || mRandom || mPublicKey


离线解绑
	
0、1、2
	
0x80
	
1
	
0
	
Flags || dataMTU || mRandom || serverRandom

channel_attach_version：对应通道号的附件的当前版本，例如0x010200代表v1.2.0 。

接收侧必须用 (Flags.Bit7, Flags.Bit6, 本机绑定状态) 三者共同判定本次握手属于哪条流程，并据此选择密钥派生路径：

PacketMaxSize_len: 用于表示PacketMaxSize字段长度，如果为0表示后面没有PacketMaxSize。

PacketMaxSize: 应用层分包的最大长度, 需分包发送的命令由该值决定一包传输的大小。

注：如果各个业务模块中有单独指令含有packetMaxSize，那么该业务模块使用模块指令中的packetMaxSize ，例如001A指令使用0019指令中的packetMaxSize 。

2.
离线解绑流程不使用 ECDH，三种模式共用同一条流程与同一把 key26。因此 security_mode_2 的设备在离线解绑流程中也必须接受 Bit6=0 的 0x1000 —— 这是 mode_2 唯一允许 Bit6=0 的场合。

3.
表中未列出的组合一律视为非法（例如 Bit7=0 且本机未绑定、或 Bit6 取值与本机 security_mode 及本流程不符），必须直接拒收并断开连接，不得忽略未预期的字段继续往下走。

4.
该判定必须在任何密钥派生与 ECC 运算之前完成，避免为一个伪造的 0x1000 白做椭圆曲线运算。

CombosFlag：多协议设备能力标志字段，长度取决于CombosFlagLength字段，其中：

bit5：1 – 支持静态IP；0 – 不支持。当为1时，通过bit8 - bit15表示支持的连接类型。

bit4：1 - 支持查询多网络连接能力；0 - 不支持。

Flags
1字节
	
sRandom
16字节
	
sPublicKey
64字节，可选

bit2：1 – 支持日志采集传输；0 – 不支持。

bit1：1 – 支持配网阶段各状态上报；0 – 不支持。

bit0：1 – 支持查询wifi热点列表；0 – 不支持。

各流程的 Flags 取值如下表所示：

流程
	
适用security_mode
	
Flags
	
Data组成


配网
	
0
	
0x00
	
Flags || sRandom


配网
	
1、2
	
0x80
	
Flags || sRandom || sPublicKey


重连
	
0、1
	
0x00
	
Flags || sRandom


重连
	
2
	
0x80
	
Flags || sRandom || sPublicKey


离线解绑
	
0、1、2
	
0x00
	
Flags || sRandom

bit8：1 - 支持有线； 0 - 不支持。

bit9：1 - 支持无线；0 - 不支持。

bit10：1 - 支持CAT1；0 - 不支持。

device_uuid
16字节
	
SRC_TYPE
1字节
	
AppFlagLength
1字节
	
AppFlag
n字节

•
SRC_TYPE：发送源。
◦
0 – 不支持；
◦
1 – 安卓APP；
◦
2 – 网关；
◦
3 – 小程序；
◦
4 – 门锁配件；
◦
5 – IOS APP；
◦
6 – 主从一体主设备；
◦
其他保留。

### • AppFlagLength：1 字节，AppFlag 的长度，当前长度为 1。

•
AppFlag：APP / 网关 / 小程序等主端的能力标志。
◦
bit0：1 - 设备通过透传通道收发 DP；0 - 设备通过原有通道（0002、0027 等）收发 DP。
◦
其他 bit 保留。

响应帧 BLE->APP：

| 1-16 | 17-22 | 23-44 | 45 | 46-61 | 62 | 63-78 | 79-94 | 95-98 |
|---|---|---|---|---|---|---|---|---|
| Device_id | Login_key | Device virtual id | beaconKey_len | beaconKey | SRC_TYPE | Login_key_16 | Secret_key_16 | Verify_key |
| 99 | 100 | 101 | 102-103 | 104 | 105-120 |  |  |  |
| AppFlag<br>Length | AppFlag | node_id_len | node_id | ble_net_key_len | ble_net_key |  |  |  |

协议版本号
2字节
	
安全版本号
3字节
	
加密方式
1字节
	
Flag
1字节
	
Flag2
1字节
	
Flagx长度
1字节
	
Flagx-预留
n字节
	
Bond
1字节
	
RegisterKey
32字节


固件版本号
3字节
	
SDK版本字符长度  1字节
	
SDK版本字符
n字节
	
硬件版本号
3字节
	
mcu固件版本号
3字节
	
mcu硬件版本号
3字节
	
通信能力
2字节
	
Device virtual id length
1字节
	
Device virtual id
n字节


WiFi_flag
1字节
	
设备能力长度
1字节
	
设备能力
n字节
	
addr_type
1字节
	
ble_mac_addr
6字节


fw_key_len
1字节，如果为0表示后面没有fw_key
	
fw_key
n字节


zigbee_mac_len
1字节，如果为0表示后面没有zigbee_mac
	
zigbee_mac
n字节


attach_len
1字节，如果为0表示后面没有attach_data
	
attach_data
n字节


PacketMaxSize_len
1字节，如果为0表示后面没有PacketMaxSize
	
PacketMaxSize
n字节


Sl_len
1字节，如果为0表示后面没有Sl_value
	
Sl_value
n字节


CombosFlag Length
1字节，如果为0表示后面没有CombosFlag
	
CombosFlag
n字节

Device virtual id：设备虚拟ID，22字节，设备每次被绑定时存储此虚拟ID，被重置时删除此虚拟ID。

beaconKey：是否包含beaconKey取决于查询设备信息指令中的FLAG，如果FLAG_bit4为1则beaconKey_len为16，如果app获取不到beaconKey，则配网失败 ；如果FLAG_bit4为0，则beaconKey_len赋值0，表示不存在beaconKey。

加密方式security_mode：定义同ADV章节的加密方式

0 – 不支持；

固件版本号：例如0x010200代表v1.2.0；

硬件版本号：例如0x010000代表v1.1.0；

SDK版本信息：采用长度+字符串形式表示（对应sdk_full_version字段）；

4 – 门锁配件；

MCU固件版本号：例如0x010000代表v1.1.0，没有外部mcu或者有外部mcu但是没获取到版本号，则填0 。

MCU硬件版本号：同MCU固件版本号。

其他保留。

Verify_key：4字节，当通过MD5(16字节login_key+16字节secret_key+authkey)前四字节加密后比对数据，如校验成功方可开启一般加密_V2版本。

注：由于新老App该字段长度不一致，该命令当前允许62字节或98字节协议动态使用。设备将通过填充数据以及长度来判定是否标识已执行一般加密_V2版本。

AppFlagLength：1字节，AppFlag的长度，当前长度为1。

AppFlag：APP/网关/小程序等主端的能力标志，bit0：1 – 设备通过透传通道收发DP；0 – 设备通过原有通道（0002、0027等）收发DP，其他bit保留。

注意：APP/网关/小程序等主端根据0000指令里的协议版本号判断是否包含AppFlagLength及AppFlag字段，如果版本号大于等于4.8就需要包含该字段，AppFlag的字节数取决于AppFlagLength。

Node_id：节点id，BLE Beacon单控/群控时用到的节点id，每次配网时下发，重连时不下发。

ble_net_key_len：1字节无符号整数，表示ble_net_key的长度。

ble_net_key：16字节无符号整数，表示BLE广播控制时候使用的key，需要时根据云端逻辑更新（避免频繁更新），每次配网时下发，重连时不下发。

•
bit6：是否支持语音服务。

•
bit5：WIFI-BLE combo设备是否支持Plug&play配网协议标志，0 – 不支持，1- 支持。

•
bit4：是否需要beaconKey，0-不需要，1-需要（配对请求指令中含beaconKey）。

| 状态码<br>1字节 |
|---|

•
bit2：0-使用固件版本号1, 1-使用固件版本号2。保留，默认0（已强制使用3位版本号）

•
bit1：0-一般加密设备 ,   1-高级加密设备。

### • bit0: 0-使用硬件版本号1,  1-使用硬件版本号2。保留，默认0（已强制使用3位版本号）

命令帧 APP->BLE：

•
bit7：1 – 内部方案；0 – 外部方案；

| Dp数组1的数据 | Dp数组1的数据 |  |  | ~ | Dp数组n的数据 | ~ | Dp数组n的数据 |  |
|---|---|---|---|---|---|---|---|---|
| 1 | 2 | 3 | 3~ | ~ | n+1 | n+3~ | n+2 | Dp_type |
| Dp_len | Dp_type | Dp_data | Dp_data | Dp_id | Dp_id | Dp_type | Dp_len | Dp_data |

•
bit5：1 – 支持Hey Tuya；0 – 不支持；

•
bit4：1 – 支持离线解绑升级方案（可根据前序操作区分是否清除数据）；0 – 不支持；

•
bit3：1 – 当前程序运行在Bootloader区；0 – 当前程序运行在程序区。

•
bit2：执行一般加密V2协议状态（默认0表示未执行V2协议，1表示已执行V2协议）保留，默认0

•
bit1：请求更新long-term keys（默认0表示不请求，1表示请求）。适用于v1老设备升级后主动请求更新密钥。

•
bit0：当前蓝牙连接是否已经LINK层加密。

#define  DT_ENUM    4

#define  DT_BITMAP   5

Flagx-预留：n字节，随Flagx长度定义。

Dp_data：数据，dp_len个字节。

设备能力长度：1字节，设备能力长度字段

设备能力：n字节，按bits方式使用 。

•
其他 bits 保留。

| 状态码<br>1字节 |
|---|

•
bit22: 1 - 该设备是否支持通过BLE链路去管理WiFi信息（例如：WiFi名称和密码，仅双模使用）

•
bit21：1 - 设备支持BLE Beacon单控/群控能力；0 - 设备不支持BLE Beacon单控/群控能力。

### • bit20：1 - 设备支持Long Range能力；0 - 设备不支持Long Range能力。

•
bit19：1 – 设备通过透传通道收发DP；0 – 设备通过原有通道（0002、0027等）收发DP，适用于蓝牙单点、Combo设备，其中蓝牙单点和Combo的透传通道不同，格式也不同，单点暂不使用，详见“蓝牙直控网关下子设备技术方案”。

•
bit18: 1 – BLE设备支持wifi局域网通信能力；0 – BLE设备不支持wifi局域网通信能力。

如果数据长度n>0，代表查询n个dp点的数据，这n个dp点序号在data数据区。

•
bit16：0 - 不支持网关中继功能；1 - 支持网关中继功能。

•
bit15：1 – 支持主从一体；0 – 不支持主从一体。

•
bit14：设备在线逻辑，0-采用标准功耗设备在线策略；1- 采用低功耗设备在线策略，目前只适用于     蓝牙网关，手机 APP 无需关心此位。

| 状态码<br>1字节 |
|---|

•
bit12-11：Connectivity ，0-同时支持手机和网关；1-只支持网关；2-只支持手机。

> 注：状态数据是通过“状态上报指令发送”。

•
bit9：1 – 支持挂载配件；0 – 不支持。

### • bit8：是否支持psk，用于wifi多协议设备，1 – 支持；0 – 不支持。

•
bit7：1 – 需要强制LINK层加密；0 – 无需强制LINK层加密。

•
bit6：1 – 支持蓝牙BT/BLE双模；0 – 不支持。

### • bit5：1 – 支持定时；0 – 不支持定时。

•
bit4：1 – 支持扩展模块；0 – 不支持扩展模块。

•
bit3：1 – 有蓝牙LINK层加密使能；0 – 无蓝牙LINK层加密使能。

•
bit2：1 – 具备通过beacon能力；0 – 没有beacon能力。

•
bit1：1 – 网关和app对于该设备使用低功耗在线逻辑；0 – 使用标准功耗在线逻辑。

•
bit0：1 – OTA时DP可控；0 – OTA时DP不可控。

| 状态码<br>1字节 |
|---|

Wifi_flag ：适用于wifi/BLE双协议设备。

•
bit0（暂未使用） ：
◦
0 – 发送配网信息前不发送查询wifi设备当前配网状态指令。
◦
1 – 发送配网信息前发送查询wifi设备当前配网状态指令。

### • bit1： ◦ 0 – 优先通过WIFI配网。 ◦ 1 – 优先通过蓝牙配网。

addr_type：蓝牙mac地址类型，0 – Public Device Address ；1 – Random Device Address；

ble_mac_addr：蓝牙mac地址，6字节，小端格式，低地址存低字节。

fw_key_len：固件key长度，如果为0表示后面没有固件key。

fw_key：固件key，主要用于涂鸦通用模组固件，一般为8或16字节。

| 状态码<br>1字节 |
|---|

zigbee_mac_len：zigbee mac长度，如果为0表示后面没有zigbee mac。

> 注：设备收到该重置指令后，解绑设备并清除login key和虚拟ID。

## 8.11 读取大数据摘要信息：0x0007

attach_data：附件信息数据，4字节一个单元，格式如下所示：


附件信息1
	
~
	
附件信息n


channel
1字节
	
channel_attach_version
3字节
	
~
	
channel
1字节
	
channel_attach_version
3字节


◦
channel：通道号，范围是10-19 。
◦
channel_attach_version：对应通道号的附件的当前版本，例如0x010200代表v1.2.0 。
◦
根据attach_len的值来计算有多少个附件以及对应的附件通道号和版本号。

PacketMaxSize_len: 用于表示PacketMaxSize字段长度，如果为0表示后面没有PacketMaxSize。

| Version<br>1字节 | Bulk_Type<br>1字节 |
|---|---|

注：如果各个业务模块中有单独指令含有packetMaxSize，那么该业务模块使用模块指令中的packetMaxSize ，例如001A指令使用0019指令中的packetMaxSize 。

Bulk_Type：要读取的大数据编号，由具体产品定义，从1开始，0表示通用类型数据。

Sl_len：用于表示sl_value字段长度，如果为0表示后面没有sl_value。

Sl_value：用于wifi设备，0 – psk ; 1 – ecc ; 2 – mtls（双向认证）; 3 – mtls + 安全芯片 。

Data格式：

| Version<br>1字节 | Bulk_Type<br>1字节 | Status<br>1字节 | FLAG<br>1字节 | 总长度<br>4字节 | 总CRC32<br>4字节 | 块长度<br>2字节 |
|---|---|---|---|---|---|---|

CombosFlag：多协议设备能力标志字段，长度取决于CombosFlagLength字段，其中：

•
bit5：1 – 支持静态IP；0 – 不支持。当为1时，通过bit8 - bit15表示支持的连接类型。

Bulk_Type：要读取的大数据类型编号，从1开始，由具体产品定义，0表示通用类型数据。

Status：0 – 成功，后跟数据有效；1 – Type无效，后跟数据无效或者无后跟数据；其他 – 保留。

FLAG：是否需要 APP 解析大数据，0 – 需要，1 – 不需要APP解析，需要面板或者云端解析。

•
bit1：1 – 支持配网阶段各状态上报；0 – 不支持。

总CRC32 ：4字节，大端格式，本Type类型的大数据总CRC32。

•
bit8 - bit15：静态ip支持的连接类型。
◦
bit8：1 - 支持有线； 0 - 不支持。
◦
bit9：1 - 支持无线；0 - 不支持。
◦
bit10：1 - 支持CAT1；0 - 不支持。
◦
其他bit保留。

## 8.3 配对请求：0x1002（PV6）

命令帧 APP->BLE：

Data格式：

| Version<br>1字节 | Bulk_Type<br>1字节 | 块号<br>2字节 |
|---|---|---|

verify_key[32] = HMAC-SHA-256_auth_key( L(sRandom) || L(device_uuid) || L(device_id) || L(beacon_key)

|| L(login_key) || L(secret_key) || L(node_id)

|| L(ble_net_key) )

其中 L(x) = x 的长度(1 字节) || x。

响应帧 BLE->APP

字段顺序严格按上述顺序，不得调整；长度为 0 的缺省字段也必须参与计算，编码为单字节 0x00；verify_key 自身不参与自身的计算。采用「长度 || 值」逐字段拼接是为了避免变长字段首尾相接产生的拼接歧义。

| Version<br>1字节 | Bulk_Type<br>1字节 | Status<br>1字节 | 块号<br>2字节 | 本块长度<br>2字节 | 单包长度<br>2字节 | 块数据CRC16<br>2字节 |
|---|---|---|---|---|---|---|

作用：校验云端的合法性。0x1002 报文的 MIC 由 secret_key_21 保证，而 secret_key_21 是 手机/网关 自行派生的，手机/网关 完全可以伪造 0x1002 的内容并生成合法的 MIC；verify_key 由 auth_key 计算，手机/网关 不掌握 auth_key，因此无法伪造。设备必须重算并比对 verify_key，才能确认这批数据确实由云端签发。

计算范围覆盖 0x1002 中全部由云端签发的字段和设备随机数，防止 手机/网关 篡改 beacon_key、node_id、ble_net_key、device_id 等字段（这些字段仅有 MIC 保护，而 MIC 对 手机/网关 不构成约束），同时加入设备随机数也可以防止手机/网关不经过云端而重放之前申请的激活信息。

Status：0 – 成功；1 – Type无效；2 – 块号无效；

块号：当前要读取的块数据编号（app每发送一次读取块数据指令，设备会以响应帧中的单包长度通过0x0009指令分包发送块数据）。

本块长度：当前块号块数据的总长度。

单包长度：本次读取的块数据上报的分包最大长度。

status：0-成功；1-失败；2-已经在绑定状态

## 8.4 查询设备信息：0x0000（PV≤5）

命令帧 BLE->APP：

Data格式：

| Version<br>1字节 | Bulk_Type<br>1字节 | 当前块号<br>2字节 | 当前包号<br>2字节 | 当前包长度<br>2字节 | 当前包数据<br>n字节 |
|---|---|---|---|---|---|

Data length：本次连接协商的ATT MTU – 3 。

Version：当前必须为0。

Bulk_Type：要读取的大数据类型，从1开始由具体产品定义，0表示通用类型数据。

当前块号：当前传输的数据块编号。

1
	
2
	
3
	
4
	
5
	
6
	
7-12
	
13
	
14
	
15-46


固件版本号1
	
协议版本号
	
Flag
	
Bond
	
Srand 
	
硬件版本1
	
register_key+16字节0


47
	
48
	
49
	
50
	
51
	
52
	
53-54
	
55
	
56-77


固件版本号2
	
硬件版本号2
	
通信能力
	
Flag2
	
Device virtual id(22字节)


78
	
79
	
80
	
81
	
82
	
83
	
84
	
85
	
86-88
	
89


mcu固件版本号
	
mcu硬件版本号
	
   Wifi_flag
	
0
	
设备能力
	
addr_type


90 - 95
	
      96
	
97~
	
…
	
…
	

	



ble_mac_addr
	
fw_key_len
	
fw_key
	
zigbee_mac_len
	
zigbee_mac
	
attach_len
	
attach_data



	

	

	

	

	

	



PacketMaxSize_len
	
PacketMaxSize
	
Sl_len
	
Sl_value
	
CombosFlag
Length
	
CombosFlag

硬件版本号2：例如0x010000代表v1.1.0；硬件版本号1：取硬件版本号2的高2位，例如v1.1。

固件版本号2：例如0x010200代表v1.2.0；固件版本号1：取固件版本号2的高2位，例如V1.2。

MCU固件版本号：例如0x010000代表v1.1.0，没有外部mcu或者有外部mcu但是没获取到版本号，则填0 。

举例说明：设备当前共有9K字节数据，准备每块最大传输2k字节，需要分为5块数据，最后一块1k字节。

1. 1. app首先发送0x0007指令，设备回复总长度为9k字节，每块最大2K字节以及总数据的CRC32 。

1. 2. app 发送0x0008指令读取第一块数据，指定块号为0，设备回复了本块数据长度、本块数据CRC16，最大分包长度，app收到设备的回复后开始计时，准备接收本块数据。

1. 3. 设备通过0x0009指令开始分包发送本块数据，app无需回复该指令，只负责接收。

1. 4. app通过0x0009指令接收完一整块数据后（根据接收的长度判断是否结束），开始校验本块数据，校验失败，则重新读取，检验成功读取下一块。

1. bit4：是否需要beaconKey，0-不需要，1-需要（配对请求指令中含beaconKey）。

1. bit3：对于高级加密已绑定设备是否需要进行云端验证，0-不需要，1-需要。

bit2：0-使用固件版本号1, 1-使用固件版本号2。

### bit1：0-一般加密设备 ,   1-高级加密设备。

bit0: 0-使用硬件版本号1,  1-使用硬件版本号2。

Data格式：

| Version<br>1字节 | Bulk_Type<br>1字节 |
|---|---|

bit6：1 – 支持BR/EDR通道(SPP & GOBR)；0 – 不支持；

Bulk_Type：要擦除的大数据类型，从1开始由具体产品定义，0表示通用类型数据。

bit4：1 – 支持离线解绑升级方案（可根据前序操作区分是否清除数据）；0 – 不支持；

bit3：1 – 当前程序运行在Bootloader区；0 – 当前程序运行在程序区。

bit2：执行一般加密V2协议状态（默认0表示未执行V2协议，1表示已执行V2协议）

| Version<br>1字节 | Bulk_Type<br>1字节 | Status<br>1字节 |
|---|---|---|

bit0：当前蓝牙连接是否已经LINK层加密，其他bit保留。

register_key：AES-ECB(auth_key(前16字节),server_rand)，本条指令加密方式为key_1时才有效，其他填0。

从v3.3版本协议开始，使用register_key。

Bond：0-未绑定，1-已绑定。

### Srand：设备当前有效随机数,6字节。

通信能力(2个字节大端格式，高字节在前)：格式详见广播格式章节。

Device virtual id：22字节，设备虚拟id，设备如果没有此虚拟ID，填0发送。

| Version<br>1字节 | Request Bulk Count<br>1字节 | Request Bulk Data Info<br>7字节 | ~ | Request Bulk Data Info<br>7字节 |
|---|---|---|---|---|

Request Bulk Data Info格式：

| 1个字节 | 1个字节 | 1个字节 | 4个字节 |
|---|---|---|---|
| Bulk_Type | FLAG | Res | 总长度 |

1 – 发送配网信息前发送查询wifi设备当前配网状态指令。

Version：当前必须为0。

Request Bulk Count：要请求的大数据个数。

Bulk_Type：要读取的大数据类型编号，从1开始，由具体产品定义，0表示通用类型数据。

FLAG：是否需要 APP 解析大数据，0 – 需要，1 – 不需要APP解析，需要面板或者云端解析。

bit23: 1 - 设备支持NFC+BLE beacon通讯能力（例：门锁快速解锁）；0 - 设备不支持

数据总长度：4字节，大端格式，本Type类型的大数据总长度，跟0x0007指令中回复的总长度一致，例如可做预览使用。

bit21：1 - 设备支持BLE Beacon单控/群控能力；0 - 设备不支持BLE Beacon单控/群控能力。

bit20：1 - 设备支持Long Range能力；0 - 设备不支持Long Range能力。

bit19：1 – 设备通过透传通道收发DP；0 – 设备通过原有通道（0002、0027等）收发DP，适用于蓝牙单点、Combo设备，其中蓝牙单点和Combo的透传通道不同，格式也不同，单点暂不使用，详见“蓝牙直控网关下子设备技术方案”。

### bit18: 1 – BLE设备支持wifi局域网通信能力；0 – BLE设备不支持wifi局域网通信能力。

![Feishu Docs - Image](blob:https://icn602w9tnqf.feishu.cn/ca5aa309-2f49-49b2-981e-f3deca362c44)

### bit16：0 - 不支持网关中继功能；1 - 支持网关中继功能。

bit15：1 – 支持主从一体；0 – 不支持主从一体。

bit14：设备在线逻辑，0-采用标准功耗设备在线策略；1- 采用低功耗设备在线策略，目前只适用于     蓝牙网关，手机 APP 无需关心此位。

| type<br>1字节 |
|---|

TYPE: 0 – BLE升级固件，1 – 外部mcu升级固件，10-19：附件通道号（IOT前台创建），其他 – 保留。

bit10: 1 – 支持差分OTA；0  – 不支持差分OTA。

bit9：1 – 支持挂载配件；0 – 不支持。

bit8：是否支持psk，用于wifi多协议设备，1 – 支持；0 – 不支持。

| Flag<br>1字节 | OTA_Version<br>1字节 | TYPE<br>1字节 | Type_data<br>6字节 | flag2<br>1字节 | Sign_info_len<br>1字节 | Sign_info_data<br>n字节 |
|---|---|---|---|---|---|---|

bit6：1 – 支持蓝牙BT/BLE双模；0 – 不支持。

OTA_Version：OTA协议大版本，例如0x03代表3.X的协议版本,当前必须等于3，其他值会被app拒绝升级。

TYPE：0 – BLE升级固件，1 – 外部mcu升级固件，10-19：附件通道号（IOT前台创建），其他 – 保留。

Type_data：不同的type对应不同格式的type_data，TYPE为0、1、10-19对应的type_data格式如下所示：

| 1 | 2 | 3 | 4 | 5 | 6 |
|---|---|---|---|---|---|
| Version四字节 |  |  |  | 最大包长度 |  |

bit1：1 – 网关和app对于该设备使用低功耗在线逻辑；0 – 使用标准功耗在线逻辑。

bit0：1 – OTA时DP可控；0 – OTA时DP不可控。

addr_type：蓝牙mac地址类型，0 – Public Device Address ；1 – Random Device Address；

sign_info_len：sign_info_data的长度，单位字节，如果flag2的bit0为0，则sign_info_len为0，即没有后续的sign_info_data字段。

sign_info_data：内容如下所示，后续支持扩展，长度有sign_info_len决定。

| 1 | 2 |
|---|---|
| Signature algorithm | key_version |

zigbee_mac_len：zigbee mac长度，如果为0表示后面没有zigbee mac。

zigbee_mac：zigbee设备mac，一般为8个字节。

注：如果固件支持验签，app或者网关需要发送0x0018指令下发固件签名信息，之后再下发0x000D升级文件信息，如果固件返回的key_version比云端最新的key版本低，app/网关需要通过0x0019指令给设备固件更新到最新的密钥版本。老版本app/网关不会解析新增的flag2字段，所以也就不会发送签名数据，固件要兼容老app，按照不支持验签的ota流程执行。

attach_data：附件信息数据，4字节一个单元，格式如下所示：

# 1 2 3 4 ~ n n+1 n+2 n+3 channel channel_attach_version ~ channel channel_attach_version

channel：通道号，范围是10-19 。

channel_attach_version：对应通道号的附件的当前版本，例如0x010200代表v1.2.0 。

| TYPE<br>1字节 | 产品PID<br>8字节 | 文件版本<br>4字节 | 文件MD5<br>16字节 | 文件长度<br>4字节 | CRC32<br>4字节 |
|---|---|---|---|---|---|

PacketMaxSize_len: 用于表示PacketMaxSize字段长度，如果为0表示后面没有PacketMaxSize。

| TYPE<br>1字节 | 产品PID<br>8字节 | 文件版本<br>4字节 | 文件长度<br>4字节 | Digest TLV<br>1 + 1 + n字节 |
|---|---|---|---|---|

注：如果各个业务模块中有单独指令含有packetMaxSize，那么该业务模块使用模块指令中的packetMaxSize ，例如001A指令使用0019指令中的packetMaxSize 。

| Digest TLV |  |  |
|---|---|---|
| Digest Type | Digest  Len | Digest |
| 1字节 | 1字节 | N字节 |
|  |  |  |
| 0x01 - SHA256 | 32 | ~ |
| 其他 - 保留 |  |  |

Sl_value：用于wifi设备，0 – psk ; 1 – ecc ; 2 – mtls（双向认证）; 3 – mtls + 安全芯片 。

- • TYPE：0 – BLE升级固件，1 – 外部mcu升级固件，10-19：附件通道号（IOT前台创建），其他 – 保留，不同的TYPE，后面对应不同的数据格式，上表为TYPE=0、1、10-19对应的格式

- CombosFlag：多协议设备能力标志字段，长度取决于CombosFlagLength字段，其中：

- bit5：1 – 支持静态IP；0 – 不支持。当为1时，通过bit8 - bit15表示支持的连接类型。

- bit4：1 - 支持查询多网络连接能力；0 - 不支持。

- • CRC32：文件全部内容的 CRC32 值。用途为传输与存储的错误检测、以及断点续传的已储存数据比对（见响应帧说明），不承担密码学校验职能。

- • 文件摘要：文件摘要。 ◦ ≤V5，采用MD5算法计算； ◦ ≥V6，由Digest TLV决定。V6 版设备按以下两个用途使用 ▪ 文件身份标识：设备将其与断点信息一同储存；断点续传时，设备以该字段比对本次下发文件与已储存文件是否为同一文件，不一致则从头传输； ▪ 完整性校验：设备在接收完成后（收到 0x0010 升级结束指令时）必须计算已接收文件的完整摘要并与该字段比对，不一致则按校验失败处理，升级不生效。  注：校验机制的分工 端到端完整性：Digest（摘要）负责文件身份标识与端到端完整性校验； 真实性/防篡改：固件真实性由签名机制（0x0018 签名数据下发、0x0010 结束时验签）保证，摘要校验不能替代验签。

bit1：1 – 支持配网阶段各状态上报；0 – 不支持。

bit0：1 – 支持查询wifi热点列表；0 – 不支持。

Data格式：

| TYPE<br>1字节 | STATE<br>1字节 | 已储存文件长度<br>4字节 | 已储存文件CRC32<br>4字节 | 已储存文件MD5（目前不使用）<br>16字节 |
|---|---|---|---|---|

bit8：1 - 支持有线； 0 - 不支持。

| TYPE<br>1字节 | STATE<br>1字节 | 已储存文件长度<br>4字节 | 已储存文件摘要信息（Digest TLV）<br>1+1+n字节 |
|---|---|---|---|

- • TYPE：0 – BLE升级固件，1 – 外部mcu升级固件，10-19：附件通道号（IOT前台创建），其他 – 保留，不同的TYPE，后面对应不同的数据格式，上表为TYPE=0、1、10-19对应的格式。

- • STATE：

0x00：正常升级

8.5
配对请求：0x0001（PV≤5）

0x02: 文件版本低于或者等于当前版本

0x03：文件大小超过范围。

1-16
	
17-22
	
23-44
	
45
	
46-61
	
62
	
63-78
	
79-94
	
95-98


Device_id
	
Login_key
	
Device virtual id
	
beaconKey_len
	
beaconKey
	
SRC_TYPE
	
Login_key_16
	
Secret_key_16
	
Verify_key


99
	
100
	
101
	
102-103
	
104
	
105-120
	

	

	



AppFlag
Length
	
AppFlag
	
node_id_len
	
node_id
	
ble_net_key_len
	
ble_net_key

其他：保留。

- Device virtual id：设备虚拟ID，22字节，设备每次被绑定时存储此虚拟ID，被重置时删除此虚拟ID。

- • 已储存文件信息： ◦ ≤V5：已储存文件MD5（目前不使用），默认填充指定长度的0x00；  说明：为了支持断点续传，这里会返回设备端已经储存的文件信息，APP在收到后，首先根据设备返回的已储存文件长度计算新文件对应长度的CRC32，然后和设备返回的CRC32对比，如果两者都吻合，那么在下面的文件起始传输请求中将起始传输偏移量改为该长度值，否则文件起始传输偏移量改为0，表示从头开始传输。  ◦ ≥V6：已储存文件内容（长度即"已储存文件长度"字段值）的摘要，设备端从已接收并储存的数据计算并返回；无已储存文件时 Digest Type=0x00、Digest Len=0。  说明：断点续传时，APP 计算新文件前"已储存文件长度"字节的摘要，与设备返回的摘要比对，两者吻合则从该长度继续传输，否则已储存数据不可信、从头传输。

SRC_TYPE：发送源。

## 8.19 OTA升级文件偏移请求：0x000E

命令帧 APP->BLE：

Data格式：

| TYPE<br>1字节 | offset<br>4字节 |
|---|---|

TYPE：0 – BLE升级固件，1 – 外部mcu升级固件，10-19：附件通道号（IOT前台创建），其他 – 保留

offset：文件起始传输偏移量，四字节。

6 – 主从一体主设备；

响应帧 BLE->APP：

Verify_key：4字节，当通过MD5(16字节login_key+16字节secret_key+authkey)前四字节加密后比对数据，如校验成功方可开启一般加密_V2版本。

| TYPE<br>1字节 | offset<br>4字节 |
|---|---|

TYPE：0 – BLE升级固件，1 – 外部mcu升级固件，10-19：附件通道号（IOT前台创建），其他 – 保留

AppFlag：APP/网关/小程序等主端的能力标志，bit0：1 – 设备通过透传通道收发DP；0 – 设备通过原有通道（0002、0027等）收发DP，其他bit保留。

> 注：实际文件传输的偏移地址应该以设备端要求的为准，且设备端要求的地址会小于等于APP端给出的偏移。

Node_id：节点id，BLE Beacon单控/群控时用到的节点id，每次配网时下发，重连时不下发。

### ble_net_key_len：1字节无符号整数，表示ble_net_key的长度。

ble_net_key：16字节无符号整数，表示BLE广播控制时候使用的key，需要时根据云端逻辑更新（避免频繁更新），每次配网时下发，重连时不下发。

DATA格式：

| TYPE<br>1字节 | 包号<br>2字节 | 当前包数据长度n<br>2字节 | 当前包数据CRC16<br>2字节 | 当前包数据<br>n字节 |
|---|---|---|---|---|

TYPE：0 – BLE升级固件信息，1 – 外部mcu升级固件信息，10-19：附件通道号（IOT前台创建），其他 – 保留。

状态码
1字节

状态码：0-成功；1-失败；2-已经在绑定状态

Data格式：

| TYPE<br>1字节 | STATE<br>1字节 |
|---|---|

TYPE：0 – BLE升级固件信息，1 – 外部mcu升级固件，10-19：附件通道号（IOT前台创建），其他 – 保留。

Data格式（当前版本最大长度限定为258）：

Dp数组1的数据
	
~
	
Dp数组n的数据


1
	
2
	
3
	
3~
	
~
	
n
	
n+1
	
n+2
	
n+3~


Dp_id
	
Dp_type
	
Dp_len
	
Dp_data
	
~
	
Dp_id
	
Dp_type
	
Dp_len
	
Dp_data

Dp_id：  1个字节，在开发平台注册的dp_id序号。

Dp_type：1个字节。

#define  DT_RAW     0

#define  DT_BOOL     1

#define  DT_VALUE    2

### #define  DT_STRING   3

#define  DT_ENUM    4

#define  DT_BITMAP   5

| TYPE<br>1字节 |
|---|

TYPE：0 – BLE升级固件信息，1 – 外部mcu升级固件，10-19：附件通道号（IOT前台创建），其他 – 保留。

注（V6 版）：设备在收到升级结束指令后，应先校验已接收文件的完整摘要与 0x000D 命令帧Digest TLV 一致，再执行签名校验；摘要校验或签名校验任一失败，均按校验失败状态返回，升级不生效。

响应帧 BLE->APP：

响应帧 BLE->APP：

状态码
1字节

| TYPE<br>1字节 | STATE<br>1字节 |
|---|---|

TYPE：0 – BLE固件信息，1 – 外部mcu升级固件，10-19：附件通道号（IOT前台创建），其他 – 保留。

8.7
设备状态查询：0x0003

命令帧 APP->BLE：

如果数据长度n=0，代表查询所有dp点数据。

如果数据长度n>0，代表查询n个dp点的数据，这n个dp点序号在data数据区。

0x03：签名校验失败

响应帧 BLE->APP：

Data格式：

## 8.22 OTA升级准备通知：0x0011

状态码：0-成功，1-dp点个数超出注册的数量，2-其他错误。

注：状态数据是通过“状态上报指令发送”。

| VERSION<br>1字节 | TYPE<br>1字节 |
|---|---|

8.8
设置登入密码：0x0004

TYPE：0 – BLE升级固件信息，1 – 外部mcu升级固件，10-19：附件通道号（IOT前台创建），其他 – 保留。

8.9
设备解绑：0x0005

命令帧 APP->BLE：

| VERSION<br>1字节 | TYPE<br>1字节 | STATE<br>1字节 |
|---|---|---|

VERSION：当前固定为0。

TYPE：0 – BLE固件信息，1 – 外部mcu升级固件，10-19：附件通道号（IOT前台创建），其他 – 保留。

Data格式：

状态码
1字节

状态码：0-成功；其他-失败

---

8.10
设备重置：0x0006

## 8.23 BLE通道产测指令：0x0012

数据帧：APP<->BLE

Data格式：

| 55AA完整产测指令或者响应<br>N个字节 |
|---|

Data格式：

## 8.24 读取设备日志信息：0x0013

状态码：0-成功；其他-失败

注：设备收到该重置指令后，解绑设备并清除login key和虚拟ID。

8.11
读取大数据摘要信息：0x0007

命令帧 APP->BLE：

| 日志信息总包数<br>1字节 | 当前包号<br>1字节 | 当前日志包长度<br>1字节 | 当前包数据<br>n字节 |
|---|---|---|---|

总包数：需要传输的日志信息总包数，如果总包数为0，说明当前没有设备日志信息。

当前包号：当前包序号，从0开始，如果总包数是2，当前包号是1，那么说明该包是最后一包。

Bulk_Type：要读取的大数据编号，由具体产品定义，从1开始，0表示通用类型数据。

包数据格式：“TDTDTDTDTD”；TD：4字节unix时间戳+1字节code。

响应帧 BLE->APP：

## 8.25 设备异常解绑：0x0014

Version
1字节
	
Bulk_Type
1字节
	
Status
1字节
	
FLAG
1字节
	
总长度
4字节
	
总CRC32
4字节
	
块长度
2字节

注：数据存储格式根据产品定义。

Version：当前必须为0。

Bulk_Type：要读取的大数据类型编号，从1开始，由具体产品定义，0表示通用类型数据。

Status：0 – 成功，后跟数据有效；1 – Type无效，后跟数据无效或者无后跟数据；其他 – 保留。

| 状态码<br>1字节 |
|---|

总长度：4字节，大端格式，本Type类型的大数据总长度。

总CRC32 ：4字节，大端格式，本Type类型的大数据总CRC32。

### 块长度：大数据读取以块为单位，手机APP每次发送要读取的块号（从0开始计数），块数据会以回复的单包长度为单位分包发送。

命令帧 APP->BLE：

8.12
读取大数据：0x0008

| type<br>1字节 | Server cert public key<br>65字节 | signature<br>64字节 |
|---|---|---|

type:

Version
1字节
	
Bulk_Type
1字节
	
块号
2字节

1 – 数据区是Server cert public key + signature；

Bulk_Type：要读取的大数据编号，从1开始由具体产品定义，0用于通用类型数据。

块号：当前要读取的块数据编号（app每发送一次读取块数据指令，设备会以响应帧中的单包长度分包发送块数据）。

响应帧 BLE->APP：

响应帧 BLE->APP

| STATE<br>1字节 | Device random<br>6字节 |
|---|---|

Version
1字节
	
Bulk_Type
1字节
	
Status
1字节
	
块号
2字节
	
本块长度
2字节
	
单包长度
2字节
	
块数据CRC16
2字节

0 – 证书验证成功，后跟6字节随机数让云端签名。

Bulk_Type：要读取的大数据类型，从1开始由具体产品定义，0表示通用类型数据。

Status：0 – 成功；1 – Type无效；2 – 块号无效；

### 块号：当前要读取的块数据编号（app每发送一次读取块数据指令，设备会以响应帧中的单包长度通过0x0009指令分包发送块数据）。

本块长度：当前块号块数据的总长度。

单包长度：本次读取的块数据上报的分包最大长度。

| type<br>1字节 | Random signature<br>64字节 |
|---|---|

type:  0 – 随机数签名；其他- 保留。

Random signature：设备所发随机数的签名。

命令帧 BLE->APP：

响应帧 BLE->APP：

Version
1字节
	
Bulk_Type
1字节
	
当前块号
2字节
	
当前包号
2字节
	
当前包长度
2字节
	
当前包数据
n字节

| STATE<br>1字节 | type<br>1字节 | Device cert<br>n字节 |
|---|---|---|

STATE：
0 – 随机数签名验证成功，如果是未绑定设备则后跟设备证书，如果是已绑定设备后面没有数据。
1 – 随机数签名验证失败，后不跟任何数据。

Bulk_Type：要读取的大数据类型，从1开始由具体产品定义，0表示通用类型数据。

Device cert：设备完整证书。

当前包号：当前块数据中的数据分包发送编号，从0开始。

## 8.28 认证信息3：0x0017

app每发送一次0x0008读取块数据指令，设备会通过该指令自动分包发送本块数据。

Data格式：

| STATE<br>1字节 | Server random<br>6字节 |
|---|---|

1.
app首先发送0x0007指令，设备回复总长度为9k字节，每块最大2K字节以及总数据的CRC32 。

2.
app 发送0x0008指令读取第一块数据，指定块号为0，设备回复了本块数据长度、本块数据CRC16，最大分包长度，app收到设备的回复后开始计时，准备接收本块数据。

3.
设备通过0x0009指令开始分包发送本块数据，app无需回复该指令，只负责接收。

4.
app通过0x0009指令接收完一整块数据后（根据接收的长度判断是否结束），开始校验本块数据，校验失败，则重新读取，检验成功读取下一块。

5.
全部读取完后，app做一次总长度和总crc检验。

6.
app全部校验通过后，按需发送数据擦除指令擦除设备已读取的大数据。

| type<br>1字节 | Random signature<br>64字节 |
|---|---|

type:  0 – 随机数签名；其他- 保留。

命令帧 APP->BLE：

## 8.29 OTA升级文件签名数据： 0x0018

Version
1字节
	
Bulk_Type
1字节

Version：当前必须为0。

| TYPE<br>1字节 | PID<br>8字节 | Signature algorithm<br>1字节 | key_version<br>1字节 | Signature_len<br>1字节 | Signature data<br>n字节=Signature_len |
|---|---|---|---|---|---|

TYPE：0 – BLE升级固件，1 – 外部mcu升级固件，10-19：附件通道号（IOT前台创建），其他 – 保留，

响应帧 BLE->APP

Signature algorithm：签名算法，1 - ECDSA，其他保留。

Version
1字节
	
Bulk_Type
1字节
	
Status
1字节

Signature_len：签名数据长度。

Bulk_Type：要擦除的大数据类型，从1开始由具体产品定义，0表示通用类型数据。

Status： 0 – 成功；1 – 失败。

响应帧 BLE->APP：

8.15
请求大数据同步：0x000B

| TYPE<br>1字节 | STATE<br>1字节 |
|---|---|

TYPE：0 – BLE升级固件，1 – 外部mcu升级固件，10-19：附件通道号（IOT前台创建），其他 – 保留，

Version
1字节
	
Request Bulk Count
1字节
	
Request Bulk Data Info
7字节
	
~
	
Request Bulk Data Info
7字节

Request Bulk Data Info格式：

1个字节
	
1个字节
	
1个字节
	
4个字节


Bulk_Type
	
FLAG
	
Res
	
总长度

2 - 签名key_version不匹配，需要通过0x0019指令给设备固件更新key；

Version：当前必须为0。

Request Bulk Count：要请求的大数据个数。

Bulk_Type：要读取的大数据类型编号，从1开始，由具体产品定义，0表示通用类型数据。

### FLAG：是否需要 APP 解析大数据，0 – 需要，1 – 不需要APP解析，需要面板或者云端解析。

Res：保留字段（20250819补充说明，之前没有该字段说明）。

数据总长度：4字节，大端格式，本Type类型的大数据总长度，跟0x0007指令中回复的总长度一致，例如可做预览使用。

| Signature algorithm<br>1字节 | key_version<br>1字节 | key_len<br>1字节 | key<br>n字节=key_len |
|---|---|---|---|

Signature algorithm：签名算法，1 - ECDSA，其他保留。

key_version：签名key的版本。

key_len：key长度。

key：验签公钥。

8.17
OTA升级请求： 0x000C

响应帧 BLE->APP：

Data格式：

| STATE<br>1字节 |
|---|

TYPE: 0 – BLE升级固件，1 – 外部mcu升级固件，10-19：附件通道号（IOT前台创建），其他 – 保留。

0 - 存储成功；

1 - 当前签名算法不支持，需要按照0x000C响应指令里的签名算法下发公钥；

2 - 未知错误；

Flag
1字节
	
OTA_Version
1字节
	
TYPE
1字节
	
Type_data
6字节
	
flag2
1字节
	
Sign_info_len
1字节
	
Sign_info_data
n字节

Flag：0x00-允许升级，0x01-拒绝升级。

### OTA_Version：OTA协议大版本，例如0x03代表3.X的协议版本,当前必须等于3，其他值会被app拒绝升级。

TYPE：0 – BLE升级固件，1 – 外部mcu升级固件，10-19：附件通道号（IOT前台创建），其他 – 保留。

Type_data：不同的type对应不同格式的type_data，TYPE为0、1、10-19对应的type_data格式如下所示：

| Version<br>1字节 |
|---|

Version：当前固件版本号，例如0x00 01 00 02代表版本为V1.0.2。

最大包长度：设备允许的单包最大长度，单位字节。

flag2：bit0 ，0 - 不支持固件验签；1 - 支持固件验签；其他bit1-7 保留（必须为0） 。

sign_info_len：sign_info_data的长度，单位字节，如果flag2的bit0为0，则sign_info_len为0，即没有后续的sign_info_data字段。

| 1 | 2 - 3 | 4 - 7 | 8 - 9 | 10 - 12 | 13 - 15 |
|---|---|---|---|---|---|
| Version | protocolVer | devAttribute | baselineVer | softVer | cadVer |
| 16 - 18 | 19 - 21 | 22 | 23 | 24 | 25 |
| cdVer | modules_softVer | modules_otaChannel | modules_online | options_isFK | options_otaChannel |
| 26 | 27 - 28 | 29 - 34 | 35 - 50 | 51 - 66 | 67 - 88 |
| Flag | packetMaxSize | hid | productKey | productKeyStr | devID |
| 89 | 90 - xx | xx | xx | xx | xx |
| uuidLen | uuid | options_udf_Len | options_udf | options_commPri_len | options_commPri |

> 注：具体内容及格式以wifi端为准。

Signature algorithm：固件支持的验签算法，1 - ECDSA ；其他值保留。

protocolVer：wifi协议版本，2个字节，例如0x0202表示v2.2 ；

注：如果固件支持验签，app或者网关需要发送0x0018指令下发固件签名信息，之后再下发0x000D升级文件信息，如果固件返回的key_version比云端最新的key版本低，app/网关需要通过0x0019指令给设备固件更新到最新的密钥版本。老版本app/网关不会解析新增的flag2字段，所以也就不会发送签名数据，固件要兼容老app，按照不支持验签的ota流程执行。

baselineVer： 基线版本，2个字节，例如0x2800表示V40.00 ；

softVer：固件版本号，3个字节，例如0x010101表示v1.1.1；

cadVer：cad版本，3个字节，例如0x010101表示v1.1.1；

cdVer：3个字节，例如0x010101表示v1.1.1；

TYPE
1字节
	
产品PID
8字节
	
文件版本
4字节
	
文件MD5
16字节
	
文件长度
4字节
	
CRC32
4字节

modules_otaChannel：mcu升级通道，1个字节；

TYPE
1字节
	
产品PID
8字节
	
文件版本
4字节
	
文件长度
4字节
	
Digest TLV
1 + 1 + n字节

options_isFK：是否OEM，1个字节， 0 – false , 1 – true ；

Digest TLV


Digest Type
	
Digest  Len
	
Digest 


1字节
	
1字节
	
N字节



	

	



0x01 - SHA256
	
32
	
~


其他 - 保留

Flag： bit0 是否需要schema, 0 – 不需要，1 – 需要；其他位保留；

•
TYPE：0 – BLE升级固件，1 – 外部mcu升级固件，10-19：附件通道号（IOT前台创建），其他 – 保留，不同的TYPE，后面对应不同的数据格式，上表为TYPE=0、1、10-19对应的格式

productkey：产品ID，16字节；

•
文件版本：例如，0x00010002代表版本为V1.0.2。

•
文件长度：文件总长度，4 字节。

•
CRC32：文件全部内容的 CRC32 值。用途为传输与存储的错误检测、以及断点续传的已储存数据比对（见响应帧说明），不承担密码学校验职能。

•
文件摘要：文件摘要。
◦
≤V5，采用MD5算法计算；
◦
≥V6，由Digest TLV决定。V6 版设备按以下两个用途使用
▪
文件身份标识：设备将其与断点信息一同储存；断点续传时，设备以该字段比对本次下发文件与已储存文件是否为同一文件，不一致则从头传输；
▪
完整性校验：设备在接收完成后（收到 0x0010 升级结束指令时）必须计算已接收文件的完整摘要并与该字段比对，不一致则按校验失败处理，升级不生效。

注：校验机制的分工
端到端完整性：Digest（摘要）负责文件身份标识与端到端完整性校验；
真实性/防篡改：固件真实性由签名机制（0x0018 签名数据下发、0x0010 结束时验签）保证，摘要校验不能替代验签。

uuid：设备uuid ；

options_udf_Len：udf长度；

options_udf：udf内容。

options_commPri_len：options communicatePriority长度，1个字节，如果为0，则不携带通道优先级，默认以云端下发的为准。

options_commPri：communicatePriority 内容数据。

TYPE
1字节
	
STATE
1字节
	
已储存文件长度
4字节
	
已储存文件摘要信息（Digest TLV）
1+1+n字节

### • TYPE：0 – BLE升级固件，1 – 外部mcu升级固件，10-19：附件通道号（IOT前台创建），其他 – 保留，不同的TYPE，后面对应不同的数据格式，上表为TYPE=0、1、10-19对应的格式。

命令帧 APP->BLE：

0x00：正常升级

JSON字符串，内容主要包括devID, secKey, localKey, schema等，app需要将所有的字段组装成标准JSON格式的字符串，然后基于该指令分包发送，分包协议采用第7节介绍的协议格式，单包最大长度采用0x001A响应帧中的packetMaxSize。

注：对于app的每一个分包，设备都会响应，app只有收到对应的分包响应后才能发送下一分包。

0x03：文件大小超过范围。

响应帧：BLE->APP

Data格式：

| status<br>1字节 | 已接收的数据长度<br>4字节 | 当前分包长度<br>2字节 | 已接收的数据长度<br>4字节 | 总长度<br>4字节 |
|---|---|---|---|---|

•
已储存文件信息：
◦
≤V5：已储存文件MD5（目前不使用），默认填充指定长度的0x00；

说明：为了支持断点续传，这里会返回设备端已经储存的文件信息，APP在收到后，首先根据设备返回的已储存文件长度计算新文件对应长度的CRC32，然后和设备返回的CRC32对比，如果两者都吻合，那么在下面的文件起始传输请求中将起始传输偏移量改为该长度值，否则文件起始传输偏移量改为0，表示从头开始传输。

◦
≥V6：已储存文件内容（长度即"已储存文件长度"字段值）的摘要，设备端从已接收并储存的数据计算并返回；无已储存文件时 Digest Type=0x00、Digest Len=0。

说明：断点续传时，APP 计算新文件前"已储存文件长度"字节的摘要，与设备返回的摘要比对，两者吻合则从该长度继续传输，否则已储存数据不可信、从头传输。

1 – 当前分包有效，可以发送下一包；

8.19
OTA升级文件偏移请求：0x000E

3 – 其他错误，蓝牙即将断开；

当前分包序号：固定2字节；

TYPE
1字节
	
offset
4字节

TYPE：0 – BLE升级固件，1 – 外部mcu升级固件，10-19：附件通道号（IOT前台创建），其他 – 保留

总长度：4字节，需要接收的数据总长度，该值在app发送第一个分包时包含。

## 8.33 发送 WIFI SSID 信息：0x001C

命令帧 APP->BLE：

TYPE
1字节
	
offset
4字节

TYPE：0 – BLE升级固件，1 – 外部mcu升级固件，10-19：附件通道号（IOT前台创建），其他 – 保留

```
{
    "ssid":"tuya_test",
    "passwd":"12345678",
}
```

注：实际文件传输的偏移地址应该以设备端要求的为准，且设备端要求的地址会小于等于APP端给出的偏移。

响应帧 BLE->APP：

8.20
OTA升级数据： 0x000F

| status<br>1字节 |
|---|

status：0 – 接收成功，1 -接收失败。

> 注：该指令主要在wifi设备云端已激活的场景下使用，主要用于更新设备存储的AP信息或者在蓝牙通道激活设备的时候首次给设备发送SSID信息。

TYPE：0 – BLE升级固件信息，1 – 外部mcu升级固件信息，10-19：附件通道号（IOT前台创建），其他 – 保留。

## 8.34 TUYA数据透传：0x001D

命令帧 APP<->BLE：

DATA ：透传数据，最大258字节。

> 该指令主要用于TUYA内部业务数据透传，内容格式由APP统一维护。

TYPE：0 – BLE升级固件信息，1 – 外部mcu升级固件，10-19：附件通道号（IOT前台创建），其他 – 保留。

## 8.35 WIFI设备上报联网状态：0x001E

命令帧 BLE->APP：

0x01：包号异常

| network_status<br>1字节 |
|---|

network_status：1字节，联网状态码，定义如下：

0 – 局域网和MQTT都离线；

1 – 局域网在线，MQTT离线

8.21
OTA升级结束：0x0010

命令帧 APP->BLE：

## 8.36 ECDH密钥协商：0x0020

命令帧 APP->BLE：

TYPE：0 – BLE升级固件信息，1 – 外部mcu升级固件，10-19：附件通道号（IOT前台创建），其他 – 保留。

| CURVE TYPE<br>1字节 | PUBLIC KEY<br>64 or n |
|---|---|

CURVE TYPE：加密曲线类型
0 - secp160r1
1 - secp192r1
2 - secp224r1
3 - secp256r1
4 - secp256k1

PUBLIC KEY：公钥。

Data格式：

TYPE
1字节
	
STATE
1字节

TYPE：0 – BLE固件信息，1 – 外部mcu升级固件，10-19：附件通道号（IOT前台创建），其他 – 保留。

STATE：

## 8.37 发送配网信息：0x0021

命令帧 APP->BLE：

DATA 格式：采用json无格式字符串，包含结束符。

```
{
    "mac":"aabbccddeeff",
    "ssid":"tuya_test",
    "passwd":"12345678",
    "token":"AYaawossptx3ma"
}
```

其他: 保留

响应帧 BLE->APP:

8.22
OTA升级准备通知：0x0011

| STATE<br>1字节 |
|---|

state：0 – 接收成功，1 -接收失败。

VERSION
1字节
	
TYPE
1字节

## 8.38 上报配网状态：0x0022

TYPE：0 – BLE升级固件信息，1 – 外部mcu升级固件，10-19：附件通道号（IOT前台创建），其他 – 保留。

Data格式：

| Result<br>2字节 |
|---|

Result：2字节结果码，蓝牙透传，值由wifi端定义。

VERSION
1字节
	
TYPE
1字节
	
STATE
1字节

VERSION：当前固定为0。

TYPE：0 – BLE固件信息，1 – 外部mcu升级固件，10-19：附件通道号（IOT前台创建），其他 – 保留。

| Status<br>1字节 |
|---|

Status：0 – 接收成功，1 -接收失败。

0x01：失败

## 8.39 数据透传：0x0023

命令帧 APP<->BLE：

8.23
BLE通道产测指令：0x0012

数据帧：APP<->BLE

响应帧：DATA无。

55AA完整产测指令或者响应
N个字节

## 8.40 唤醒指令：0x0024（目前仅用于IPC）

8.24
读取设备日志信息：0x0013

命令帧 APP->BLE：

DATA：无

## 8.41 APP主动同步时间：0x0025

命令帧 APP->BLE：

Data格式：

| Type=0<br>1字节 | Unix ms级时间<br>13字节字符串 | Time_zone<br>2字节时区（有符号型） |
|---|---|---|

总包数：需要传输的日志信息总包数，如果总包数为0，说明当前没有设备日志信息。

| 1字节 | 1字节 | 1字节 | 1字节 | 1字节 | 1字节 | 1字节 | 1字节 | 2字节（有符号型） |  |
|---|---|---|---|---|---|---|---|---|---|
| Type=1 | 年 | 月 | 日 | 时 | 分 | 秒 | 星期 | 时区高 | 时区低 |

注：如 2019 年4 月 28 日12:23:25 星期二， 应按如下顺序发送：0x13,0x04,0x1C,0x0C,0x17,0x19,0x02。

包数据格式：“TDTDTDTDTD”；TD：4字节unix时间戳+1字节code。

时区：实际时区的100倍，例如北京东八区为8x100=800，西7.5区为-750；

8.25
设备异常解绑：0x0014

响应帧 BLE->APP：

Data格式：

| State<br>1字节 |
|---|

state：0 – 成功；1 – 失败。

Data：

## 8.42 查询多协议设备配网状态：0x0026

状态码：0-成功；其他-失败。

DATA：无

8.26
认证信息1：0x0015

响应帧 BLE->APP：

DATA 格式：

| flag<br>1字节 | len<br>1字节 | data<br>n字节=len |
|---|---|---|

flag：
1 – 设备当前未配网，对应len=0，无data数据区。
2 – 设备当前已联网，未激活，对应data为当前token数据，len为token长度。
3 – 设备当前已激活，对应data为当前token数据，len为token长度。
其他值–无效。

0 – 数据区没有内容，主要用于已绑定设备连接时不发送证书的场景；

# 1 – 数据区是Server cert public key + signature；

其他 – 保留。

Data格式：

| 1 | 2 | 3 |  | 4 | 5 | 6 | 7 | 8 | 9 |
|---|---|---|---|---|---|---|---|---|---|
| VERSION | R_SN |  |  |  |  | DP1_ID | DP1_TYPE | DP1_LEN |  |
| 10 ~ (10+DP1_LEN) |  | ~ | n |  | n+1 | n+2 | n+3 | n+4 ~ |  |
| DP1_DATA |  | ~ | DPn_ID |  | DPn_TYPE | DPn_LEN |  | DPn_DATA |  |

VERSION：指令格式版本号，当前为0 。

STATE
1字节
	
Device random
6字节

Dp_id：  1个字节，在开发平台注册的dp点序号。

0 – 证书验证成功，后跟6字节随机数让云端签名。

#define  DT_RAW     0

#define  DT_BOOL     1

#define  DT_VALUE    2

#define  DT_STRING   3

#define  DT_ENUM    4

type
1字节
	
Random signature
64字节

Dp_len：2个字节，数据长度，最大长度限制以iot平台为准。

Random signature：设备所发随机数的签名。

响应帧 BLE->APP：

Data格式：

| VERSION<br>1字节 | R_SN<br>4字节 |  |  |  | STATE<br>1字节 |
|---|---|---|---|---|---|

STATE：
0 – 随机数签名验证成功，如果是未绑定设备则后跟设备证书，如果是已绑定设备后面没有数据。
1 – 随机数签名验证失败，后不跟任何数据。

type：0 – 数据区是设备完整证书；其他 – 保留。

Device cert：设备完整证书。

0x01：失败

8.28
认证信息3：0x0017

## 8.44 带数据源dp点数据下发：0x0028（PV≥4.5）

命令帧 APP->BLE：

STATE
1字节
	
Server random
6字节

| 1 | 2 | 3 | 3 | 4 | 5 | 6 | 5 | 7 | x~(7+ADD_INFO_LEN) | 7 |
|---|---|---|---|---|---|---|---|---|---|---|
| x~(7+ADD_INFO_LEN) | VERSION | R_SN |  |  |  | DP_SRC_TYPE |  | ADD_INFO_LEN | ADD_INFO |  |
| n | n+1 | n+2 | n+3 | n+4~ | n+4~ |  |  | ~ | m | m+1 |
| DP1_ID | DP1_TYPE | DP1_LEN |  |  | DP1_DATA | DP1_DATA |  |  | DPn_ID | DPn_TYPE |
| DPn_TYPE | m+3 | m+4~ |  |  |  |  |  | ~ |  |  |
| DPn_LEN |  | DPn_DATA |  |  |  |  |  |  |  |  |

0 – 证书验证成功，后跟6字节随机数让设备签名。

R_SN：发送序号，四字节，高字节在前。

DP_SRC_TYPE：数据源类型。

0 – 主体设备；
1 – 配件；
2 – 主从一体从设备。

ADD_INFO_LEN：数据源补充信息长度。

ADD_INFO：数据源补充信息。当数据源为主体设备时，无补充信息；但数据源为配件时，该字段存放配件的ID_INFO：格式如下：

| 1 | 2 | 3~ |
|---|---|---|
| ID_TYPE | ID_LEN | ID_DATA |

ID_TYPE：

0 – 配件的SHORT_ID，配件激活时由云端下发；

1 – 主从一体从设备的UUID；

Data格式：

TYPE
1字节
	
PID
8字节
	
Signature algorithm
1字节
	
key_version
1字节
	
Signature_len
1字节
	
Signature data
n字节=Signature_len

TYPE：0 – BLE升级固件，1 – 外部mcu升级固件，10-19：附件通道号（IOT前台创建），其他 – 保留，

PID：固件PID。

Signature algorithm：签名算法，1 - ECDSA，其他保留。

key_version：签名key的版本。

#define  DT_RAW     0

#define  DT_BOOL     1

#define  DT_VALUE    2

#define  DT_STRING   3

#define  DT_ENUM    4

TYPE
1字节
	
STATE
1字节

TYPE：0 – BLE升级固件，1 – 外部mcu升级固件，10-19：附件通道号（IOT前台创建），其他 – 保留，

Dp_data：数据，dp_len个字节。

0 - 存储成功；

1 - 当前签名算法不支持，需要按照0x000C响应指令里的签名算法签名；

2 - 签名key_version不匹配，需要通过0x0019指令给设备固件更新key；

| 1 | 2 | 3 | 4 | 5 | 6 |
|---|---|---|---|---|---|
| VERSION | R_SN |  |  |  | DP_SRC_TYPE |
| 7 | X~(7+ADD_INFO_LEN) | n |  |  |  |
| ADD_INFO_LEN | ADD_INFO | STATE |  |  |  |

VERSION、R_SN、DP_SRC_TYPE、ADD_INFO_LEN、ADD_INFO：取命令帧对应数据。

STATE：

8.30
OTA升级文件签名密钥更新： 0x0019

命令帧 APP->BLE：

Data格式：

### Signature algorithm 1字节 key_version 1字节 key_len 1字节 key n字节=key_len

Signature algorithm：签名算法，1 - ECDSA，其他保留。

key_version：签名key的版本。

| 1 | 2 | 3 | x~(7+ADD_INFO_LEN) | n | m~ |
|---|---|---|---|---|---|
| VERSION | DP_SRC_TYPE | ADD_INFO_LEN | ADD_INFO | NUM | DP_LIST |

VERSION：指令格式版本号，当前为0 。

DP_SRC_TYPE：数据源类型。
0 – 主体设备；
1 – 配件；
2 – 主从一体从设备。

ADD_INFO_LEN：数据源补充信息长度。

ADD_INFO：数据源补充信息。当数据源为主体设备时，无补充信息；但数据源为配件时，该字段存放配件的ID_INFO：格式如下：

| 1 | 2 | 3~ |
|---|---|---|
| ID_TYPE | ID_LEN | ID_DATA |

ID_TYPE：

0 – 配件的SHORT_ID，配件激活时由云端下发；

1 - 当前签名算法不支持，需要按照0x000C响应指令里的签名算法下发公钥；

2 - 未知错误；

ID_LEN：ID的数据长度。

ID_DATA：ID的数据内容。

8.31
查询Combo WIFI设备信息： 0x001A

如果NUM >0，代表查询NUM个dp点的数据，这NUM个dp点序号在DP_LIST数据区。

Data格式：

Version
1字节

Version：指令版本号，当前为0 。

| 1 | 2 | 3 | x~(7+ADD_INFO_LEN) | n |
|---|---|---|---|---|
| VERSION | DP_SRC_TYPE | ADD_INFO_LEN | ADD_INFO | STATUS |

STATUS：0-成功，1-dp点个数超出注册的数量，2-其他错误。

> 注：状态数据通过“状态上报指令发送”。

1
	
2 - 3
	
4 - 7
	
8 - 9
	
10 - 12
	
13 - 15


Version
	
protocolVer
	
devAttribute
	
baselineVer
	
softVer
	
cadVer


16 - 18
	
19 - 21
	
22
	
23
	
24
	
25


cdVer
	
modules_softVer
	
modules_otaChannel
	
modules_online
	
options_isFK
	
options_otaChannel


26
	
27 - 28
	
29 - 34
	
35 - 50
	
51 - 66
	
67 - 88


Flag
	
packetMaxSize
	
hid
	
productKey
	
productKeyStr
	
devID


89
	
90 - xx
	
xx
	
xx
	
xx
	
xx


uuidLen
	
uuid
	
options_udf_Len
	
options_udf
	
options_commPri_len
	
options_commPri

### 注：具体内容及格式以wifi端为准。

一次会话可能需要多次语音交互，单次语音交互的会话流程如下：

protocolVer：wifi协议版本，2个字节，例如0x0202表示v2.2 ；

![Feishu Docs - Image](blob:https://icn602w9tnqf.feishu.cn/899f0006-e1e5-4e0b-9ac7-31ee392f7adb)

baselineVer： 基线版本，2个字节，例如0x2800表示V40.00 ；

softVer：固件版本号，3个字节，例如0x010101表示v1.1.1；

cadVer：cad版本，3个字节，例如0x010101表示v1.1.1；

![Feishu Docs - Image](blob:https://icn602w9tnqf.feishu.cn/a7a3ad59-773b-4993-a3da-ee37a8741b1a)

modules_softVer：mcu软件版本，3个字节，例如0x010101表示v1.1.1；

语音类指令协议版本约束：从V6开始，VERSION字段升级使用0x01。涉及指令包括：

1. modules_online：线上，1个字节，0 – false , 1 – true ；

1. options_isFK：是否OEM，1个字节， 0 – false , 1 – true ；

1. options_otaChannel：wifi固件升级通道，1个字节；

1. Flag： bit0 是否需要schema, 0 – 不需要，1 – 需要；其他位保留；

1. packetMaxSize：2字节，发送设备激活信息时，分包的最大长度；

1. 6. 语音授权token数据上报：0x0035（分包）

1. 7. 语音授权token数据下发：0x0036（分包）

1. devID：设备ID，22字节；

hid：6字节，mac地址；

## 8.47 语音传输控制：0x0030

命令帧 APP<->BLE：

options_udf_Len：udf长度；

| 1 | 2 | 3 | 4 - x |
|---|---|---|---|
| VERSION | isACK | cmd | content_data |

options_commPri_len：options communicatePriority长度，1个字节，如果为0，则不携带通道优先级，默认以云端下发的为准。

options_commPri：communicatePriority 内容数据。

cmd：控制码，如果isACK为1，表明该帧是本cmd的响应帧，控制码定义如下：

- 8.32 发送设备激活信息： 0x001B（分包）

- • 0x02 – Provide Speech；

- • 0x03 – Endpoint Speech；

- JSON字符串，内容主要包括devID, secKey, localKey, schema等，app需要将所有的字段组装成标准JSON格式的字符串，然后基于该指令分包发送，分包协议采用第7节介绍的协议格式，单包最大长度采用0x001A响应帧中的packetMaxSize。

- 注：对于app的每一个分包，设备都会响应，app只有收到对应的分包响应后才能发送下一分包。

content data：

isACK=0 , cmd = 0x01：Start Speech

| content data |  |  |  |  |  |
|---|---|---|---|---|---|
| 1字节 | 1字节 | 1字节 | 4字节 | 1字节 | 2字节 |
| audio format | audio profile | suppressEarcon | dialog id | play voice | result data packetMaxSize |

status
1字节
	
当前分包序号
2字节
	
当前分包长度
2字节
	
已接收的数据长度
4字节
	
总长度
4字节

0 – PCM_L16_16KHZ_MONO;

1 – OPUS_16KHZ_32KBPS_CBR_0_20MS;

2 – OPUS_16KHZ_16KBPS_CBR_0_20MS;

3 – MSBC;

当前分包序号：固定2字节；

audio profile：

0 – CLOSE_TALK  ;

总长度：4字节，需要接收的数据总长度，该值在app发送第一个分包时包含。

2 – FAR_FIELD ;

suppressEarcon：耳标抑制，0 – 播放；1 – 不播放。

dialog id ：对话id，4字节，从0开始。

play voice：是否语音播放识别结果，0 – 不播放；1 – 播放（如果播放则不需要发送0032指令的文字信息）。

result data packetMaxSize ：App发送语音识别结果数据时，分包的最大长度。

Code block
C
{
    "ssid":"tuya_test",
    "passwd":"12345678",
}

isACK=1 , cmd = 0x01：Start Speech ACK

| content data |  |
|---|---|
| 4字节 | 1字节 |
| dialog id | status |

dialog id同上；

status
1字节

0 - 成功；1 – 未授权；2 – 无网络；3 – 网络异常；

注：该指令主要在wifi设备云端已激活的场景下使用，主要用于更新设备存储的AP信息或者在蓝牙通道激活设备的时候首次给设备发送SSID信息。

isACK=0 , cmd = 0x02：Provide Speech

| content data |
|---|
| 4字节 |
| dialog id |

dialog id : 必须和 Start Speech中一样。

DATA ：透传数据，最大258字节。

isACK=1 , cmd = 0x02：Provide Speech ACK

| content data |  |  |
|---|---|---|
| 1字节 | 1字节 | 4字节 |
| audio format | audio profile | dialog id |

dialog id : 必须和 Start Speech中一样。

audio format 和 audio profile 详见 Start Speech 指令说明。

Data格式：

isACK=0 , cmd = 0x03：Endpoint Speech

| content data |
|---|
| 4字节 |
| dialog id |

0 – 局域网和MQTT都离线；

isACK=1 , cmd = 0x03：Endpoint Speech ACK

| content data |
|---|
| 4字节 |
| dialog id |

isACK=0 , cmd = 0x04：Stop Speech

| content data |  |
|---|---|
| 1字节 | 4字节 |
| error code | dialog id |

error code ：

CURVE TYPE
1字节
	
PUBLIC KEY
64 or n

CURVE TYPE：加密曲线类型
0 - secp160r1
1 - secp192r1
2 - secp224r1
3 - secp256r1
4 - secp256k1

2 – INTERNAL ；

3 – UNSUPPORTED ；

4 – USER_CANCELLED ；

5 – NOT_FOUND ；

6 – INVALID ；

8.37
发送配网信息：0x0021

isACK=1 , cmd = 0x04：Stop Speech ACK

| content data |
|---|
| 4字节 |
| dialog id |

Code block
C
{
    "mac":"aabbccddeeff",
    "ssid":"tuya_test",
    "passwd":"12345678",
    "token":"AYaawossptx3ma"
}

isACK=0 , cmd = 0x05：Notify Speech State

| content data |
|---|
| 1字节 |
| state |

app state ：

STATE
1字节

state：0 – 接收成功，1 -接收失败。

2 – PROCESSING ；

8.38
上报配网状态：0x0022

命令帧 BLE->APP：

## 8.48 语音传输数据：0x0031

Result
2字节

Result：2字节结果码，蓝牙透传，值由wifi端定义。

| 1 | 2-3 | 4 | 5 | 6 - 7 | 8- x |
|---|---|---|---|---|---|
| VERSION | Sn | Type | Flag | Length | Audio Data |

VERSION：指令格式版本号定义详见，≤V5的版本=0， ≥V6的版本=1 。

Sn：发送序号，2字节，高字节在前。

Type_bit6-bit4 ：保留。

Type_bit7 ：    0 – 需要响应帧；1 – 不需要响应帧。

Flag：bit3-bit0：audio format，见 start speech参数说明，其他bit保留。

Length ：语音数据长度；

Audio Data：语音数据。

DATA ：透传数据，不解析。

响应帧 APP->BLE：

响应帧：DATA无。

| 1 | 2 | 3 | 4 | 5 | 6 |
|---|---|---|---|---|---|
| VERSION | Sn |  | Type | Flag | Status |

VERSION、Sn、TYPE、FLAG：取命令帧的对应数据。

命令帧 APP<->BLE：DATA无

响应帧：DATA无。

0x01：失败

8.41
APP主动同步时间：0x0025

## 8.49 语音识别结果数据：0x0032（分包）

命令帧 APP->BLE：

Type=0
1字节
	
Unix ms级时间
13字节字符串
	
Time_zone
2字节时区（有符号型）

| 1 | 2 | 3 - 4 | 5- x |
|---|---|---|---|
| VERSION | type | length | result data |

1字节
	
1字节
	
1字节
	
1字节
	
1字节
	
1字节
	
1字节
	
1字节
	
2字节（有符号型）


Type=1
	
年
	
月
	
日
	
时
	
分
	
秒
	
星期
	
时区高
	
时区低

注：如 2019 年4 月 28 日12:23:25 星期二， 应按如下顺序发送：0x13,0x04,0x1C,0x0C,0x17,0x19,0x02。

length ：数据长度；

result data：需要显示或者播报的结果数据，不同的type对应不同的格式，如下所示。

Data 分包发送，分包协议采用第7节介绍的协议格式，单包最大长度采用Start Speech 中的packetMaxSize。

注：对于app的每一个分包，设备都会响应，app只有收到对应的分包响应后才能发送下一分包。

result data格式：

State
1字节

| Result data |
|---|
| Length 个字节 |
| 语音识别常规数据 |

8.42
查询多协议设备配网状态：0x0026

| Result data |  |  |  |  |  |  |
|---|---|---|---|---|---|---|
| 1字节 | n字节 | 1字节 | n字节 | 1字节 | 1字节 | n字节 |
| mainTitle<br>Length | mainTitle <br>string | subtitle<br>Length | subtitle <br>string | Temperature unit | Weather Days | Weather data |

| Weather data |  |  |  |  |  |  |
|---|---|---|---|---|---|---|
| 要查询日期的天气 |  | 1-7天的天气 |  |  |  |  |
| Length | Data | Length | Data | … | Length | Data |
| 2字节 | n字节 | 1字节 | n字节 | … | 1字节 | n字节 |
| n | query day weather data | n | Daily<br>data | … | n | Daily<br>data |

| query day weather data |  |  |  |  |  |  |  |  |
|---|---|---|---|---|---|---|---|---|
| 1字节 | 2字节 | 2字节 | 2字节 | 2字节 | 1字节 | n字节 | 1字节 | n字节 |
| Flag | 天气<br>类型 | 当前<br>温度 | 最低<br>温度 | 最高<br>温度 | TTS_Length | TTS String | DescriptionLength | Description<br>String |

| Daily data |  |  |  |  |  |  |  | 2字节 |
|---|---|---|---|---|---|---|---|---|
| 2字节 | 2字节 | 2字节 | 1字节 | n字节 | 1字节 | n字节 | 1字节 | 天气<br>类型 |
| 天气<br>类型 | 最低<br>温度 | 最高<br>温度 | Date<br>Length | Date String | Day<br>Length | Day<br>String | Description<br>Length | Description<br>String |

mainTitle ：主标题，内容为城市名，如：Hangzhou,China

subtitle ：次标题，内容为日期，如：Wednesday, June 16, 2021

flag：
1 – 设备当前未配网，对应len=0，无data数据区。
2 – 设备当前已联网，未激活，对应data为当前token数据，len为token长度。
3 – 设备当前已激活，对应data为当前token数据，len为token长度。
其他值–无效。

Weather Days ：后跟的天气数据是多少天的数据（包含查询的日期的天气）

8.43
dp点数据下发：0x0027（PV≥4.0）

查询日期即为subtitle显示的日期。

Flag：bit0 – 是否包含当前温度；其他bit保留。

1
	
2
	
3
	
4
	
5
	
6
	
7
	
8
	
9


VERSION
	
R_SN
	
DP1_ID
	
DP1_TYPE
	
DP1_LEN


10 ~ (10+DP1_LEN)
	
~
	
n
	
n+1
	
n+2
	
n+3
	
n+4 ~


DP1_DATA
	
~
	
DPn_ID
	
DPn_TYPE
	
DPn_LEN
	
DPn_DATA

VERSION：指令格式版本号，当前为0 。

Description Length：天气类型描述字符串长度。

Description String：天气类型描述字符串，例如Rainy，sunny等。

Dp_type：1个字节。

type=2 时（日历、toDOlist数据）：

| Result data |  |  |  |  |  |
|---|---|---|---|---|---|
| 1字节 | n字节 | 1字节 | n字节 | 1字节 | n字节 |
| mainTitleLength | mainTitle string | subtitleLength | subtitle string | list number | list data |

| List data |  |  |  |  |  |
|---|---|---|---|---|---|
| List_1 data |  |  |  | … | List_n data |
| 1字节 | n字节 | 1字节 | n字节 | … |  |
| leftTextLength | leftTextString | rightTextLength | rightTextString | … | 内容同List_1 |

mainTitle ：主标题，内容为日历或者list，如：To Do List

subtitle ：次标题，内容为list个数或者日期，如：Wednesday, June 16, 2021 或者 2 items

List number：后面的List个数。

leftTextLength，LeftTextString ：List左标题。

rightTextLength，rightTextString ：List右标题。

响应帧 BLE->APP：

Data格式：

| 1 | 2 - 3 | 4 - 5 | 6 - 9 | 10 - 13 |
|---|---|---|---|---|
| status | 当前分包序号 | 当前分包长度 | 已接收的数据长度 | 总长度 |

VERSION、R_SN：取命令帧对应数据。

1 – 当前分包有效，可以发送下一包；

2 – 当前分包错误，需要从头发送；

3 – 其他错误，蓝牙即将断开；

当前分包序号：固定2字节；

8.44
带数据源dp点数据下发：0x0028（PV≥4.5）

已接收的数据长度：4字节，表示目前为止已经接收到的字节数；

总长度：4字节，需要接收的数据总长度，该值在app发送第一个分包时包含。

1
	
2
	
3
	
4
	
5
	
6
	
7
	
x~(7+ADD_INFO_LEN)


VERSION
	
R_SN
	
DP_SRC_TYPE
	
ADD_INFO_LEN
	
ADD_INFO


n
	
n+1
	
n+2
	
n+3
	
n+4~
	
~
	
m
	
m+1


DP1_ID
	
DP1_TYPE
	
DP1_LEN
	
DP1_DATA
	

	
DPn_ID
	
DPn_TYPE


m+2
	
m+3
	
m+4~
	

	

	
~
	

	



DPn_LEN
	
DPn_DATA

### VERSION：指令格式版本号，当前为0 。

R_SN：发送序号，四字节，高字节在前。

DP_SRC_TYPE：数据源类型。

| VERSION<br>1字节 | cmd<br>1字节 | type<br>1字节 | Token MD5（≤V5） /<br>Token Digest TLV（≥V6）<br>n字节 | time<br>4字节 | Loop count<br>1字节 | loopcountpausetime<br>4字节 | Reminder text Length<br>2字节 | Reminder text<br>N字节 |
|---|---|---|---|---|---|---|---|---|

VERSION：指令格式版本号定义详见，≤V5的版本=0， ≥V6的版本=1 。

ADD_INFO：数据源补充信息。当数据源为主体设备时，无补充信息；但数据源为配件时，该字段存放配件的ID_INFO：格式如下：

1
	
2
	
3~


ID_TYPE
	
ID_LEN
	
ID_DATA

Token摘要：描述Token摘要信息
◦
Token MD5： VERISON=0时，为MD5摘要。(≤V5使用)
◦
Token Digest TLV：VERSION=1时，格式详见Digest TLV。(≥V6使用)

time ： 定时、闹钟、提醒的时间，4字节unix时间戳。

Loop count：循环次数，0表示只提醒一次，提醒和闹钟采用，定时没有，定时时值为0。

loopcountpausetime：循环次数不为0时才有意义，内容为循环提醒时的间隔时间，单位ms；

Reminder text Length：提醒文本数据的长度。

Reminder text ：提醒文本，字符串格式。

Dp_id：  1个字节，在开发平台注册的dp点序号。

Dp_type：1个字节。

| VERSION<br>1字节 | cmd<br>1字节 | type<br>1字节 | Token MD5（≤V5） /<br>Token Digest TLV（≥V6）<br>n字节 | Status<br>1字节 |
|---|---|---|---|---|

VERSION、cmd、type、token 摘要：取命令帧的对应数据。

#define  DT_VALUE    2

#define  DT_STRING   3

#define  DT_ENUM    4

#define  DT_BITMAP   5

### Dp_len：2个字节，数据长度，最大长度限制以iot平台为准。

Dp_data：数据，dp_len个字节。

Data格式：

| VERSION<br>1字节 | token data request<br>1字节 |
|---|---|

VERSION：指令格式版本号定义详见，≤V5的版本=0， ≥V6的版本=1 。

1
	
2
	
3
	
4
	
5
	
6


VERSION
	
R_SN
	
DP_SRC_TYPE


7
	
X~(7+ADD_INFO_LEN)
	
n
	

	

	



ADD_INFO_LEN
	
ADD_INFO
	
STATE

注：该指令响应帧只返回token的MD5（≤V5）/ Token Digest TLV（≥V6） ，token数据通过0x0035上报。,

STATE：

响应帧 BLE->APP：

Data格式：

| VERSION<br>1字节 | Status<br>1字节 | Token MD5（≤V5） /<br>Token Digest TLV（≥V6）<br>n字节 |
|---|---|---|

VERSION: 指令格式版本号定义详见，≤V5的版本=0， ≥V6的版本=1 。

命令帧 APP->BLE：

0x00：成功，后跟有效token数据。

1
	
2
	
3
	
x~(7+ADD_INFO_LEN)
	
n
	
m~


VERSION
	
DP_SRC_TYPE
	
ADD_INFO_LEN
	
ADD_INFO
	
NUM
	
DP_LIST

Token摘要：描述Token摘要信息
◦
Token MD5： VERISON=0时，为MD5摘要。(≤V5使用)
◦
Token Digest TLV：VERSION=1时，格式详见Digest TLV。(≥V6使用)

DP_SRC_TYPE：数据源类型。
0 – 主体设备；
1 – 配件；
2 – 主从一体从设备。

## 8.52 语音授权token数据上报：0x0035（分包）

ADD_INFO：数据源补充信息。当数据源为主体设备时，无补充信息；但数据源为配件时，该字段存放配件的ID_INFO：格式如下：

1
	
2
	
3~


ID_TYPE
	
ID_LEN
	
ID_DATA

| 1 | 2 - 3 | 4- x |
|---|---|---|
| VERSION | Token length | token data |

VERSION：指令格式版本号定义详见，≤V5的版本=0， ≥V6的版本=1 。

Token length ：token数据长度；

token data：token数据。

> Data 分包发送，分包协议采用第7节介绍的协议格式，单包最大长度采用Start Speech 中的packetMaxSize。
> 注：对于设备的每一个分包，APP都要响应，设备只有收到对应的分包响应后才能发送下一分包。

ID_DATA：ID的数据内容。

NUM：如果NUM =0，代表查询所有dp点数据。

如果NUM >0，代表查询NUM个dp点的数据，这NUM个dp点序号在DP_LIST数据区。

| 1 | 2 - 3 | 4 - 5 | 6 - 9 | 10 - 13 |
|---|---|---|---|---|
| status | 当前分包序号 | 当前分包长度 | 已接收的数据长度 | 总长度 |

status： 0 – 全部接收完成；

1 – 当前分包有效，可以发送下一包；

1
	
2
	
3
	
x~(7+ADD_INFO_LEN)
	
n


VERSION
	
DP_SRC_TYPE
	
ADD_INFO_LEN
	
ADD_INFO
	
STATUS

STATUS：0-成功，1-dp点个数超出注册的数量，2-其他错误。

注：状态数据通过“状态上报指令发送”。

当前分包长度：固件2字节；

已接收的数据长度：4字节，表示目前为止已经接收到的字节数；

总长度：4字节，需要接收的数据总长度，该值在设备发送第一个分包时包含。

## 8.53 语音授权token数据下发：0x0036（分包）

命令帧 APP->BLE：

一次会话包含多次语音交互的流程：

| VERSION<br>1字节 | Token MD5（≤V5） /<br>Token Digest TLV（≥V6）<br>n字节 | Token length<br>2字节 | token data<br>n字节 |
|---|---|---|---|

VERSION：指令格式版本号定义详见，≤V5的版本=0， ≥V6的版本=1 。

Token摘要：描述Token摘要信息

- 语音类指令协议版本约束：从V6开始，VERSION字段升级使用0x01。涉及指令包括：

- • Token Digest TLV：VERSION=1时，格式详见Digest TLV。(≥V6使用)

Token length ：token数据长度；

3.
语音识别结果数据：0x0032（分包）

> Data 分包发送，分包协议采用第7节介绍的协议格式，单包最大长度采用Start Speech 中的packetMaxSize。
> 注：对于每一个分包，设备都要响应，APP只有收到对应的分包响应后才能发送下一分包。

5.
语音授权token读取：0x0034

6.
语音授权token数据上报：0x0035（分包）

7.
语音授权token数据下发：0x0036（分包）

| 1 | 2 - 3 | 4 - 5 | 6 - 9 | 10 - 13 |
|---|---|---|---|---|
| status | 当前分包序号 | 当前分包长度 | 已接收的数据长度 | 总长度 |

status： 0 – 全部接收完成；

1 – 当前分包有效，可以发送下一包；

2 – 当前分包错误，需要从头发送；

3 – 其他错误，蓝牙即将断开；

1
	
2
	
3
	
4 - x


VERSION
	
isACK
	
cmd
	
content_data

VERSION：指令格式版本号定义详见，≤V5的版本=0， ≥V6的版本=1 。

已接收的数据长度：4字节，表示目前为止已经接收到的字节数；

cmd：控制码，如果isACK为1，表明该帧是本cmd的响应帧，控制码定义如下：

•
 0x01 – Start Speech；

### • 0x02 – Provide Speech；

•
0x03 – Endpoint Speech；

•
0x04 – Stop Speech；

| 1 | 2 | 3 | 4-n |
|---|---|---|---|
| VERSION | subCmd | data length | data |

VERSION：指令格式版本号定义详见，≤V5的版本=0， ≥V6的版本=1 。

subCmd：0 – 清空授权token；1 – 设置alexa通知状态；其他–保留。

content data


1字节
	
1字节
	
1字节
	
4字节
	
1字节
	
2字节


audio format
	
audio profile
	
suppressEarcon
	
dialog id
	
play voice
	
result data packetMaxSize

当subCmd为0时，data length为0，无data数据。

0 – PCM_L16_16KHZ_MONO;

| 1 |
|---|
| notify_flag |

2 – OPUS_16KHZ_16KBPS_CBR_0_20MS;

3 – MSBC;

响应帧 BLE->APP：

audio profile：

| 1 | 2 | 3 | 4 | 5-n |
|---|---|---|---|---|
| VERSION | VERSION | Status | data length | data |

VERSION：指令格式版本号定义详见，≤V5的版本=0， ≥V6的版本=1 。

2 – FAR_FIELD ;

suppressEarcon：耳标抑制，0 – 播放；1 – 不播放。

dialog id ：对话id，4字节，从0开始。

play voice：是否语音播放识别结果，0 – 不播放；1 – 播放（如果播放则不需要发送0032指令的文字信息）。

result data packetMaxSize ：App发送语音识别结果数据时，分包的最大长度。

data length：1字节，后面data内容的长度。

当subCmd为0（清空授权token指令）和 1（设置alexa通知状态）时，data length为0，无data数据。

content data


4字节
	
1字节


dialog id
	
status

## 8.55 发送ZigBee配网信息：0x0040

命令帧 APP->BLE：

0 - 成功；1 – 未授权；2 – 无网络；3 – 网络异常；

| PAN<br>2字节 | exPAN<br>8字节 | Channel<br>1字节 | net key<br>16字节 |
|---|---|---|---|

isACK=0 , cmd = 0x02：Provide Speech

content data


4字节


dialog id

dialog id : 必须和 Start Speech中一样。

net key：16字节。

isACK=1 , cmd = 0x02：Provide Speech ACK

content data


1字节
	
1字节
	
4字节


audio format
	
audio profile
	
dialog id

dialog id : 必须和 Start Speech中一样。

| Status<br>1字节 |
|---|

Status：0 – 接收成功，1 -接收失败。

isACK=0 , cmd = 0x03：Endpoint Speech

### content data 4字节 dialog id

命令帧 APP->BLE：

isACK=1 , cmd = 0x03：Endpoint Speech ACK

content data


4字节


dialog id

响应帧 BLE->APP：

isACK=0 , cmd = 0x04：Stop Speech

| 标识 | 模组信息单元组 |  |  |  |  | ... | 模组信息单元组 |  |  |  |  |
|---|---|---|---|---|---|---|---|---|---|---|---|
| Include_<br>Emt_info | Emt | N_TLD | TLD | ~ | TLD | ... | Emt | N_TLD | TLD | ~ | TLD |
| 1字节 | 1字节 | 1字节 | 1+2+N | ~ |  | ... | 1字节 | 1字节 | 1+2+N | ~ |  |

error code ：

include_emt_info：用于表述该回复包是否直接携带模组信息，1-直接携带，0-不携带，无后续模组信息单元组字段。当为0时后续模组信息，通过其他指令（待定）走分包传输机制。

Emt：模组类型，定义如下：

2 – INTERNAL ；

3 – UNSUPPORTED ；

4 – USER_CANCELLED ；

0x04 – ZIGBEE模组

N_TLD：后续TLD数据单元组的个数。

TLD: 表示一个数据单元组，其中type表示类型，占1字节；length表示data的长度，占2字节；data表示实际数据。其中type类型定义如下：

isACK=1 , cmd = 0x04：Stop Speech ACK

content data


4字节


dialog id

0x03 – 通信优先级

isACK=0 , cmd = 0x05：Notify Speech State

content data


1字节


state

0x06 – OTA通道与版本

0x07 – 被动查询/主动上报标识（用于标识该信息是app查询的还是设备主动上报的）

| TLD - 0x01 扩展模组信息 |  |  |
|---|---|---|
| Type | Length | Data |
| 0x01 | ~ | Json字符串 |
|  |  | 当emt为NB模组时，设备信息Json字符串格式：<br>{<br>"pv":"3.0",<br>"bv":"3.0",<br>"sv":"1.0.0",<br>"i":"866242050005256",<br>“opt”:1,<br>"h":28800<br>}<br><br>当emt为Wi-Fi模组时，设备信息Json字符串格式：<br>可不包含，模组激活时自己会上报<br><br>当emt为Cat模组时，设备信息Json字符串格式：<br>可不包含，模组激活时自己会上报 |

| TLD - 0x02 扩展模组状态 | TLD - 0x02 扩展模组状态 |  |
|---|---|---|
| Type | Length | Data |
| Length | 0x01 | 0x00- 弹出<br>0x01 - 插入 |

| TLD - 0x03 通信优先级 |  |  |
|---|---|---|
| Type | Length | Data |
| 0x03 | ~ | 一个优先级用1个字节表示，其顺序表示优先级从高到低。例某设备通信优先级定义为BLE > MQTT，则data数据表示为0x03, 0x01。 |

| TLD - 0x04 OTA优先级【暂未使用】 |  |  |
|---|---|---|
| Type | Length | Data |
| 0x04 | ~ | 定义梳理中 |

| TLD - 0x05 扩展模组绑定状态 |  |  |
|---|---|---|
| Type | Length | Data |
| 0x05 | 0x01 | 0x00 - 未绑定<br>0x01 - 已绑定 |

| TLD - 0x06 OTA通道与版本【暂不使用】 |  |  |
|---|---|---|
| Type | Length | Data |
| 0x06 | ~ | Channel1+Version1 + .. + Channelx+Versionx，其中：<br>Channel用1字节表示; <br>Version用3字节表示，如0x010200代表v1.2.0 |

| TLD - 0x07 被动查询/主动上报标识 |  |  |
|---|---|---|
| Type | Length | Data |
| 0x07 | 0x01 | 用于标识该信息是app查询的还是设备主动上报的，定义如下：<br>0x00-被动查询<br>0x01-设备主动上报；主动上报的app无需回复应答 |

# 1 2-3 4 5 6 - 7 8- x VERSION Sn Type Flag Length Audio Data

VERSION：指令格式版本号定义详见，≤V5的版本=0， ≥V6的版本=1 。

Sn：发送序号，2字节，高字节在前。

| 模组激活信息单元组 |  | ... | 模组激活信息单元组 |  |
|---|---|---|---|---|
| Emt | TLD | ... | Emt | TLD |
| 1字节 | 1+2+N | ... | 1字节 | 1+2+N |

Type_bit7 ：    0 – 需要响应帧；1 – 不需要响应帧。

Flag：bit3-bit0：audio format，见 start speech参数说明，其他bit保留。

Length ：语音数据长度；

Audio Data：语音数据。

0x04 – ZIGBEE模组。

TLD: 数据单元组，其中type表示类型，占1字节；length表示data的长度，占2字节；data表示实际数据。表示扩展模组的激活信息，各模组定义不一，具体以模组定义为准。拼装好数据后基于该指令分包发送，分包协议采用第7节介绍的协议格式，单包最大长度采用0x0000响应帧中的packetMaxSize。注：对于app的每一个分包，设备都会响应，app只有收到对应的分包响应后才能发送下一分包。

Data格式：

1
	
2
	
3
	
4
	
5
	
6


VERSION
	
Sn
	
Type
	
Flag
	
Status

| TLD - NB模组激活信息 |  |  |
|---|---|---|
| Type | Length | Data |
| 0x01 | ~ | Json字符串, 内容主要包括devID, secKey。<br>{<br>"s":"nYqC0z6yuqJzrggALzatsB42K0PCzqi5",<br>"d":"6c7294fd20d1a5b39cngrv"<br>} |

当Emt为WI-Fi/Cat模组时，TLD定义为：

| TLD - Wi-Fi模组激活信息 |  |  |
|---|---|---|
| Type | Length | Data |
| 0x01 | 0x01 | JSON字符串，内容主要包括devID, secKey, localKey等，云端与设备端对齐，中间链路如APP、BLE透传。<br>{<br>"devId":"6cba0dda87e9ae35f8cudk",<br>"secKey":"fd0ed4e6beff2361",<br>"region":"AY",<br>"env":"pr_0",<br>"localKey":"a427f948b8451c68"<br>} |

当Emt为Zigbee模组时，TLD定义为：

| TLD - Zigbee模组激活信息 |  |  |  |  |  |
|---|---|---|---|---|---|
| Type | Length | Data |  |  |  |
| 0x01 | ~ | PAN<br>2字节 | exPAN<br>8字节 | Channel<br>1字节 | net key<br>16字节 |

8.49
语音识别结果数据：0x0032（分包）

命令帧 APP->BLE：

| 1 | 2 - 3 | 4 - 5 | 6 - 9 | 10 - 13 |
|---|---|---|---|---|
| status | 当前分包序号 | 当前分包长度 | 已接收的数据长度 | 总长度 |

1
	
2
	
3 - 4
	
5- x


VERSION
	
type
	
length
	
result data

VERSION：指令格式版本号定义详见，≤V5的版本=0， ≥V6的版本=1 。

type ： 0 – 普通语音识别结果数据；1 – 天气数据；2 – 日历、toDOlist数据；其他 – 保留。

3 – 其他错误，蓝牙即将断开；

result data：需要显示或者播报的结果数据，不同的type对应不同的格式，如下所示。

Data 分包发送，分包协议采用第7节介绍的协议格式，单包最大长度采用Start Speech 中的packetMaxSize。

注：对于app的每一个分包，设备都会响应，app只有收到对应的分包响应后才能发送下一分包。

总长度：4字节，需要接收的数据总长度，该值在app发送第一个分包时包含。

type=0 时：

### Result data Length 个字节 语音识别常规数据

命令帧 APP->BLE：

type=1 时（天气查询结果数据）：

Result data


1字节
	
n字节
	
1字节
	
n字节
	
1字节
	
1字节
	
n字节


mainTitle
Length
	
mainTitle 
string
	
subtitle
Length
	
subtitle 
string
	
Temperature unit 
	
Weather Days
	
Weather data

Weather data


要查询日期的天气
	
1-7天的天气


Length
	
Data
	
Length
	
Data
	
…
	
Length
	
Data


2字节
	
n字节
	
1字节
	
n字节
	
…
	
1字节
	
n字节


n
	
query day weather data

	
n
	
Daily
data
	
…
	
n
	
Daily
data

query day weather data


1字节
	
2字节
	
2字节
	
2字节
	
2字节
	
1字节
	
n字节
	
1字节
	
n字节


Flag
	
天气
类型
	
当前
温度
	
最低
温度
	
最高
温度
	
TTS_Length
	
TTS String
	
DescriptionLength
	
Description
String

| 1字节 | 6字节 | 1 字节 | 1字节 | 1字节 | 1字节 | n字节 |
|---|---|---|---|---|---|---|
| Status | MAC地址 | 设备能力 | 设备状态 | 连接状态 | 蓝牙名称长度n | 蓝牙名称 |

mainTitle ：主标题，内容为城市名，如：Hangzhou,China

subtitle ：次标题，内容为日期，如：Wednesday, June 16, 2021

设备能力：Bit0:  CTKD标志位 0-不支持  1-支持，其他bit保留，默认为0 。

设备状态：Bit0: 配对标志， 0-未配对，1-已配对，其他bit保留，默认为0 。

温度值为有符号型。

查询日期即为subtitle显示的日期。

Flag：bit0 – 是否包含当前温度；其他bit保留。

TTS_Length：TTS文本字符串长度。

蓝牙名称长度：BT蓝牙名称长度，最长32字节。

Description Length：天气类型描述字符串长度。

Description String：天气类型描述字符串，例如Rainy，sunny等。

## 8.59 文件发送流程

![Feishu Docs - Image](blob:https://icn602w9tnqf.feishu.cn/d3e2048d-9451-4ffa-ac53-4f27a0ed967a)

### Result data 1字节 n字节 1字节 n字节 1字节 n字节 mainTitleLength mainTitle string subtitleLength subtitle string list number list data

List data


List_1 data
	
…
	
List_n data


1字节
	
n字节
	
1字节
	
n字节
	
…
	



leftTextLength
	
leftTextString
	
rightTextLength
	
rightTextString
	
…
	
         内容同List_1

mainTitle ：主标题，内容为日历或者list，如：To Do List

| 1字节 | 2字节 | 1字节 | n字节 | 4字节 | 4字节 | 16字节 |
|---|---|---|---|---|---|---|
| File Type | File ID | File identifier Length | File identifier | File Version | File Length | File MD5 |

List number：后面的List个数。

| 1字节 | 2字节 | 1字节 | n字节 | 4字节 | 4字节 | 1 + 1 + n字节 |
|---|---|---|---|---|---|---|
| File Type | File ID | File identifier Length | File identifier | File Version | File Length | Digest TLV |

Digest TLV：新文件的完整摘要（算法见 0x000D 节 Digest TLV 定义），由 发送方 计算并随命令帧下发。设备侧用途：

1）完整性校验基准：设备接收完成后，计算所收文件的完整摘要并与该字段比对；

2）断点续传识别：设备判断本地已储存的未完成文件是否即为本次下发文件时（判断文件是否变更），以该字段作为识别依据之一（与 File Type / File ID / File Version / File Length 配合使用）。

Data格式：

1
	
2 - 3
	
4 - 5
	
6 - 9
	
10 - 13


status
	
当前分包序号
	
当前分包长度
	
已接收的数据长度
	
总长度

File ID：文件ID，应用层定义。

File identifier Length：文件标识符长度。

File identifier：文件标识符。

File Version：文件版本，应用层定义格式。

File Length：文件总长度。

File MD5：文件的MD5 。

注：设备端在解析该指令时，不能限制最大长度，方便后续扩展。

总长度：4字节，需要接收的数据总长度，该值在app发送第一个分包时包含。

响应帧 BLE->APP：

8.50
语音闹钟设置：0x0033

| 1字节 | 2字节 | 1字节 | 2字节 | 4字节 | 16字节 |
|---|---|---|---|---|---|
| File Type | File ID | Status | 最大包长度 | 已储存文件长度 | 已储存文件MD5 |

Data格式：

| 1字节 | 2字节 | 1字节 | 2字节 | 4字节 | 1 + 1 + n字节 |
|---|---|---|---|---|---|
| File Type | File ID | Status | 最大包长度 | 已储存文件长度 | Digest TLV |

Digest TLV：已储存文件内容（长度即"已储存文件长度"字段值）的摘要，设备端从已接收并储存的数据计算并返回；无已储存文件时 Digest Type=0x00、Digest Len=0。

> 说明：断点续传时，APP 计算新文件前"已储存文件长度"字节的摘要，与设备返回的摘要比对，两者吻合则从该长度继续传输（见 0x0071），否则已储存数据不可信、从头传输。

type ： 0 - 定时；1 – 闹钟；2 – 提醒。

Token摘要：描述Token摘要信息
◦
Token MD5： VERISON=0时，为MD5摘要。(≤V5使用)
◦
Token Digest TLV：VERSION=1时，格式详见Digest TLV。(≥V6使用)

time ： 定时、闹钟、提醒的时间，4字节unix时间戳。

Loop count：循环次数，0表示只提醒一次，提醒和闹钟采用，定时没有，定时时值为0。

loopcountpausetime：循环次数不为0时才有意义，内容为循环提醒时的间隔时间，单位ms；

Reminder text Length：提醒文本数据的长度。

Reminder text ：提醒文本，字符串格式。

0x03：文件大小超过限制。

(0x04：安卓当做else处理)。

0x05：本地已经存在该文件，无需重复下发。

VERSION
1字节
	
cmd
1字节
	
type
1字节
	
Token MD5（≤V5） /
Token Digest TLV（≥V6）
n字节
	
Status
1字节

VERSION、cmd、type、token 摘要：取命令帧的对应数据。

最大包长度：设备允许的单包最大长度，单位字节。

已储存文件信息：

说明：为了支持断点续传，这里会返回设备端已经储存的文件信息，APP在收到后，首先根据设备返回的已储存文件长度计算新文件对应长度的摘要（算法与命令帧 Digest TLV 一致；≤V5 版为 MD5），然后和设备返回的盒文件摘要值对比，如果两者都吻合，那么在下面的文件起始传输请求中将起始传输偏移量改为该长度值，否则文件起始传输偏移量改为0，表示从头开始传输。

## 8.51 语音授权token读取：0x0034

命令帧 APP->BLE：

Data格式：

| 1字节 | 2字节 | 4字节 |
|---|---|---|
| File Type | File ID | File offset |

VERSION：指令格式版本号定义详见，≤V5的版本=0， ≥V6的版本=1 。

Token data request：0 – 只读取授权token的MD5（≤V5）/ Token Digest TLV（≥V6）；1 – 同时读取token的MD5（≤V5）/ Token Digest TLV（≥V6） 和 token 数据。

注：该指令响应帧只返回token的MD5（≤V5）/ Token Digest TLV（≥V6） ，token数据通过0x0035上报。,

响应帧 BLE->APP：

响应帧 BLE->APP：

| 1字节 | 2字节 | 4字节 |
|---|---|---|
| File Type | File ID | File offset |

VERSION
1字节
	
Status
1字节
	
Token MD5（≤V5） /
Token Digest TLV（≥V6）
n字节

VERSION: 指令格式版本号定义详见，≤V5的版本=0， ≥V6的版本=1 。

说明：实际文件传输的偏移地址应该以设备端要求的为准，且设备端要求的地址会小于等于APP端给出的偏移。

0x00：成功，后跟有效token数据。

### 0x01：设备无token，后跟16个0。

Token摘要：描述Token摘要信息
◦
Token MD5： VERISON=0时，为MD5摘要。(≤V5使用)
◦
Token Digest TLV：VERSION=1时，格式详见Digest TLV。(≥V6使用)

DATA格式：

| 1字节 | 2字节 | 2字节 | 2字节 | 2字节 | n字节 |
|---|---|---|---|---|---|
| File Type | File ID | 包号 | 当前包数据长度n | 当前包数据CRC16 | 当前包数据 |

命令帧 BLE->APP：

响应帧 BLE->APP：

1
	
2 - 3
	
4- x


VERSION
	
Token length
	
token data

| 1字节 | 2字节 | 1字节 |
|---|---|---|
| File Type | File ID | Status |

File Type、File ID见文件信息描述。

token data：token数据。

Data 分包发送，分包协议采用第7节介绍的协议格式，单包最大长度采用Start Speech 中的packetMaxSize。
注：对于设备的每一个分包，APP都要响应，设备只有收到对应的分包响应后才能发送下一分包。

0x01：包号异常

响应帧 APP->BLE：

0x03：crc检验失败

1
	
2 - 3
	
4 - 5
	
6 - 9
	
10 - 13


status
	
当前分包序号
	
当前分包长度
	
已接收的数据长度
	
总长度

status： 0 – 全部接收完成；

# 1 – 当前分包有效，可以发送下一包；

2 – 当前分包错误，需要从头发送；

3 – 其他错误，蓝牙即将断开；

| 1字节 | 2字节 |
|---|---|
| File Type | File ID |

File Type、File ID见文件信息描述。

已接收的数据长度：4字节，表示目前为止已经接收到的字节数；

总长度：4字节，需要接收的数据总长度，该值在设备发送第一个分包时包含。

Data格式：

| 1字节 | 2字节 | 1字节 |
|---|---|---|
| File Type | File ID | Status |

File Type、File ID见文件信息描述。

Data格式：

VERSION
1字节
	
Token MD5（≤V5） /
Token Digest TLV（≥V6）
n字节
	
Token length
2字节
	
token data
n字节

VERSION：指令格式版本号定义详见，≤V5的版本=0， ≥V6的版本=1 。

0x02: 数据总MD5检验失败（数据总摘要校验失败）

•
Token MD5： VERISON=0时，为MD5摘要。(≤V5使用)

•
Token Digest TLV：VERSION=1时，格式详见Digest TLV。(≥V6使用)

### Token length ：token数据长度；

![Feishu Docs - Image](blob:https://icn602w9tnqf.feishu.cn/b54f6829-55a6-4cb4-a19b-4ab3e3fca24d)

### Data 分包发送，分包协议采用第7节介绍的协议格式，单包最大长度采用Start Speech 中的packetMaxSize。 注：对于每一个分包，设备都要响应，APP只有收到对应的分包响应后才能发送下一分包。

命令帧 APP->BLE：

DATA格式：ID_INFO + TYPE

ID_INFO格式如下：

| 1 | 2 | 3~ |
|---|---|---|
| ID_TYPE | ID_LEN | ID_DATA |

status： 0 – 全部接收完成；

0 – 配件的SHORT_ID，配件激活时由云端下发；

2 – 当前分包错误，需要从头发送；

3 – 其他错误，蓝牙即将断开；

ID_LEN：ID的数据长度。

ID_DATA：ID的数据内容。

已接收的数据长度：4字节，表示目前为止已经接收到的字节数；

TYPE：0 – BLE升级固件，1 – 外部mcu升级固件，10-19：附件通道号（IOT前台创建），其他 – 保留。

8.54
语音通用设置：0x0037

命令帧 APP->BLE：

| 1~n | n+1 | n+2 | n+3 | (n+4)~(n+9) |
|---|---|---|---|---|
| ID_INFO | Flag | OTA_Version | TYPE | Type_data |

1
	
2
	
3
	
4-n


VERSION
	
subCmd
	
data length
	
data

| 1 | 2 | 3~ |
|---|---|---|
| ID_TYPE | ID_LEN | ID_DATA |

subCmd：0 – 清空授权token；1 – 设置alexa通知状态；其他–保留。

data length：1字节，后面data内容的长度。

当subCmd为0时，data length为0，无data数据。

当subCmd为1时，data length为1，data内容为：

ID_LEN：ID的数据长度。

notify_flag ：0 - 无效值； 1 - 有通知， 2 - 通知清除。

Flag：0x00-允许升级，0x01-拒绝升级。

OTA_Version：OTA协议大版本，例如0x03代表3.X的协议版本。

TYPE：0 – BLE升级固件，1 – 外部mcu升级固件，10-19：附件通道号（IOT前台创建），其他 – 保留。

1
	
2
	
3
	
4
	
5-n


VERSION
	
subCmd
	
Status
	
data length
	
data

| 1 | 2 | 3 | 4 | 5 | 6 |
|---|---|---|---|---|---|
| Version四字节 |  |  |  | 最大包长度 |  |

subCmd：同请求帧。

Version：当前固件版本号，例如0x00 01 00 02代表版本为V1.0.2。

最大包长度：设备允许的单包最大长度，单位字节。

0x01：失败。

## 8.66 配件OTA升级文件信息：0x0081

data length：1字节，后面data内容的长度。

当subCmd为0（清空授权token指令）和 1（设置alexa通知状态）时，data length为0，无data数据。

| n字节 | 37字节 |
|---|---|
| ID_INFO | FILE_INFO |

8.55
发送ZigBee配网信息：0x0040

| 1字节 | 8字节 | 4字节 | 16字节 | 4字节 | 4字节 |
|---|---|---|---|---|---|
| TYPE | 产品PID | 文件版本 | 文件MD5 | 文件长度 | CRC32 |

Data格式(或为透传，具体以Zigbee为准)：

| 1字节 | 8字节 | 4字节 | 4字节 | 4字节 | 1 + 1 + n字节 |
|---|---|---|---|---|---|
| TYPE | 产品PID | 文件版本 | 文件长度 | CRC32 | Digest TLV |

ID_INFO格式如下：

| 1 | 2 | 3~ |
|---|---|---|
| ID_TYPE | ID_LEN | ID_DATA |

Channel：1字节；

0 – 配件的SHORT_ID，配件激活时由云端下发；

1 – 主从一体从设备的UUID；

响应帧 BLE->APP：

ID_LEN：ID的数据长度。

ID_DATA：ID的数据内容。

TYPE：0 – BLE升级固件，1 – 外部mcu升级固件，10-19：附件通道号（IOT前台创建），其他 – 保留，不同的TYPE，后面对应不同的数据格式，上表为TYPE=0、1、10-19对应的格式

文件版本：例如，0x00010002代表版本为V1.0.2。

8.56
查询扩展模组设备信息：0x0050

响应帧 BLE->APP：

Data格式：

| n字节 | 1字节 | 1字节 | 4字节 | 4字节 | 16字节 |
|---|---|---|---|---|---|
| ID_INFO | ID_INFO | STATE | 已储存文件长度 | 已储存文件CRC32 | 已储存文件MD5<br>（目前不使用） |

响应帧 BLE->APP：

| n字节 | 1字节 | 1字节 | 4字节 | 4字节 |
|---|---|---|---|---|
| ID_INFO | TYPE | STATE | 已储存文件长度 | 已储存文件CRC32 |

标识
	
模组信息单元组
	
...
	
模组信息单元组


Include_
Emt_info
	
Emt
	
N_TLD
	
TLD
	
~
	
TLD
	
...
	
Emt
	
N_TLD
	
TLD
	
~
	
TLD


1字节
	
1字节
	
1字节
	
1+2+N
	
~
	

	
...
	
1字节
	
1字节
	
1+2+N
	
~

| 1 | 2 | 3~ |
|---|---|---|
| ID_TYPE | ID_LEN | ID_DATA |

include_emt_info：用于表述该回复包是否直接携带模组信息，1-直接携带，0-不携带，无后续模组信息单元组字段。当为0时后续模组信息，通过其他指令（待定）走分包传输机制。

0 – 配件的SHORT_ID，配件激活时由云端下发；

1 – 主从一体从设备的UUID；

0x02 – Wi-Fi模组

ID_LEN：ID的数据长度。

ID_DATA：ID的数据内容。

TYPE：0 – BLE升级固件，1 – 外部mcu升级固件，10-19：附件通道号（IOT前台创建），其他 – 保留，不同的TYPE，后面对应不同的数据格式，上表为TYPE=0、1、10-19对应的格式。

TLD: 表示一个数据单元组，其中type表示类型，占1字节；length表示data的长度，占2字节；data表示实际数据。其中type类型定义如下：

0x01 – 扩展模组设备信息

0x01：产品PID不一致

0x02: 文件版本低于或者等于当前版本

0x04 – 固件OTA优先级

0x05 – 扩展模组绑定状态

0x06 – OTA通道与版本

0x07 – 被动查询/主动上报标识（用于标识该信息是app查询的还是设备主动上报的）

TLD - 0x01 扩展模组信息


Type
	
Length
	
Data


0x01
	
~
	
Json字符串



	

	
当emt为NB模组时，设备信息Json字符串格式：
{
"pv":"3.0",
"bv":"3.0",
"sv":"1.0.0",
"i":"866242050005256",
“opt”:1,
"h":28800
}

当emt为Wi-Fi模组时，设备信息Json字符串格式：
可不包含，模组激活时自己会上报

当emt为Cat模组时，设备信息Json字符串格式：
可不包含，模组激活时自己会上报

TLD - 0x02 扩展模组状态


Type
	
Length
	
Data


0x02
	
0x01
	
0x00- 弹出
0x01 - 插入

### TLD - 0x03 通信优先级 Type Length Data 0x03 ~ 一个优先级用1个字节表示，其顺序表示优先级从高到低。例某设备通信优先级定义为BLE > MQTT，则data数据表示为0x03, 0x01。

TLD - 0x04 OTA优先级【暂未使用】


Type
	
Length
	
Data


0x04
	
~
	
定义梳理中

TLD - 0x05 扩展模组绑定状态


Type
	
Length
	
Data


0x05
	
0x01
	
0x00 - 未绑定
0x01 - 已绑定

TLD - 0x06 OTA通道与版本【暂不使用】


Type
	
Length
	
Data


0x06
	
~
	
Channel1+Version1 + .. + Channelx+Versionx，其中：
Channel用1字节表示; 
Version用3字节表示，如0x010200代表v1.2.0

| 1 | 2 | 3~ |
|---|---|---|
| ID_TYPE | ID_LEN | ID_DATA |

8.57
发送扩展模组激活信息：0x0051（分包）

0 – 配件的SHORT_ID，配件激活时由云端下发；

1 – 主从一体从设备的UUID；

模组激活信息单元组
	
...
	
模组激活信息单元组


Emt
	
TLD
	
...
	
Emt
	
TLD


1字节
	
1+2+N
	
...
	
1字节
	
1+2+N

ID_LEN：ID的数据长度。

ID_DATA：ID的数据内容。

TYPE：0 – BLE升级固件，1 – 外部mcu升级固件，10-19：附件通道号（IOT前台创建），其他 – 保留

offset：文件起始传输偏移量，四字节。

0x04 – ZIGBEE模组。

TLD: 数据单元组，其中type表示类型，占1字节；length表示data的长度，占2字节；data表示实际数据。表示扩展模组的激活信息，各模组定义不一，具体以模组定义为准。拼装好数据后基于该指令分包发送，分包协议采用第7节介绍的协议格式，单包最大长度采用0x0000响应帧中的packetMaxSize。注：对于app的每一个分包，设备都会响应，app只有收到对应的分包响应后才能发送下一分包。

DATA格式：ID_INFO + TYPE + offset

当Emt为NB模组时，TLD定义为：

| 1 | 2 | 3~ |
|---|---|---|
| ID_TYPE | ID_LEN | ID_DATA |

当Emt为WI-Fi/Cat模组时，TLD定义为：

TLD - Wi-Fi模组激活信息


Type
	
Length
	
Data


0x01
	
~
	
JSON字符串，内容主要包括devID, secKey, localKey等，云端与设备端对齐，中间链路如APP、BLE透传。
{
"devId":"6cba0dda87e9ae35f8cudk",
"secKey":"fd0ed4e6beff2361",
"region":"AY",
"env":"pr_0",
"localKey":"a427f948b8451c68"
}

当Emt为Zigbee模组时，TLD定义为：

TLD - Zigbee模组激活信息


Type
	
Length
	
Data


0x01
	
~
	
PAN
2字节
	
exPAN
8字节
	
Channel
1字节
	
net key
16字节

ID_LEN：ID的数据长度。

ID_DATA：ID的数据内容。

1
	
2 - 3
	
4 - 5
	
6 - 9
	
10 - 13


status
	
当前分包序号
	
当前分包长度
	
已接收的数据长度
	
总长度

offset：设备要求的起始传输文件偏移量。

说明：实际文件传输的偏移地址应该以设备端要求的为准，且设备端要求的地址会小于等于APP端给出的偏移。

2 – 当前分包错误，需要从头发送；

## 8.68 配件OTA升级数据： 0x0083

命令帧 APP->BLE：

当前分包长度：固件2字节；

| n字节 | 7+n字节 |
|---|---|
| ID_INFO | OTA_DATA |

总长度：4字节，需要接收的数据总长度，该值在app发送第一个分包时包含。

| 1字节 | 2字节 | 2字节 | 2字节 | n字节 |
|---|---|---|---|---|
| TYPE | 包号 | 当前包数据长度n | 当前包数据CRC16 | 当前包数据 |

8.58
查询BT设备信息：0x0060

| 1 | 2 | 3~ |
|---|---|---|
| ID_TYPE | ID_LEN | ID_DATA |

ID_TYPE：

0 – 配件的SHORT_ID，配件激活时由云端下发；

1 – 主从一体从设备的UUID；

Data 格式：

1字节
	
6字节
	
1 字节
	
1字节
	
1字节
	
1字节
	
n字节


Status
	
MAC地址
	
设备能力
	
设备状态
	
连接状态
	
蓝牙名称长度n
	
蓝牙名称

status： 0 – 接收成功， 1 -接收失败。

TYPE：0 – BLE升级固件信息，1 – 外部mcu升级固件信息，10-19：附件通道号（IOT前台创建），其他 – 保留。

设备能力：Bit0:  CTKD标志位 0-不支持  1-支持，其他bit保留，默认为0 。

设备状态：Bit0: 配对标志， 0-未配对，1-已配对，其他bit保留，默认为0 。

Data格式：

| 1~n-1 | n | n+1 |
|---|---|---|
| ID_INFO | TYPE | STATE |

ID_INFO格式如下：

| 1 | 2 | 3~ |
|---|---|---|
| ID_TYPE | ID_TYPE | ID_DATA |

蓝牙名称长度：BT蓝牙名称长度，最长32字节。

0 – 配件的SHORT_ID，配件激活时由云端下发；

1 – 主从一体从设备的UUID；

8.59
文件发送流程

ID_LEN：ID的数据长度。

ID_DATA：ID的数据内容。

TYPE：0 – BLE升级固件信息，1 – 外部mcu升级固件，10-19：附件通道号（IOT前台创建），其他 – 保留。

Data格式：

1字节
	
2字节
	
1字节
	
n字节
	
4字节
	
4字节
	
16字节


File Type
	
File ID
	
File identifier Length
	
File identifier
	
File Version
	
File Length
	
File MD5

0x01：包号异常

1字节
	
2字节
	
1字节
	
n字节
	
4字节
	
4字节
	
1 + 1 + n字节


File Type
	
File ID
	
File identifier Length
	
File identifier
	
File Version
	
File Length
	
Digest TLV

Digest TLV：新文件的完整摘要（算法见 0x000D 节 Digest TLV 定义），由 发送方 计算并随命令帧下发。设备侧用途：

1）完整性校验基准：设备接收完成后，计算所收文件的完整摘要并与该字段比对；

2）断点续传识别：设备判断本地已储存的未完成文件是否即为本次下发文件时（判断文件是否变更），以该字段作为识别依据之一（与 File Type / File ID / File Version / File Length 配合使用）。

## 8.69 配件OTA升级结束：0x0084

File Type：0 – 通用文件类型，其他 – 保留。

DATA格式：ID_INFO + TYPE

File identifier Length：文件标识符长度。

| 1 | 2 | 3~ |
|---|---|---|
| ID_TYPE | ID_LEN | ID_DATA |

File Version：文件版本，应用层定义格式。

0 – 配件的SHORT_ID，配件激活时由云端下发；

1 – 主从一体从设备的UUID；

注：设备端在解析该指令时，不能限制最大长度，方便后续扩展。

ID_LEN：ID的数据长度。

ID_DATA：ID的数据内容。

TYPE：0 – BLE升级固件信息，1 – 外部mcu升级固件，10-19：附件通道号（IOT前台创建），其他 – 保留。

1字节
	
2字节
	
1字节
	
2字节
	
4字节
	
16字节


File Type
	
File ID
	
Status
	
最大包长度
	
已储存文件长度
	
已储存文件MD5

响应帧 BLE->APP：

1字节
	
2字节
	
1字节
	
2字节
	
4字节
	
1 + 1 + n字节


File Type
	
File ID
	
Status
	
最大包长度
	
已储存文件长度
	
Digest TLV

| 1~n | n+1 | n+2 |
|---|---|---|
| ID_INFO | TYPE | STATE |

说明：断点续传时，APP 计算新文件前"已储存文件长度"字节的摘要，与设备返回的摘要比对，两者吻合则从该长度继续传输（见 0x0071），否则已储存数据不可信、从头传输。

| 1 | 2 | 3~ |
|---|---|---|
| ID_TYPE | ID_LEN | ID_DATA |

ID_TYPE：

0 – 配件的SHORT_ID，配件激活时由云端下发；

1 – 主从一体从设备的UUID；

0x00：可以下发。

ID_LEN：ID的数据长度。

0x02: 有此文件，但版本低于或者等于当前版本。

TYPE：0 – BLE固件信息，1 – 外部mcu升级固件，10-19：附件通道号（IOT前台创建），其他 – 保留。

(0x04：安卓当做else处理)。

0x05：本地已经存在该文件，无需重复下发。

0x01：数据总长度错误

0x02: 数据总CRC检验失败

最大包长度：设备允许的单包最大长度，单位字节。

## 8.70 语音传输控制（GPT）：0x0090

说明：为了支持断点续传，这里会返回设备端已经储存的文件信息，APP在收到后，首先根据设备返回的已储存文件长度计算新文件对应长度的摘要（算法与命令帧 Digest TLV 一致；≤V5 版为 MD5），然后和设备返回的盒文件摘要值对比，如果两者都吻合，那么在下面的文件起始传输请求中将起始传输偏移量改为该长度值，否则文件起始传输偏移量改为0，表示从头开始传输。

## 8.71 语音传输数据（GPT）：0x0091

内容完全复用功能码为0x0031的指令，要注意判断 version 字段。

## 8.72 语音识别结果数据（GPT）：0x0092（分包）

内容完全复用功能码为0x0032的指令，要注意判断 version 字段。

### 1字节 2字节 4字节 File Type File ID File offset

File Type 、File ID同请求帧。

File offset：文件起始传输偏移量，四字节。

命令帧 APP -> BLE：

响应帧 BLE->APP：

| 1 | 2-3 | 4 | 5 | 6 - 7 | 8- x |
|---|---|---|---|---|---|
| Version | Sn | Type | Flag | Length | Audio Data |

1字节
	
2字节
	
4字节


File Type
	
File ID
	
File offset

File Type 、File ID同请求帧。

File offset：文件起始传输偏移量，四字节。

说明：实际文件传输的偏移地址应该以设备端要求的为准，且设备端要求的地址会小于等于APP端给出的偏移。

Flag：bit3-bit0：audio format，见 start speech参数说明，其他bit保留。

8.62
发送文件数据：0x0072

Audio Data：语音数据。

DATA格式：

1字节
	
2字节
	
2字节
	
2字节
	
2字节
	
n字节


File Type
	
File ID
	
包号
	
当前包数据长度n
	
当前包数据CRC16
	
当前包数据

Data格式：

| 1 | 2-3 | 4 | 5 | 6 |
|---|---|---|---|---|
| Version | Sn | Type | Flag | Status |

Version、Sn、Type、Flag：取命令帧的对应数据。

1字节
	
2字节
	
1字节


File Type
	
File ID
	
Status

File Type、File ID见文件信息描述。

0x01：失败

0x00：成功

## 8.74 Hey Tuya语音模式控制信令：0x0094

命令帧 APP <-> BLE：

0x03：crc检验失败

| 1 | 2 | 3 |
|---|---|---|
| Version | Type | Opcode |

Version：指令格式版本号，当前为0 。

Type：opcode大类，定义如下：

命令帧 APP->BLE：

0x01-错误通知类；如app启动对应服务失败/异常，通知设备停止，也没必要尝试重试；

1字节
	
2字节


File Type
	
File ID

File Type、File ID见文件信息描述。

| 操作信令 | 值 | 方向 | request_start |
|---|---|---|---|
| request_start | 设备 → App | 请求开始；设备收到app回复的start_recording后再启动录音并推音频流； | 请求开始；设备收到app回复的start_recording后再启动录音并推音频流； |
| request_stop | 请求结束；设备收到app回复的stop_recording后再停止录音 | 请求结束；设备收到app回复的stop_recording后再停止录音 | 请求结束；设备收到app回复的stop_recording后再停止录音 |
| start_recording | App → 设备 | App → 设备 | 开始录音/传输音频；准备就绪，通知设备开始上传音频 |
| 开始录音/传输音频；准备就绪，通知设备开始上传音频 | stop_recording | App → 设备 | 停止录音/传输音频；交互结束，通知设备停止上传音频 |
| heartbeat | 0x05 | APP ->设备 | 心跳包 |

当Type=0x01 错误通知类时：

| opcode-错误原因 | 说明 |
|---|---|
| 0 | 超量 |
| 1 | 任务已存在 |
| 2 | 权限未设置 |
| 3 | 语言不支持 |
| 4 | 隐私未同意 |
| 5 | 需要用户先登陆 |
| 6 | 其它错误 |

1字节
	
2字节
	
1字节


File Type
	
File ID
	
Status

### File Type、File ID见文件信息描述。

命令帧 BLE->APP：

0x00：成功

| Dp数组1的数据 |  |  |  | ~ | Dp数组n的数据 |  |  |  |
|---|---|---|---|---|---|---|---|---|
| 1 | 2 | 3 | 4~ | ~ | n | n+1 | n+2 | n+3~ |
| Dp_id | Dp_type | Dp_len | Dp_data | ~ | Dp_id | Dp_type | Dp_len | Dp_data |

Dp_id：  1个字节，在开发平台注册的dp_id序号。

Dp_type：1个字节。
#define  DT_RAW     0 
#define  DT_BOOL    1
#define  DT_VALUE    2
#define  DT_STRING   3
#define  DT_ENUM    4 
#define  DT_BITMAP   5

Dp_len：1个字节，数据长度，最大255

Dp_data：数据，dp_len个字节。

8.65
配件OTA升级请求： 0x0080

命令帧 APP->BLE：

| STATE<br>1字节 |
|---|

ID_INFO格式如下：

1
	
2
	
3~


ID_TYPE
	
ID_LEN
	
ID_DATA

ID_TYPE：

0 – 配件的SHORT_ID，配件激活时由云端下发；

## 8.76 设备调试信息显示：0x8002

命令帧 BLE->APP：

Data格式：string字符串，例如"hello from tuya ble"。

ID_DATA：ID的数据内容。

响应帧 APP->BLE：

TYPE：0 – BLE升级固件，1 – 外部mcu升级固件，10-19：附件通道号（IOT前台创建），其他 – 保留。

| STATUS<br>1字节 |
|---|

响应帧 BLE->APP：

0x00：成功显示 。

1~n
	
n+1
	
n+2
	
n+3
	
(n+4)~(n+9)


ID_INFO
	
Flag
	
OTA_Version
	
TYPE
	
Type_data

ID_INFO格式如下：

# 1 2 3~ ID_TYPE ID_LEN ID_DATA

命令帧 BLE->APP：

0 – 配件的SHORT_ID，配件激活时由云端下发；

| 时间戳 |  | Dp数组1的数据 |  |  | ~ | Dp数组n的数据 | Dp数组n的数据 |  | 1 | 1 |
|---|---|---|---|---|---|---|---|---|---|---|
| 1 | 2-x | x+1 | x+2 | x+4~ | x+4~ | ~ | x+n+1 | x+n+1 | x+n+3~ | x+n+3~ |
| timeType | time | Dp_id | Dp_type | Dp_len | Dp_data | ~ | Dp_id | Dp_type | Dp_len | Dp_data |

TimeType：0表示后面传输的数据是13字节ms级时间字符串，例如”1553932355000”，13字节ms级，2019/3/30 15:52:35

1表示后面time是4字节unix秒级时间戳，大端格式传输。

Dp_id：  1个字节，在开发平台注册的dp_id序号。

Flag：0x00-允许升级，0x01-拒绝升级。

OTA_Version：OTA协议大版本，例如0x03代表3.X的协议版本。

TYPE：0 – BLE升级固件，1 – 外部mcu升级固件，10-19：附件通道号（IOT前台创建），其他 – 保留。

Type_data：不同的type对应不同格式的type_data，TYPE为0、1、10-19对应的type_data格式如下所示：

1
	
2
	
3
	
4
	
5
	
6


Version四字节
	
最大包长度

#define  DT_ENUM    4

Version：当前固件版本号，例如0x00 01 00 02代表版本为V1.0.2。

最大包长度：设备允许的单包最大长度，单位字节。

Dp_data：数据，dp_len个字节。

8.66
配件OTA升级文件信息：0x0081

命令帧：APP->BLE：

| STATE<br>1字节 |
|---|

n字节
	
37字节


ID_INFO
	
FILE_INFO

FILE_INFO

1字节
	
8字节
	
4字节
	
16字节
	
4字节
	
4字节


TYPE
	
产品PID
	
文件版本
	
文件MD5
	
文件长度
	
CRC32

V6版-FILE_INFO

### 1字节 8字节 4字节 4字节 4字节 1 + 1 + n字节 TYPE 产品PID 文件版本 文件长度 CRC32 Digest TLV

命令帧 BLE->APP：

1
	
2
	
3~


ID_TYPE
	
ID_LEN
	
ID_DATA

| 序号R_SN |  | 标志 | Dp数组1的数据 |  |  |  | ~ | Dp数组n的数据 |  |  |  |
|---|---|---|---|---|---|---|---|---|---|---|---|
| 1 | 2 | 3 | 4 | 5 | 6 | 7 | ~ | n | n+1 | n+2 | n+3~ |
| R_SN_H | R_SN_L | flag | Dp_id | Dp_type | Dp_len | Dp_data | ~ | Dp_id | Dp_type | Dp_len | Dp_data |

0 – 配件的SHORT_ID，配件激活时由云端下发；

flag：0 – 同时上报云端和面板；

1 - 上报云端，不上报面板；

2 - 上报面板，不上报云端；

ID_DATA：ID的数据内容。

TYPE：0 – BLE升级固件，1 – 外部mcu升级固件，10-19：附件通道号（IOT前台创建），其他 – 保留，不同的TYPE，后面对应不同的数据格式，上表为TYPE=0、1、10-19对应的格式

文件版本：例如，0x00010002代表版本为V1.0.2。

响应帧 APP->BLE：

响应帧 BLE->APP：

| 1 | 2 | 3 | 4 |
|---|---|---|---|
| R_SN_H | R_SN_L | flag | STATE |

n字节
	
1字节
	
1字节
	
4字节
	
4字节
	
16字节


ID_INFO
	
TYPE
	
STATE
	
已储存文件长度
	
已储存文件CRC32
	
已储存文件MD5
（目前不使用）

flag：取命令帧的flag。

n字节
	
1字节
	
1字节
	
4字节
	
4字节


ID_INFO
	
TYPE
	
STATE
	
已储存文件长度
	
已储存文件CRC32

ID_INFO格式如下：

1
	
2
	
3~


ID_TYPE
	
ID_LEN
	
ID_DATA

ID_TYPE：

## 8.79 带标志和时间戳状态dp点数据上报：0x8005

1 – 主从一体从设备的UUID；

Data格式：

| 序号R_SN |  | 标志 | 时间戳 |  | Dp点数据 |  |  |  | ~ |
|---|---|---|---|---|---|---|---|---|---|
| 1 | 2 | 3 | 4 | 5-x | x+1 | x+2 | x+3 | x+4~ | x+4~ |
| R_SN_H | R_SN_L | flag | timeType | time | timeType | Dp_type | Dp_type | Dp_data | ~ |

ID_DATA：ID的数据内容。

TYPE：0 – BLE升级固件，1 – 外部mcu升级固件，10-19：附件通道号（IOT前台创建），其他 – 保留，不同的TYPE，后面对应不同的数据格式，上表为TYPE=0、1、10-19对应的格式。

1 - 上报云端，不上报面板；

2 - 上报面板，不上报云端；

3 - 都不上报。

0x02: 文件版本低于或者等于当前版本

0 - 表示后面传输的time数据是13字节ms级时间字符串，例如”1553932355000”，13字节ms级，表示2019/3/30 15:52:35

1 - 表示后面time是4字节unix秒级时间戳，大端格式传输。

Dp点数据格式详见0x8001指令。

已储存文件信息：

说明：为了支持断点续传，这里会返回设备端已经储存的文件信息，APP在收到后，首先根据设备返回的已储存文件长度计算新文件对应长度的CRC32，然后和设备返回的CRC32对比，如果两者都吻合，那么在下面的文件起始传输请求中将起始传输偏移量改为该长度值，否则文件起始传输偏移量改为0，表示从头开始传输。

Data格式：

| 1 | 2 | 3 | 4 |
|---|---|---|---|
| R_SN_H | R_SN_L | flag | STATE |

R_SN：取命令帧的R_SN。

DATA格式：ID_INFO + TYPE + offset

ID_INFO格式如下：

1
	
2
	
3~


ID_TYPE
	
ID_LEN
	
ID_DATA

ID_TYPE：

0 – 配件的SHORT_ID，配件激活时由云端下发；

## 8.80 dp点数据上报：0x8006（PV≥4.0）

命令帧 BLE->APP：

ID_LEN：ID的数据长度。

| 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 |
|---|---|---|---|---|---|---|---|---|
| VERSION | R_SN |  |  |  | TYPE | FLAG | DP1_ID | DP1_TYPE |
| 10 | 11 | 12 ~ (12+DP1_LEN) |  | ~ | n | n+1 | n+2 | n+3 |
| DP1_LEN |  | DP1_DATA |  |  | DPn_ID | DPn_TYPE | DPn_LEN |  |
| n+4 ~ |  |  |  |  |  |  |  |  |
| DPn_DATA |  |  |  |  |  |  |  |  |

TYPE：0 – BLE升级固件，1 – 外部mcu升级固件，10-19：附件通道号（IOT前台创建），其他 – 保留

offset：文件起始传输偏移量，四字节。

TYPE_bit3-bit0 ：0 – 主动上报的dp数据；1 – 应答查询的dp数据；（2-16）：保留。

TYPE_bit6-bit4 ：保留。

TYPE_bit7 ：    0 – 需要响应帧；1 – 不需要响应帧。

FLAG：0 – 同时上报云端和面板；

1
	
2
	
3~


ID_TYPE
	
ID_LEN
	
ID_DATA

2 - 上报面板，不上报云端；

0 – 配件的SHORT_ID，配件激活时由云端下发；

Dp点数据格式详见0x0027指令，DP_LEN：2个字节。

其他保留。

ID_LEN：ID的数据长度。

ID_DATA：ID的数据内容。

| 1 | 2 | 3 | 4 | 5 | 6 | 7 | VERSION |
|---|---|---|---|---|---|---|---|
| VERSION | R_SN | R_SN |  |  | TYPE | FLAG | STATE |

VERSION、R_SN、TYPE、FLAG：取命令帧的对应数据。

说明：实际文件传输的偏移地址应该以设备端要求的为准，且设备端要求的地址会小于等于APP端给出的偏移。

0x00：成功

8.68
配件OTA升级数据： 0x0083

命令帧 APP->BLE：

## 8.81 带时间戳dp点数据上报：0x8007（PV≥4.0）

n字节
	
7+n字节


ID_INFO
	
OTA_DATA

OTA_DATA格式：

| 1 | 2 | 3 | 4 |  | 5 | 6 | 7 | 8 | 9~n |
|---|---|---|---|---|---|---|---|---|---|
| VERSION | R_SN |  |  |  |  | TYPE | FLAG | timeType | time_data |
| n + 1 | n + 2 | n + 3 |  | n + 4 | n + 5 ~ |  | ~ | ~ | ~ |
| DP1_ID | DP1_TYPE | DP1_LEN |  |  | DP1_DATA |  | ~ | DPn_ID | DPn_TYPE |
| ~ |  | ~ |  |  |  |  |  |  |  |
| DPn_LEN |  | DPn_DATA |  |  |  |  |  |  |  |

VERSION：指令格式版本号，当前为0 。

1
	
2
	
3~


ID_TYPE
	
ID_LEN
	
ID_DATA

TYPE_bit3-bit0 ：0 – 主动上报的dp数据；1 – 应答查询的dp数据；（2-16）：保留。

0 – 配件的SHORT_ID，配件激活时由云端下发；

TYPE_bit7 ：    0 – 需要响应帧；1 – 不需要响应帧。

FLAG：0 – 同时上报云端和面板；

ID_LEN：ID的数据长度。

2 - 上报面板，不上报云端；

TYPE：0 – BLE升级固件信息，1 – 外部mcu升级固件信息，10-19：附件通道号（IOT前台创建），其他 – 保留。

timeType：

0 - 表示后面传输的time_data数据是13字节ms级时间字符串，例如”1553932355000”，13字节ms级，表示2019/3/30 15:52:35

1 - 表示后面time_data是4字节unix秒级时间戳，大端格式传输。

1~n-1
	
n
	
n+1


ID_INFO
	
TYPE
	
STATE

ID_INFO格式如下：

1
	
2
	
3~


ID_TYPE
	
ID_LEN
	
ID_DATA

ID_TYPE：

| 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 |
|---|---|---|---|---|---|---|---|
| VERSION | R_SN |  |  |  | TYPE | FLAG | STATE |

VERSION、R_SN、TYPE、FLAG：取命令帧的对应数据。

STATE：

ID_LEN：ID的数据长度。

ID_DATA：ID的数据内容。

TYPE：0 – BLE升级固件信息，1 – 外部mcu升级固件，10-19：附件通道号（IOT前台创建），其他 – 保留。

## 8.82 带数据源dp点数据上报：0x8008（PV≥4.5）

命令帧 BLE->APP：

0x01：包号异常

| 1 | 2 | 3 | 4 | 5 | R_SN | 7 | x~ (8+ADD_INFO_LEN-1) | ADD_INFO |
|---|---|---|---|---|---|---|---|---|
| VERSION | R_SN | n+4 | n+6~(n+6+DP1_LEN-1) | ~ | DP_SRC_TYPE | ADD_INFO_LEN | ADD_INFO |  |
| n | n+1 | m+4 ~ | n+3 | n+4 | DPn_TYPE | n+6~(n+6+DP1_LEN-1) |  | ~ |
| TYPE | FLAG | DP1_ID | DP1_TYPE | DP1_LEN |  | DP1_DATA |  | R_SN |
| m | m+1 | m+2 | m+3 | ADD_INFO_LEN | DPn_ID | x~ (8+ADD_INFO_LEN-1) |  | 2 |
| DPn_ID | DPn_TYPE | DPn_LEN | n+2 | DPn_DATA | 4 |  | n+6~(n+6+DP1_LEN-1) | TYPE |

VERSION：指令格式版本号，当前为0 。

R_SN：发送序号，四字节，高字节在前。

DP_SRC_TYPE：数据源类型。

8.69
配件OTA升级结束：0x0084

1 – 配件；

2 – 主从一体从设备。

ADD_INFO_LEN：数据源补充信息长度。

ADD_INFO：数据源补充信息。当数据源为主体设备时，无补充信息；但数据源为配件时，该字段存放配件的ID_INFO。ID_INFO格式如下：

| 1 | 2 | 3 |
|---|---|---|
| ID_TYPE | ID_LEN | ID_DATA |

0 – 配件的SHORT_ID，配件激活时由云端下发；

0 – 配件的SHORT_ID，配件激活时由云端下发；

1 – 主从一体从设备的UUID；

ID_LEN：ID的数据长度。

ID_DATA：ID的数据内容。

TYPE：0 – BLE升级固件信息，1 – 外部mcu升级固件，10-19：附件通道号（IOT前台创建），其他 – 保留。

TYPE_bit3-bit0 ：0 – 主动上报的dp数据；1 – 应答查询的dp数据；（2-16）：保留。

TYPE_bit6-bit4 ：保留。

1~n
	
n+1
	
n+2


ID_INFO
	
TYPE
	
STATE

FLAG：0 – 同时上报云端和面板；

1
	
2
	
3~


ID_TYPE
	
ID_LEN
	
ID_DATA

2 - 上报面板，不上报云端；

0 – 配件的SHORT_ID，配件激活时由云端下发；

Dp点数据格式详见0x0027指令，DP_LEN：2个字节。

其他保留。

ID_LEN：ID的数据长度。

ID_DATA：ID的数据内容。

| 1 | 2 | 3 | 4 | 5 | 6 | 7 | x~ (8+ADD_INFO_LEN-1) | x~ (8+ADD_INFO_LEN-1) |
|---|---|---|---|---|---|---|---|---|
| VERSION | R_SN |  |  | DP_SRC_TYPE | DP_SRC_TYPE | ADD_INFO_LEN | ADD_INFO |  |
| n+1 | n+1 | n+2 |  |  |  |  |  |  |
| TYPE | FLAG | STATE |  |  |  |  |  |  |

VERSION、R_SN、DP_SRC_TYPE、ADD_INFO_LEN、ADD_INFO、TYPE、FLAG：取命令帧的对应数据。

0x00：成功

0x01：数据总长度错误

0x02: 数据总CRC检验失败

0x03:其它

## 8.83 带数据源和时间戳dp点数据上报：0x8009（PV≥4.5）

内容完全复用功能码为0x0030的指令，要注意判断 version 字段。

8.71
语音传输数据（GPT）：0x0091

| 1 | 2 | 3 | 4 | 5 | 6 | 7 | x~ (8+ADD_INFO_LEN-1) |  | n |
|---|---|---|---|---|---|---|---|---|---|
| VERSION | R_SN |  |  |  | DP_SRC_TYPE | ADD_INFO_LEN | ADD_INFO |  | TYPE |
| n+1 | n+2 | n+3~ | m | M+1 | m+2 | m+3 | m+3 |  | ~ |
| FLAG | time_Type | Time_data | DP1_ID | DP1_TYPE | DP1_LEN |  | DP1_DATA |  |  |
| y | y+1 | y+2 | y+3 | y+4~ |  |  |  |  |  |
| DPn_ID | DPn_TYPE | DPn_LEN |  | DPn_DATA |  |  |  |  |  |

8.72
语音识别结果数据（GPT）：0x0092（分包）

内容完全复用功能码为0x0032的指令，要注意判断 version 字段。

8.73
语音传输数据（GPT）：0x0093

要注意判断 Version 字段。

1 – 配件；

2 – 主从一体从设备。

ADD_INFO_LEN：数据源补充信息长度。

1
	
2-3
	
4
	
5
	
6 - 7
	
8- x


Version
	
Sn
	
Type
	
Flag
	
Length
	
Audio Data

| 1 | 2 | 3 |
|---|---|---|
| ID_TYPE | ID_LEN | ID_DATA |

Sn：发送序号，2字节，高字节在前。

0 – 配件的SHORT_ID，配件激活时云端下发；

Type_bit7 ：    0 – 需要响应帧；1 – 不需要响应帧。

Flag：bit3-bit0：audio format，见 start speech参数说明，其他bit保留。

ID_LEN：ID的数据长度。

ID_DATA：ID的数据内容。

TYPE_bit3-bit0 ：0 – 主动上报的dp数据；1 – 应答查询的dp数据；（2-16）：保留。

TYPE_bit6-bit4 ：保留。

TYPE_bit7 ：    0 – 需要响应帧；1 – 不需要响应帧。

1
	
2-3
	
4
	
5
	
6


Version
	
Sn
	
Type
	
Flag
	
Status

Version、Sn、Type、Flag：取命令帧的对应数据。

2 - 上报面板，不上报云端；

3 - 都不上报。

timeType：

0 - 表示后面传输的time_data数据是13字节ms级时间字符串，例如”1553932355000”，13字节ms级，表示2019/3/30 15:52:35

1 - 表示后面time_data是4字节unix秒级时间戳，大端格式传输。

Dp点数据格式详见0x0027指令，DP_LEN：2个字节。

Data格式：

1
	
2
	
3


Version
	
Type
	
Opcode

Version：指令格式版本号，当前为0 。

| 1 | 2 | 3 | 4 | 5 | 6 | 7 | x~ (8+ADD_INFO_LEN-1) |  |
|---|---|---|---|---|---|---|---|---|
| VERSION | R_SN |  |  |  | DP_SRC_TYPE | ADD_INFO_LEN | ADD_INFO |  |
| n | n+1 | n+2 | n+2 |  |  |  |  |  |
| TYPE | FLAG | STATE |  |  |  |  |  |  |

VERSION、R_SN、DP_SRC_TYPE、ADD_INFO_LEN、ADD_INFO、TYPE、FLAG：取命令帧的对应数据。

0x01-错误通知类；如app启动对应服务失败/异常，通知设备停止，也没必要尝试重试；

Opcode ：具体命令/其他含义，定义如下：

当Type=0x00 控制类时：

操作信令
	
值
	
方向
	
描述


request_start
	
0x01
	
设备 → App
	
请求开始；设备收到app回复的start_recording后再启动录音并推音频流；


request_stop
	
0x02
	
设备 → App
	
请求结束；设备收到app回复的stop_recording后再停止录音


start_recording
	
0x03
	
App → 设备
	
开始录音/传输音频；准备就绪，通知设备开始上传音频


stop_recording
	
0x04
	
App → 设备
	
停止录音/传输音频；交互结束，通知设备停止上传音频


heartbeat
	
0x05
	
APP ->设备
	
心跳包

## 8.84 设备请求云端实时时间1：0x8011

opcode-错误原因
	
说明


0
	
超量


1
	
任务已存在


2
	
权限未设置


3
	
语言不支持


4
	
隐私未同意


5
	
需要用户先登陆


6
	
其它错误

DATA：无

8.75
dp点数据上报：0x8001

响应帧：APP->BLE：

Data格式：

| 13字节字符串 | 2字节时区（有符号型） |
|---|---|
| Unix ms级时间 | Time_zone |

时区：实际时区的100倍，例如北京东八区为8x100=800，西7.5区为-750；

Dp_type：1个字节。
#define  DT_RAW     0 
#define  DT_BOOL    1
#define  DT_VALUE    2
#define  DT_STRING   3
#define  DT_ENUM    4 
#define  DT_BITMAP   5

注2：此处获取的2字节时区是App本地时区，即已经将夏令时计算在内的时区（但是双模设备将会改成云端时区，即不包含夏令时的时区，是否双模设备由App端作区分，将于V6.7.0发布，讨论于20250521）。

Dp_data：数据，dp_len个字节。

## 8.85 设备请求云端实时时间2：0x8012

命令帧 BLE->APP：

Data格式：

STATE
1字节

响应帧 APP->BLE：

Data格式：

| 1字节 | 1字节 | 1字节 | 1字节 | 1字节 | 1字节 | 1字节 | 2字节（有符号型） |  |
|---|---|---|---|---|---|---|---|---|
| 年 | 月 | 日 | 时 | 分 | 秒 | 星期 | 时区高 | 时区低 |

注：如 2019 年4 月 28 日12:23:25 星期二， 应按如下顺序发送：0x13,0x04,0x1C,0x0C,0x17,0x19,0x02。

8.76
设备调试信息显示：0x8002

时区：实际时区的100倍，例如北京东八区为8x100=800，西7.5区为-750；

Data格式：string字符串，例如"hello from tuya ble"。

## 8.86 设备请求手机本地实时时间：0x8013

命令帧 BLE->APP：

STATUS
1字节

STATUS：

响应帧 APP->BLE：

Data格式：

| 1字节 | 1字节 | 1字节 | 1字节 | 1字节 | 1字节 | 1字节 | 2字节（有符号型） |  |
|---|---|---|---|---|---|---|---|---|
| 年 | 月 | 日 | 时 | 分 | 秒 | 星期 | 时区高 | 时区低 |

注：手机本地时间，如 2019 年4 月 28 日12:23:25 星期二， 应按如下顺序发送：0x13,0x04,0x1C,0x0C,0x17,0x19,0x02。

星期：0-6 , 0代表星期日。

时区：实际时区的100倍，例如北京东八区为8x100=800，西7.5区为-750；

时间戳
	
Dp数组1的数据
	
~
	
Dp数组n的数据


1
	
2-x
	
x+1
	
x+2
	
x+3
	
x+4~
	
~
	
x+n
	
x+n+1
	
x+n+2
	
x+n+3~


timeType
	
time
	
Dp_id
	
Dp_type
	
Dp_len
	
Dp_data
	
~
	
Dp_id
	
Dp_type
	
Dp_len
	
Dp_data

### TimeType：0表示后面传输的数据是13字节ms级时间字符串，例如”1553932355000”，13字节ms级，2019/3/30 15:52:35

1表示后面time是4字节unix秒级时间戳，大端格式传输。

Dp_id：  1个字节，在开发平台注册的dp_id序号。

Dp_type：1个字节。

#define  DT_RAW     0

#define  DT_BOOL    1

| 10字节 | 2字节 | 1字节 | 10字节 | 10字节 | 10字节 | 10字节 | 10字节 |
|---|---|---|---|---|---|---|---|
| timestamp | timestamp | n | start_1 | end_1 | end_1 | start_n | end_n |

timestamp：当前unix时间戳，10位字符串，例如：” 1599556121” 。

timezone： 实际时区的100倍，有符号型整数，例如北京东八区为8x100=800，西7.5区为-750。

n ：表示后面有几年的夏令时数据，为0表示没有夏令时数据，为1表示后面只有1年的夏令时数据。

start_1 ：第1年的夏令时起始时间，格式和timestamp一致。

end_1 ： 第1年的夏令时结束时间，格式和timestamp一致（必须大于当前时间timestamp）。

start_n ：第n年的夏令时起始时间，格式和timestamp一致。

end_n ： 第n年的夏令时结束时间，格式和timestamp一致。

注：1、如果app从云端获取不到时间，app需要回复全0数据（二进制0）；

2、响应包中的第一组夏令时数据（start_1 : end_1）有可能是当年的，也有可能是下一年的，app判断条件是如果当前时间点timestamp还没进入当年的夏令时时间段或者已经处于当年夏令时时间段内，第一组的夏令时数据应该是当年的，否则第一组的夏令时数据是timestamp当前时间点下一年的。

0x00：成功

## 8.88 设备请求天气数据：0x8015

命令帧 BLE->APP：

8.78
带标志dp点数据上报：0x8004

| 第1种天气数据 |  | 第2种天气数据 |  | ~ | 第n种天气数据 |  |
|---|---|---|---|---|---|---|
| 1字节 | LEN_1字节 | 1字节 | LEN_2字节 | ~ | 1字节 | LEN_n字节 |
| LEN_1 | KEY_1 | LEN_2 | KEY_2 | ~ | LEN_n | KEY_n |

当前支持的天气种类（KEY值）见附录“天气服务参数表”。

序号R_SN
	
标志
	
Dp数组1的数据
	
~
	
Dp数组n的数据


1
	
2
	
3
	
4
	
5
	
6
	
7
	
~
	
n
	
n+1
	
n+2
	
n+3~


R_SN_H
	
R_SN_L
	
flag
	
Dp_id
	
Dp_type
	
Dp_len
	
Dp_data
	
~
	
Dp_id
	
Dp_type
	
Dp_len
	
Dp_data

序号R_SN：两字节，高字节在前。

flag：0 – 同时上报云端和面板；

| STATE<br>1字节 |
|---|

2 - 上报面板，不上报云端；

0 – 成功，请求的天气数据会通过0x8016指令下发；

Dp点数据格式详见0x8001指令。

## 8.89 下发天气数据：0x8016

命令帧 APP->BLE：

1
	
2
	
3
	
4


R_SN_H
	
R_SN_L
	
flag
	
STATE

| 保留 | 第1种天气数据 |  |  |  |  | ~ | 第n种天气数据 |
|---|---|---|---|---|---|---|---|
| 1字节 | 1字节 | KEY_LEN | 1字节 | 1字节 | VALUE_LEN | ~ | x |
| 0x00 | KEY_LEN | KEY | VALUE_TYPE | VALUE_LEN | VALUE | ~ | x |

KEY_LEN：天气参数长度；

KEY ：天气参数，例如：w.temp/w.pm25/w.condition等；

VALUE_TYPE：天气参数值类型，0x00 - 整型数据，0x01 - 字符串，其他- 保留；

VALUE_LEN：天气参数值长度；

VALUE：天气参数（KEY）的值，例如温度是35度，KEY就是w.temp，VALUE就是35。

8.79
带标志和时间戳状态dp点数据上报：0x8005

例：w.temp:35 w.pm25:14 w.condition:晴 对应的DATA数据为（16进制）：

00  06  77 2e 74 65 6d 70  00  04  00 00 00 23   06  77 2e 70 6d 32 35  00  04  00 00 00 0e  0b  77 2e 63 6f 6e 64 69 74 69 6f 6e  01  03  e6 99 b4

序号R_SN
	
标志
	
时间戳
	
Dp点数据
	
~


1
	
2
	
3
	
4
	
5-x
	
x+1
	
x+2
	
x+3
	
x+4~
	
~


R_SN_H
	
R_SN_L
	
flag
	
timeType
	
time
	
Dp_id
	
Dp_type
	
Dp_len
	
Dp_data
	
~

06：strlen(“w.temp”)；

77 2e 74 65 6d 70：w.temp；

1 - 上报云端，不上报面板；

2 - 上报面板，不上报云端；

3 - 都不上报。

06：strlen(“w.pm25”)；

0 - 表示后面传输的time数据是13字节ms级时间字符串，例如”1553932355000”，13字节ms级，表示2019/3/30 15:52:35

1 - 表示后面time是4字节unix秒级时间戳，大端格式传输。

Dp点数据格式详见0x8001指令。

00 00 00 0e：数值，14；

0b：strlen(“w.condition”)；

77 2e 63 6f 6e 64 69 74 69 6f 6e：w.condition；

1
	
2
	
3
	
4


R_SN_H
	
R_SN_L
	
flag
	
STATE

R_SN：取命令帧的R_SN。

e6 99 b4：“晴”的UTF-8编码。

STATE：

天气状况w.conditon的值，如果类型为字符串型，则是中文的UTF-8编码，详见附录“天气UTF-8编码对照表”，如果是整型类型值，则值为天气编号（暂不支持）。

0x01：失败

响应帧 BLE->APP：

8.80
dp点数据上报：0x8006（PV≥4.0）

| STATE<br>1字节 |
|---|

STATUS ：

1
	
2
	
3
	
4
	
5
	
6
	
7
	
8
	
9


VERSION
	
R_SN
	
TYPE
	
FLAG
	
DP1_ID
	
DP1_TYPE


10
	
11
	
12 ~ (12+DP1_LEN)
	
~
	
n
	
n+1
	
n+2
	
n+3


DP1_LEN
	
DP1_DATA
	

	
DPn_ID
	
DPn_TYPE
	
DPn_LEN


n+4 ~ 
	

	

	

	

	



DPn_DATA

VERSION：指令格式版本号，当前为0 。

R_SN：发送序号，四字节，高字节在前。

### TYPE_bit3-bit0 ：0 – 主动上报的dp数据；1 – 应答查询的dp数据；（2-16）：保留。

TYPE_bit6-bit4 ：保留。

TYPE_bit7 ：    0 – 需要响应帧；1 – 不需要响应帧。

| 版本号 | 位置数据 | 第1种天气数据 |  | 第2种天气数据 |  | ~ | 第n种天气数据 | 第n种天气数据 |
|---|---|---|---|---|---|---|---|---|
| 1字节 | 1/x字节 | 1/x字节 | LEN_1字节 | LEN_1字节 | LEN_2字节 | LEN_2字节 | 1字节 | LEN_n字节 |
| VERSION | LDATA | LEN_1 | KEY_1 | LEN_2 | KEY_2 | KEY_2 | LEN_n | KEY_n |

VERSION：指令格式版本号，当前为0 。

2 - 上报面板，不上报云端；

| 位置类型 | 位置数据[仅类型为3时有该字段] |  |  |
|---|---|---|---|
| 1字节 | 1字节 | 1字节 | VALUE_LEN |
| LOCATION_TYPE | VALUE_TYPE | VALUE_LEN | VALUE |

Dp点数据格式详见0x0027指令，DP_LEN：2个字节。

1 - 配网位置，无后续位置数据；

2 - 手机当前位置，无后续位置数据；

3 - 自定义位置，有后续位置数据。自定义一般由面板内配置并通过DP点下发给设备。

1
	
2
	
3
	
4
	
5
	
6
	
7
	
8


VERSION
	
R_SN
	
TYPE
	
FLAG
	
STATE

VERSION、R_SN、TYPE、FLAG：取命令帧的对应数据。

VALUE：城市参数的值。

KEY：当前支持的天气种类（KEY值）见附录“天气服务参数表”。

0x01：失败

响应帧 APP->BLE：

8.81
带时间戳dp点数据上报：0x8007（PV≥4.0）

| STATE<br>1字节 |
|---|

STATUS ：

1
	
2
	
3
	
4
	
5
	
6
	
7
	
8
	
9~n


VERSION
	
R_SN
	
TYPE
	
FLAG
	
timeType
	
time_data


n + 1
	
n + 2
	
n + 3
	
n + 4
	
n + 5 ~
	
~
	
~
	
~


DP1_ID
	
DP1_TYPE
	
DP1_LEN
	
DP1_DATA
	
~
	
DPn_ID
	
DPn_TYPE


 ~ 
	
~
	

	

	

	



DPn_LEN
	
DPn_DATA

VERSION：指令格式版本号，当前为0 。

R_SN：发送序号，四字节，高字节在前。

### TYPE_bit3-bit0 ：0 – 主动上报的dp数据；1 – 应答查询的dp数据；（2-16）：保留。

TYPE_bit6-bit4 ：保留。

DATA：下发的天气数据，包括位置信息与天气数据，天气数据以LKTLV数据格式下发，如下所示。

| 版本号 | 位置数据 | 第1种天气数据 |  |  |  |  | ~ | 第n种天气数据 |
|---|---|---|---|---|---|---|---|---|
| 1字节 | 1/x字节 | 1字节 | KEY_LEN | 1字节 | 1字节 | VALUE_LEN | ~ | x |
| VERSION | LDATA | KEY_LEN | KEY | VALUE_TYPE | VALUE_LEN | VALUE | ~ | x |

VERSION：指令格式版本号，当前为0 。

2 - 上报面板，不上报云端；

3 - 都不上报。

KEY ：天气参数，例如：w.temp/w.pm25/w.condition等；

0 - 表示后面传输的time_data数据是13字节ms级时间字符串，例如”1553932355000”，13字节ms级，表示2019/3/30 15:52:35

1 - 表示后面time_data是4字节unix秒级时间戳，大端格式传输。

VALUE：天气参数（KEY）的值，例如温度是35度，KEY就是w.temp，VALUE就是35。

例：w.temp:35 w.pm25:14 w.condition:晴 对应的DATA数据为（16进制）：

00  06  77 2e 74 65 6d 70  00  04  00 00 00 23   06  77 2e 70 6d 32 35  00  04  00 00 00 0e  0b  77 2e 63 6f 6e 64 69 74 69 6f 6e  01  03  e6 99 b4

1
	
2
	
3
	
4
	
5
	
6
	
7
	
8


VERSION
	
R_SN
	
TYPE
	
FLAG
	
STATE

VERSION、R_SN、TYPE、FLAG：取命令帧的对应数据。

06：strlen(“w.temp”)；

77 2e 74 65 6d 70：w.temp；

00：整型数据；

04：值的长度4字节；

8.82
带数据源dp点数据上报：0x8008（PV≥4.5）

06：strlen(“w.pm25”)；

77 2e 70 6d 32 35：w.pm25；

1
	
2
	
3
	
4
	
5
	
6
	
7
	
x~ (8+ADD_INFO_LEN-1)


VERSION
	
R_SN
	
DP_SRC_TYPE
	
ADD_INFO_LEN
	
ADD_INFO


n
	
n+1
	
n+2
	
n+3
	
n+4 
	
n+5
	
n+6~(n+6+DP1_LEN-1)
	
~


TYPE
	
FLAG
	
DP1_ID
	
DP1_TYPE
	
DP1_LEN
	
DP1_DATA
	



m
	
m+1
	
m+2
	
m+3
	
m+4 ~
	

	



DPn_ID
	
DPn_TYPE
	
DPn_LEN
	
DPn_DATA

VERSION：指令格式版本号，当前为0 。

R_SN：发送序号，四字节，高字节在前。

0b：strlen(“w.condition”)；

77 2e 63 6f 6e 64 69 74 69 6f 6e：w.condition；

1 – 配件；

2 – 主从一体从设备。

ADD_INFO_LEN：数据源补充信息长度。

ADD_INFO：数据源补充信息。当数据源为主体设备时，无补充信息；但数据源为配件时，该字段存放配件的ID_INFO。ID_INFO格式如下：

天气状况w.conditon的值，如果类型为字符串型，则是中文的UTF-8编码，详见附录“天气UTF-8编码对照表”，如果是整型类型值，则值为天气编号（暂不支持）。

ID_TYPE：

0 – 配件的SHORT_ID，配件激活时由云端下发；

1 – 主从一体从设备的UUID；

| STATE<br>1字节 |
|---|

ID_LEN：ID的数据长度。

ID_DATA：ID的数据内容。

1 – 失败。

TYPE_bit3-bit0 ：0 – 主动上报的dp数据；1 – 应答查询的dp数据；（2-16）：保留。

## 8.92 设备请求IOT数据：0x8019

TYPE_bit7 ：    0 – 需要响应帧；1 – 不需要响应帧。

FLAG：0 – 同时上报云端和面板；

| 分包最大长度 | 子命令 | 数据（TLD格式） |  | packetMaxSize |
|---|---|---|---|---|
| packetMaxSize | sub_cmd | Type | Length | Data |
| 2字节 | 2字节 | 1字节 | 2字节 | n字节 |

packetMaxSize：2字节，备端指定分包传输IOT数据的最大长度。

Sub_cmd：子命令，用于区分设备请求不同类型的IOT数据，定义如下：

| 子命令 | 含义 |
|---|---|
| 0x0001 | 请求场景数据 |
| 0x0002 | 请求场景执行/控制 |
| ... | ... |

TLD：通用数据表述格式，类型+数据长度+数据。Type：表示类型，占1字节；Length：表述后续数据长度，占2字节；Data：数据。注：不同的子命令其TLD格式定义不一。

响应帧 APP->BLE：

当sub_cmd=0x0001时，TLD定义如下：

| TLD（sub_cmd=0x0001，请求场景数据） |  |  |  |  |  |
|---|---|---|---|---|---|
| Type | Length | Data | Type | Length | Length |
| Data | 数据长度 | 请求场景个数 | 请求场景<br>名称长度 | 是否请求<br>场景图片 | 场景校验码 |
| 1字节 | 2字节 | 1字节 | 2字节 | 数据长度 | 4字节 |
| 请求场景个数 | 0x0008 | 请求场景<br>名称长度 | 请求场景 | 名称长度 | 是否请求<br>场景图片 |

VERSION、R_SN、DP_SRC_TYPE、ADD_INFO_LEN、ADD_INFO、TYPE、FLAG：取命令帧的对应数据。

请求场景名称长度：场景名称统一按unicode编码，2字节表示1个字符/汉字，长度指编码后的长度。例如汉字“今”unicode编码为\u4eca， 传输顺序为0xCA, 0x4E。

是否请求场景图片：1-请求场景图片，0-不请求场景图片，预留不做实现。

场景校验值：用于增量同步，采用crc32校验算法。APP下发的场景数据中均会带场景校验值，设备端需保存。下一次设备再次请求场景数据时携带上报，APP与当前最新的场景校验值比对，如果不一致则同步下发最新的场景数据，否则不做下发。

8.83
带数据源和时间戳dp点数据上报：0x8009（PV≥4.5）

| TLD（sub_cmd=0x0002，请求场景执行） |  |  |  |  |
|---|---|---|---|---|
| Type | Length | Data |  |  |
| 类型 | 数据长度 | 场景ID长度 | 场景ID |  |
| 1字节 | 2字节 | 1字节 | n字节 |  |
| 0x00 | 1+n | n | ~ |  |

SceneID_Length：场景ID长度，最大24字节。

1
	
2
	
3
	
4
	
5
	
6
	
7
	
x~ (8+ADD_INFO_LEN-1)
	
n


VERSION
	
R_SN
	
DP_SRC_TYPE
	
ADD_INFO_LEN
	
ADD_INFO
	
TYPE


n+1
	
n+2
	
n+3~
	
m
	
M+1 
	
m+2
	
m+3
	
m+3
	
~


FLAG
	
time_Type
	
Time_data
	
DP1_ID
	
DP1_TYPE
	
DP1_LEN
	
DP1_DATA
	



y
	
y+1
	
y+2
	
y+3
	
y+4~
	

	

	



DPn_ID
	
DPn_TYPE
	
DPn_LEN
	
DPn_DATA

VERSION：指令格式版本号，当前为0 。

R_SN：发送序号，四字节，高字节在前。

DP_SRC_TYPE：数据源类型。

| Sub_cmd | TLD（APP SDK应答） |  |  |  |
|---|---|---|---|---|
| Sub_cmd | Type | Length | Data | 子命令 |
| 子命令 | 类型 | 数据长度 | 状态 | 错误码 |
| 2字节 | 1字节 | 2字节 | 1字节 | 4字节 |
| ~ | ~ | 1/5 | Length | ~ |

Sub_cmd、Type：同请求帧。

2 – 主从一体从设备。

0 – 成功，无后续的错误码字段，请求的IOT数据会通过0x801A指令下发；

ADD_INFO：数据源补充信息。当数据源为主体设备时，无补充信息；但数据源为配件时，该字段存放配件的ID_INFO。ID_INFO格式如下：

1
	
2
	
3


ID_TYPE
	
ID_LEN
	
ID_DATA

ID_TYPE：

## 8.93 APP下发IOT数据：0x801A（分包）

1 – 主从一体从设备的UUID；

| 子命令 | 数据（TLD格式） |  |  |
|---|---|---|---|
| sub_cmd | Type | Length | Data |
| 2字节 | 1字节 | 2字节 | n字节 |

Sub_cmd、Type：同请求帧。

ID_DATA：ID的数据内容。

APP SDK将业务层返回的数据按上述格式组包后分包发送，分包协议采用第7节介绍的协议格式，单包最大长度采用0x8019帧中的packetMaxSize。注：对于app的每一个分包设备都会响应，app只有收到对应的分包响应后才能发送下一分包。

TYPE_bit6-bit4 ：保留。

TYPE_bit7 ：    0 – 需要响应帧；1 – 不需要响应帧。

| Data |  |  |  |  |  |  |  |  |  |  |  |  |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 场景基础信息 |  |  |  | 场景1数据 | 场景1数据 | 场景1数据 |  | ~ | 场景n数据 |  |  |  |
| STATUS | Err<br>code | 是否<br>需要<br>更新 | 场景<br>数据<br>校验码 | 场景ID<br>长度 | 场景ID | 场景名称<br>长度 | 场景名称<br>编码 | ~ | 场景ID<br>长度 | STATUS | 场景名称<br>长度 | Err<br>code |
| 1字节 | 4字节 | 是否<br>需要<br>更新 | 4字节 | 1字节 | ~字节 | 场景<br>数据<br>校验码 | ~字节 | ~ | 1字节 | 场景ID<br>长度 | 2字节 | ~字节 |

STATUS：用于表示场景数据请求结果；

0—成功，err_code固定为0x00000000；

1—失败，错误码字段表示具体的错误，无后续字段。

err_code：错误码；

0 - 表示后面传输的time_data数据是13字节ms级时间字符串，例如”1553932355000”，13字节ms级，表示2019/3/30 15:52:35

1 - 表示后面time_data是4字节unix秒级时间戳，大端格式传输。

Dp点数据格式详见0x0027指令，DP_LEN：2个字节。

3—设备不在当前家庭下，homeid不匹配

4—场景不存在，sceneid不匹配

5—动作中没有设备

1
	
2
	
3
	
4
	
5
	
6
	
7
	
x~ (8+ADD_INFO_LEN-1)


VERSION
	
R_SN
	
DP_SRC_TYPE
	
ADD_INFO_LEN
	
ADD_INFO


n
	
n+1
	
n+2
	

	

	

	

	

	



TYPE
	
FLAG
	
STATE

VERSION、R_SN、DP_SRC_TYPE、ADD_INFO_LEN、ADD_INFO、TYPE、FLAG：取命令帧的对应数据。

是否需要更新场景数据：0-不需要，1-需要。APP端接收到设备请求的校验值与最新的场景数据校验值比对，如果不一致则需要更新，否则不需要更新（无后续字段）。

场景数据校验码：场景数据的CRC32校验值。

场景id长度：描述后续场景id字段长度，最大24字节。

场景id：场景ID编码值。

场景名称长度：描述后续场景名称编码字段长度，指转为unicode编码后的长度。

场景名称编码：场景名称的unicode编码值。

DATA：无

当sub_cmd=0x0002时，数据Data部分定义如下：

| Data |  |  | STATUS |
|---|---|---|---|
| err_code | sceneID_Length | sceneID_Length | SceneID |
| 1字节 | 4字节 | 1字节 | err_code |

STATUS：用于表示场景执行结果；

13字节字符串
	
2字节时区（有符号型）


Unix ms级时间
	
Time_zone

时区：实际时区的100倍，例如北京东八区为8x100=800，西7.5区为-750；

注1：如果app从云端获取不到时间，app需要回复全0数据（二进制0）。

注2：此处获取的2字节时区是App本地时区，即已经将夏令时计算在内的时区（但是双模设备将会改成云端时区，即不包含夏令时的时区，是否双模设备由App端作区分，将于V6.7.0发布，讨论于20250521）。

SceneID：场景ID。

8.85
设备请求云端实时时间2：0x8012

响应帧 BLE->APP：

Data格式：

| status | 当前分包序号 | 当前分包长度 | 已接收的数据长度 | 总长度 |
|---|---|---|---|---|
| 1字节 | 2字节 | 2字节 | 4字节 | 4字节 |

status： 0 – 全部接收完成；

1 – 当前分包有效，可以发送下一包；

1字节
	
1字节
	
1字节
	
1字节
	
1字节
	
1字节
	
1字节
	
2字节（有符号型）


年
	
月
	
日
	
时
	
分
	
秒
	
星期
	
时区高
	
时区低

注：如 2019 年4 月 28 日12:23:25 星期二， 应按如下顺序发送：0x13,0x04,0x1C,0x0C,0x17,0x19,0x02。

星期：0-6 , 0代表星期日。

时区：实际时区的100倍，例如北京东八区为8x100=800，西7.5区为-750；

注：如果app从云端获取不到时间，app需要回复全0数据（二进制0）。

总长度：4字节，需要接收的数据总长度，该值在app发送第一个分包时包含。

8.86
设备请求手机本地实时时间：0x8013

## 8.94 APP业务下行透传通道：0x801B（按需分包）

命令帧 APP->BLE：

Data格式：

| FLAG | App Data Packet OR App Data |
|---|---|
| 2字节 | 1 – packetMaxSize |

FLAG：请求标识，定义如下：

1字节
	
1字节
	
1字节
	
1字节
	
1字节
	
1字节
	
1字节
	
2字节（有符号型）


年
	
月
	
日
	
时
	
分
	
秒
	
星期
	
时区高
	
时区低

注：手机本地时间，如 2019 年4 月 28 日12:23:25 星期二， 应按如下顺序发送：0x13,0x04,0x1C,0x0C,0x17,0x19,0x02。

星期：0-6 , 0代表星期日。

注：如果是分包帧，所有的 App Data Packet 组包后 成为 App Data 。

App Data格式如下：

| subCmd | subCmd Data |
|---|---|
| 2字节 | x字节 |

subCmd：用于区分不同类型的透传数据，0-用于定时服务，1-用于传递 耳机增益补偿数据，其他详见内部wiki维护的业务透传sub_cmd维护表

DATA：1个字节，表示请求几年的夏令时，默认为1。

响应帧 BLE->APP：

Data格式：

| FLAG | status | 当前分包序号 | 当前分包长度 | 已接收的数据长度 | 当前分包序号 |
|---|---|---|---|---|---|
| 当前分包长度 | 当前分包长度 | 已接收的数据长度 | 已接收的数据长度 | 4字节 | 4字节 |

timestamp：当前unix时间戳，10位字符串，例如：” 1599556121” 。

timezone： 实际时区的100倍，有符号型整数，例如北京东八区为8x100=800，西7.5区为-750。

n ：表示后面有几年的夏令时数据，为0表示没有夏令时数据，为1表示后面只有1年的夏令时数据。

start_1 ：第1年的夏令时起始时间，格式和timestamp一致。

end_1 ： 第1年的夏令时结束时间，格式和timestamp一致（必须大于当前时间timestamp）。

start_n ：第n年的夏令时起始时间，格式和timestamp一致。

end_n ： 第n年的夏令时结束时间，格式和timestamp一致。

注：1、如果app从云端获取不到时间，app需要回复全0数据（二进制0）；

2、响应包中的第一组夏令时数据（start_1 : end_1）有可能是当年的，也有可能是下一年的，app判断条件是如果当前时间点timestamp还没进入当年的夏令时时间段或者已经处于当年夏令时时间段内，第一组的夏令时数据应该是当年的，否则第一组的夏令时数据是timestamp当前时间点下一年的。

注：是否需要该响应帧，取决于请求帧中的FLAG 。

8.88
设备请求天气数据：0x8015

## 8.95 APP业务上行透传通道：0x801C（按需分包）

DATA：要请求的天气内容，格式如下所示。

第1种天气数据
	
第2种天气数据
	
~
	
第n种天气数据


1字节
	
LEN_1字节
	
1字节
	
LEN_2字节
	
~
	
1字节
	
LEN_n字节


LEN_1
	
KEY_1
	
LEN_2
	
KEY_2
	
~
	
LEN_n
	
KEY_n

| FLAG | App Data Packet OR App Data |
|---|---|
| App Data Packet OR App Data | App Data Packet OR App Data |

FLAG：请求标识，定义如下：

bit0 – 该帧是否需要响应， 0 – 不响应，1 – 响应；

bit1 – 该帧是否是分包帧，0 – 不是，后跟App Data ，1 – 是，如果是分包帧那么后跟App Data Packet 。

其他bit保留。

注：如果是分包帧，所有的 App Data Packet 组包后 成为 App Data 。

0 – 成功，请求的天气数据会通过0x8016指令下发；

| subCmd | subCmd Data |
|---|---|
| 2字节 | x字节 |

subCmd：用于区分不同类型的透传数据，0-用于定时服务。

subCmd Data: 业务方自定义数据。

命令帧 APP->BLE：

DATA：下发的天气数据，以LKTLV数据格式下发，如下所示。

保留
	
第1种天气数据
	
~
	
第n种天气数据


1字节
	
1字节
	
KEY_LEN
	
1字节
	
1字节
	
VALUE_LEN
	
~
	
x


0x00
	
KEY_LEN
	
KEY
	
VALUE_TYPE
	
VALUE_LEN
	
VALUE
	
~
	
x

| FLAG | status | 当前分包序号 | 当前分包长度 | 已接收的数据长度 | 总长度 |
|---|---|---|---|---|---|
| 2字节 | 1字节 | 2字节 | 2字节 | 4字节 | 4字节 |

KEY ：天气参数，例如：w.temp/w.pm25/w.condition等；

VALUE_TYPE：天气参数值类型，0x00 - 整型数据，0x01 - 字符串，其他- 保留；

1 – 当前分包有效，可以发送下一包；

VALUE：天气参数（KEY）的值，例如温度是35度，KEY就是w.temp，VALUE就是35。

3 – 其他错误，蓝牙即将断开；

例：w.temp:35 w.pm25:14 w.condition:晴 对应的DATA数据为（16进制）：

00  06  77 2e 74 65 6d 70  00  04  00 00 00 23   06  77 2e 70 6d 32 35  00  04  00 00 00 0e  0b  77 2e 63 6f 6e 64 69 74 69 6f 6e  01  03  e6 99 b4

已接收的数据长度：4字节，表示目前为止已经接收到的字节数；

总长度：4字节，需要接收的数据总长度，该值在app发送第一个分包时包含。

注：是否需要该响应帧，取决于请求帧中的FLAG 。

00：整型数据；

## 8.96 设备上报连接状态：0x801D

00 00 00 23：35；

06：strlen(“w.pm25”)；

| 1 | 2 |
|---|---|
| 保留 | status_bit |

status_bit：

bit0：1 – LINK层已加密；0 – LINK层未加密；

00 00 00 0e：数值，14；

0b：strlen(“w.condition”)；

77 2e 63 6f 6e 64 69 74 69 6f 6e：w.condition；

## 8.97 Combo Wireless Modules下行透传通道：0x801E（按需分包）

命令帧 APP->BLE：

e6 99 b4：“晴”的UTF-8编码。

| FLAG | App Data Packet OR App Data |
|---|---|
| 2字节 | 1 – packetMaxSize |

天气状况w.conditon的值，如果类型为字符串型，则是中文的UTF-8编码，详见附录“天气UTF-8编码对照表”，如果是整型类型值，则值为天气编号（暂不支持）。

bit0 – 该帧是否需要响应， 0 – 不响应，1 – 响应；

bit1 – 该帧是否是分包帧，0 – 不是，后跟App Data ，1 – 是，如果是分包帧那么后跟App Data Packet 。

其他bit保留。

注：如果是分包帧，所有的 App Data Packet 组包后 成为 App Data ，分包协议采用第7节介绍的协议格式，单包最大长度采用0x0000帧中的packetMaxSize。

App Data格式如下：

| App Data |
|---|
| 多协议设备自定义数据 |

详细介绍：双模设备蓝牙透传通道协议基线设计

---

8.90
设备请求天气数据-指定位置：0x8017

命令帧 BLE->APP：

| FLAG | status | 当前分包序号 | 当前分包长度 | 已接收的数据长度 | 总长度 |
|---|---|---|---|---|---|
| 2字节 | 1字节 | 2字节 | 2字节 | 4字节 | 4字节 |

版本号
	
位置数据
	
第1种天气数据
	
第2种天气数据
	
~
	
第n种天气数据


1字节
	
1/x字节
	
1字节
	
LEN_1字节
	
1字节
	
LEN_2字节
	
~
	
1字节
	
LEN_n字节


VERSION
	
LDATA
	
LEN_1
	
KEY_1
	
LEN_2
	
KEY_2
	
~
	
LEN_n
	
KEY_n

VERSION：指令格式版本号，当前为0 。

LDATA：请求的位置数据，格式定义如下所示。

位置类型
	
位置数据[仅类型为3时有该字段]


1字节
	
1字节
	
1字节
	
VALUE_LEN


LOCATION_TYPE
	
VALUE_TYPE
	
VALUE_LEN
	
VALUE

LOCATION_TYPE: 请求位置类型，定义如下：

1 - 配网位置，无后续位置数据；

2 - 手机当前位置，无后续位置数据；

3 - 自定义位置，有后续位置数据。自定义一般由面板内配置并通过DP点下发给设备。

VALUE_TYPE：城市参数值类型，0x00 - 整型数据，0x01 - 字符串，其他- 保留；

注：是否需要该响应帧，取决于请求帧中的FLAG 。

---

## 8.98 Combo Wireless Modules上行透传通道：0x801F（按需分包）

命令帧 BLE->APP：

响应帧 APP->BLE：

| FLAG | App Data Packet OR App Data |
|---|---|
| 2字节 | 1 – packetMaxSize |

FLAG：请求标识，定义如下：

bit0 – 该帧是否需要响应， 0 – 不响应，1 – 响应；

bit1 – 该帧是否是分包帧，0 – 不是，后跟App Data ，1 – 是，如果是分包帧那么后跟App Data Packet 。

1 – 失败，没有天气数据会下发。

注：如果是分包帧，所有的 App Data Packet 组包后 成为 App Data ，分包协议采用第7节介绍的协议格式，单包最大长度采用0x0000帧中的packetMaxSize。

8.91
下发天气数据-指定位置：0x8018

| App Data |
|---|
| 多协议设备自定义数据 |

DATA：下发的天气数据，包括位置信息与天气数据，天气数据以LKTLV数据格式下发，如下所示。

版本号
	
位置数据
	
第1种天气数据
	
~
	
第n种天气数据


1字节
	
1/x字节
	
1字节
	
KEY_LEN
	
1字节
	
1字节
	
VALUE_LEN
	
~
	
x


VERSION
	
LDATA
	
KEY_LEN
	
KEY
	
VALUE_TYPE
	
VALUE_LEN
	
VALUE
	
~
	
x

| FLAG | status | 当前分包序号 | 当前分包长度 | 已接收的数据长度 | 总长度 |
|---|---|---|---|---|---|
| 2字节 | 1字节 | 2字节 | 2字节 | 4字节 | 4字节 |

LDATA：位置数据，定义同请求命令帧格式。

status： 0 – 全部接收完成；

KEY ：天气参数，例如：w.temp/w.pm25/w.condition等；

VALUE_TYPE：天气参数值类型，0x00 - 整型数据，0x01 - 字符串，其他- 保留；

VALUE_LEN：天气参数值长度；

VALUE：天气参数（KEY）的值，例如温度是35度，KEY就是w.temp，VALUE就是35。

当前分包长度：固件2字节；

例：w.temp:35 w.pm25:14 w.condition:晴 对应的DATA数据为（16进制）：

00  06  77 2e 74 65 6d 70  00  04  00 00 00 23   06  77 2e 70 6d 32 35  00  04  00 00 00 0e  0b  77 2e 63 6f 6e 64 69 74 69 6f 6e  01  03  e6 99 b4

注：是否需要该响应帧，取决于请求帧中的FLAG 。

---

## 8.99 APP读取配件连接信息：0x8020

77 2e 74 65 6d 70：w.temp；

00：整型数据；

| 1字节 | 1字节 | 1字节 |
|---|---|---|
| VERSION | ATTACH_LEN | ATTACH_DATA |

VERSION：指令格式版本号，当前为0 。

06：strlen(“w.pm25”)；

77 2e 70 6d 32 35：w.pm25；

BLE设备无需响应。配件连接信息通过“设备上报配件连接信息指令”发送。

04  数值长度，4字节；

---

### 0b：strlen(“w.condition”)；

77 2e 63 6f 6e 64 69 74 69 6f 6e：w.condition；

01：值为字符串型；

| 1字节 | 1字节 | ATTACH_LEN字节 | 1字节 | n字节 | ~ | n字节 |
|---|---|---|---|---|---|---|
| VERSION | ATTACH_LEN | ATTACH_DATA | ACCESSORY_NUM | ACCESSORY_INFO1 | ATTACH_LEN | ACCESSORY_INFOn |

e6 99 b4：“晴”的UTF-8编码。

| 1字节 | UUID_LEN字节 | 1字节 | ID_LEN字节 | UUID | ID_LEN字节 |
|---|---|---|---|---|---|
| UUID_LEN | UUID | STATE | ID_TYPE | FW_INFO_LEN字节 | PRODUCT_ID |
| 2字节 | SHORT_ID | FW_INFO_LEN字节 | ENC_INFO | n字节 | ID_TYPE |
| SHORT_ID | FW_INFO_LEN | FW_INFO | PRODUCT_ID | ENC_INFO | FW_INFO_LEN字节 |

天气状况w.conditon的值，如果类型为字符串型，则是中文的UTF-8编码，详见附录“天气UTF-8编码对照表”，如果是整型类型值，则值为天气编号（暂不支持）。

ATTACH_LEN：附加信息长度。

ATTACH_DATA：附加信息数据，APP读取时下发的数据原样返回，主动上报时无附加信息。

ACCESSORY_NUM：配件个数。

UUID_LEN：配件DEVICE_UUID的长度。

UUID：配件DEVICE_UUID。

status_bit：

bit0：1 – 配件连接；0 – 配件断开；

其他bit保留。

ID_TYPE：配件PID类型。0-Product ID；1-Product key。

ID Len：1字节，目前为8或者16；

PRODUCT_ID：配件产品ID。

分包最大长度
	
子命令
	
数据（TLD格式）


packetMaxSize
	
sub_cmd
	
Type
	
Length
	
Data


2字节
	
2字节
	
1字节
	
2字节
	
n字节

packetMaxSize：2字节，备端指定分包传输IOT数据的最大长度。

| 1 | 2 | 3 | 4 | 5 | 6 | 7 | ~ | n |
|---|---|---|---|---|---|---|---|---|
| channel | soft_version |  |  | hard_version |  |  |  | channel |
| n+1 | n+2 | n+3 | n+4 | n+5 | n+6 |  |  |  |
| soft_version |  |  | hard_version |  |  |  |  |  |

子命令
	
含义


0x0001
	
请求场景数据


0x0002
	
请求场景执行/控制


...
	
...

| 1 | 2 | 3~ |
|---|---|---|
| ENC_TYPE | ENC_LEN | ENC_DATA |

ENC_TYPE：配件 DEVICE_UUID 的保护方式
◦
0 – 兼容方式（仅限协议版本 ≤5 的设备使用，见下述 ENC_TYPE=0 说明）；
◦
1 – 安全方式（协议版本 ≥6 的设备必须使用，见下述 ENC_TYPE=1 说明）；
◦
其他 – 保留。

当sub_cmd=0x0001时，TLD定义如下：

TLD（sub_cmd=0x0001，请求场景数据）


Type
	
Length
	
Data


类型
	
数据长度
	
请求场景个数
	
请求场景
名称长度
	
是否请求
场景图片
	
场景校验码


1字节
	
2字节
	
1字节
	
2字节
	
1字节
	
4字节


0x00
	
0x0008
	
~
	
~
	
~
	
~

- • ENC_TYPE 为 0 时，该字段存放的是加密的配件 DEVICE_UUID，加密算法是 AES-CBC，加密 key 为主设备auth_key 的前 16 个字节计算 MD5，IV 等于 key，DEVICE_UUID 不为 16 字节整倍数时，补 0 对齐到16 的整倍数。（历史方式）

- • ENC_TYPE 为 1 时，该字段存放的是加密的配件 DEVICE_UUID，格式与算法如下：  HMAC-SHA-256_auth_key(配件DEVICE_UUID)，加密 key 为主设备auth_key。

是否请求场景图片：1-请求场景图片，0-不请求场景图片，预留不做实现。

场景校验值：用于增量同步，采用crc32校验算法。APP下发的场景数据中均会带场景校验值，设备端需保存。下一次设备再次请求场景数据时携带上报，APP与当前最新的场景校验值比对，如果不一致则同步下发最新的场景数据，否则不做下发。

Data格式：

| STATE<br>1字节 |
|---|

TLD（sub_cmd=0x0002，请求场景执行）


Type
	
Length
	
Data


类型
	
数据长度
	
场景ID长度
	
场景ID
	



1字节
	
2字节
	
1字节
	
n字节
	



0x00
	
1+n
	
n
	
~

SceneID_Length：场景ID长度，最大24字节。

## 8.101 APP下发配件激活信息：0x8022

命令帧 APP->BLE：

响应帧 APP->BLE：

| 1字节 | 1字节 | n字节 | n字节 | n字节 |
|---|---|---|---|---|
| VERSION | ACCESSORY_NUM | ACCESSORY_INFO1 | VERSION | ACCESSORY_INFOn |

Sub_cmd
	
TLD（APP SDK应答）


Sub_cmd
	
Type
	
Length
	
Data


子命令
	
类型
	
数据长度
	
状态
	
错误码


2字节
	
1字节
	
2字节
	
1字节
	
4字节


~
	
~
	
1/5
	
~
	
~

| 2字节 | 1字节 | UUID_LEN字节 | 1字节 |
|---|---|---|---|
| SHORT_ID | UUID_LEN | UUID | ACTIVE_STATE |

VERSION：指令格式版本号，当前为0 。

0 – 成功，无后续的错误码字段，请求的IOT数据会通过0x801A指令下发；

1 – 失败，有后续的错误码字段，表示具体的错误类型，没有IOT数据会下发。

错误码：当STATUS为失败时，该字段有效，表示具体的错误类型。

UUID：配件DEVICE_UUID。

8.93
APP下发IOT数据：0x801A（分包）

命令帧 APP->BLE：

子命令
	
数据（TLD格式）


sub_cmd
	
Type
	
Length
	
Data


2字节
	
1字节
	
2字节
	
n字节

Sub_cmd、Type：同请求帧。

Data: 业务层返回的业务数据。

APP SDK将业务层返回的数据按上述格式组包后分包发送，分包协议采用第7节介绍的协议格式，单包最大长度采用0x8019帧中的packetMaxSize。注：对于app的每一个分包设备都会响应，app只有收到对应的分包响应后才能发送下一分包。

| STATE<br>1字节 |
|---|

当sub_cmd=0x0001时，数据Data部分定义如下：

Data


场景基础信息
	
场景1数据
	
~
	
场景n数据


STATUS 
	
Err
code
	
是否
需要
更新
	
场景
数据
校验码
	
场景ID
长度
	
场景ID
	
场景名称
长度
	
场景名称
编码
	
~
	
场景ID
长度
	
场景ID
	
场景名称
长度
	
场景名称
编码


1字节
	
4字节
	
1字节
	
4字节
	
1字节
	
~字节
	
2字节
	
~字节
	
~
	
1字节
	
~字节
	
2字节
	
~字节

## 8.102 BLE Beacon遥控器代理认证：0x8030

0—成功，err_code固定为0x00000000；

1—失败，错误码字段表示具体的错误，无后续字段。

| 1字节 | 6字节 | 1字节 | 2字节 | 1字节 | 6字节 | 1字节 | 2字节 | …… |
|---|---|---|---|---|---|---|---|---|
| type | mac | id | S1 | type | mac | id | S1 | 最多5个 |

type：操作类型，0x00-首次认证，0x01-认证结果查询，0x02-移除。

mac：遥控器Mac地址。

id：遥控器群组ID，按位表示，一个遥控器最多支持8个群组，例如订阅群组2则为0000,0010。进行代理认证之前，被控设备已经通过本地配对的方式订阅了一个默认群组，订阅遥控器群组即表示将遥控器上面的某个按键（群组ID即为按键序号）与被控设备关联起来。

S1：S1[0] = sum(authkey[0~15]) % 256，S1[1] = rand() % 256; //务必想办法做到真随机。

4—场景不存在，sceneid不匹配

5—动作中没有设备

S1由设备生成，云端认证，App透传即可。

7—场景执行失败

是否需要更新场景数据：0-不需要，1-需要。APP端接收到设备请求的校验值与最新的场景数据校验值比对，如果不一致则需要更新，否则不需要更新（无后续字段）。

场景数据校验码：场景数据的CRC32校验值。

| 1字节 | 6字节 | 1字节 | 1字节 | 1字节 | 6字节 | 1字节 | 1字节 | …… |
|---|---|---|---|---|---|---|---|---|
| type | mac | id | status | type | mac | id | status | 最多5个 |

type：操作类型，0x00-首次认证，0x01-认证结果查询。

场景名称长度：描述后续场景名称编码字段长度，指转为unicode编码后的长度。

id：遥控器群组ID，按位表示，一个遥控器最多支持8个群组，例如订阅群组2则为0000,0010。进行代理认证之前，被控设备已经通过本地配对的方式订阅了一个默认群组，订阅遥控器群组即表示将遥控器上面的某个按键（群组ID即为按键序号）与被控设备关联起来。

status：0x00 – 成功，0x01 – 失败。

当sub_cmd=0x0002时，数据Data部分定义如下：

### Data STATUS err_code sceneID_Length SceneID 1字节 4字节 1字节 n字节

STATUS：用于表示场景执行结果；

0—成功，err_code固定为0x00000000；

| mac<br>6字节 | id<br>1字节 |
|---|---|

err_code：错误码，详见上文定义。

id：遥控器群组ID，按位表示，一个遥控器最多支持8个群组，例如订阅群组2则为0000,0010。订阅遥控器群组的操作一般处于被控设备的面板中，订阅遥控器群组即表示将遥控器上面的某个按键（群组ID即为按键序号）与被控设备关联起来。

SceneID：场景ID。

响应帧 BLE->APP：

响应帧 BLE->APP：

| status<br>1字节 |
|---|

status
	
当前分包序号
	
当前分包长度
	
已接收的数据长度
	
总长度


1字节
	
2字节
	
2字节
	
4字节
	
4字节

status： 0 – 全部接收完成；

## 8.104 BLE Beacon遥控器群组移除：0x8032

2 – 当前分包错误，需要从头发送；

3 – 其他错误，蓝牙即将断开；

| mac<br>6字节 | id<br>1字节 |
|---|---|

mac：遥控器Mac地址。

id：遥控器群组ID，按位表示，一个遥控器最多支持8个群组，例如移除群组2则为0000,0010。移除遥控器群组的操作一般处于被控设备的面板中，移除遥控器群组即表示将遥控器上面的某个按键（群组ID即为按键序号）与被控设备关联起来。

总长度：4字节，需要接收的数据总长度，该值在app发送第一个分包时包含。

8.94
APP业务下行透传通道：0x801B（按需分包）

命令帧 APP->BLE：

| status<br>1字节 |
|---|

FLAG
	
App Data Packet OR App Data


2字节
	
1 – packetMaxSize

FLAG：请求标识，定义如下：

### bit0 – 该帧是否需要响应， 0 – 不响应，1 – 响应；

bit1 – 该帧是否是分包帧，0 – 不是，后跟App Data ，1 – 是，如果是分包帧那么后跟App Data Packet

其他bit保留。

| mac<br>6字节 |
|---|

mac：遥控器Mac地址。

subCmd
	
subCmd Data


2字节
	
x字节

subCmd：用于区分不同类型的透传数据，0-用于定时服务，1-用于传递 耳机增益补偿数据，其他详见内部wiki维护的业务透传sub_cmd维护表

subCmd Data: 业务方自定义数据。

| mac<br>6字节 | id<br>1字节 |
|---|---|

mac：遥控器Mac地址。

id：遥控器群组ID，按位表示，一个遥控器最多支持8个群组，例如订阅群组2则为0000,0010。订阅遥控器群组的操作一般处于被控设备的面板中，订阅遥控器群组即表示将遥控器上面的某个按键（群组ID即为按键序号）与被控设备关联起来。

FLAG
	
status
	
当前分包序号
	
当前分包长度
	
已接收的数据长度
	
总长度


2字节
	
1字节
	
2字节
	
2字节
	
4字节
	
4字节

---

## 8.106 设备异常解绑-V2：0x002A

1 – 当前分包有效，可以发送下一包；

2 – 当前分包错误，需要从头发送；

| 标识 | 扩展描述信息 |  |  |  |  |  |  | Flag | TLV1 |
|---|---|---|---|---|---|---|---|---|---|
| Flag | TLV1 |  | TLVn-1 | TLVn-1 |  |  | TLVn |  | 1字节 |
| 1字节 | 1字节 | 1字节 | n字节 | 1字节 | 1字节 | n字节 | 1字节 | 1字节 | data |
| Flag | tag | len | data | data | len | data | tag | data | data |

Flag：bit2-bit0用于清除数据标识，定义0-不清除，1-清除，其他-待扩展。bit7-bit3：保留；

扩展描述信息：采用通用的TLV形式表示，支持单条/组合，预留后续扩展用。

已接收的数据长度：4字节，表示目前为止已经接收到的字节数；

总长度：4字节，需要接收的数据总长度，该值在app发送第一个分包时包含。

注：是否需要该响应帧，取决于请求帧中的FLAG 。

注：是否有扩展描述信息，可通过数据长度判定，如数据长度=1，则仅有Flag字段，无后续的TLV信息。

8.95
APP业务上行透传通道：0x801C（按需分包）

响应帧 BLE->APP：

DATA格式：

| 状态码<br>1字节 |
|---|

状态码：0-成功，1-设备不支持（失败），2-数据长度错误，其他-失败。

bit0 – 该帧是否需要响应， 0 – 不响应，1 – 响应；

### bit1 – 该帧是否是分包帧，0 – 不是，后跟App Data ，1 – 是，如果是分包帧那么后跟App Data Packet 。

| 天气数据参数<br>（KEY） | 天气数据参数 | 值类型 | 国内 | 国外 | 是否支持实时 | 是否支持预报 |
|---|---|---|---|---|---|---|
| w.temp | 温度 | 整数 | 支持 | 支持 | w.thigh | 最高温度 |
| w.thigh | 最高温度 | 整形 | 否 | w.tlow | 最低温度 | 是 |
| w.tlow | 最低温度 | 整形 | 否 |  | w.humidity | 湿度 |
| w.humidity | 湿度 | 整数 | 支持 | 支持 | 支持7天内数据 | w.condition |
| 天气状况描述文本，晴、雨、雪等，非固定以多语言配置为准，在不同数据中心、不同语种情况下（支持本地化翻译），值的文本内容会有所不同。建议用conditionNum，返回天气状况编码 | 天气状况描述文本，晴、雨、雪等，非固定以多语言配置为准，在不同数据中心、不同语种情况下（支持本地化翻译），值的文本内容会有所不同。建议用conditionNum，返回天气状况编码 | 字符串 | 支持 | 支持7天内数据 | w.pressure | w.pressure |
| w.pressure | 气压 | 整数 | 不支持<br>(大陆地址不支持) | w.realFeel | 温度实感 | 支持7天内数据 |
| w.realFeel | 温度实感 | w.uvi | 紫外线指数 | 支持 | 不支持 | 支持 |
| w.uvi | w.sunRise | 整数 | 不支持 | 支持 | 是 | 支持7天内数据 |
| w.sunRise | 日出时间 | 字符串（格式：2017-04-24 05:24:00） | 字符串（格式：2017-04-24 18:32:00） | 支持 | 是 | 支持7天内数据 |
| w.sunSet | 日落时间 | 字符串（格式：2017-04-24 18:32:00） | 支持 | 支持 | 是 | 本地时间<br>[搭配日出/日落使用] |
| t.unix | 格林时间<br>[搭配日出/日落使用] |  |  | w.windSpeed | 风速 | 支持 |
| t.local | 本地时间<br>[搭配日出/日落使用] | w.windDir |  | 支持 |  | 不支持 |
| w.windSpeed | 风等级 | 字符串（格式：0.9） | 支持 | 不支持 | 是 | 支持7天内数据 |
| w.windDir | 风向 | 字符串 | 支持 | 不支持 | w.tips | 天气贴士 |
| w.windLevel | 风等级 | 整数 | 支持 | 不支持 | 是 | w.rank/<br>w.quality |
| w.aqi | w.quality | 详细AQI实况及全国排名 | 字符串（格式：447/609） | 支持 （仅大陆地址支持） | 不支持 | 否 |
| w.tips | w.pm10 | w.pm10 | pm10指数 | 整数 | 支持 | 不支持 |
| w.rank/<br>w.quality | 详细AQI实况及全国排名 | 字符串（格式：447/609） | 支持 （仅大陆地址支持） | pm2.5指数 | 整数 | 支持 |
| w.pm10 | pm10指数 | 整数 | 支持 | w.o3 | w.o3 | o3指数 |
| w.pm25 | pm2.5指数 | 整数 | 不支持 | 不支持 | 是 | w.no2 |
| no2指数 | o3指数 | 整数 | 支持 | 不支持 | 是 | 否 |
| w.no2 | no2指数 | co指数 | 支持 | 不支持 | 不支持 | 否 |
| w.co | w.so2 | 二氧化硫指数 | 二氧化硫指数 | 不支持 | 支持 | 不支持 |
| w.so2 | 二氧化硫指数 | 整数 | w.date.n | 需要预报天数<br>（n表示，需要预报的天数，1<= n <=7，1表示当天） | 是 | 否 |
| w.date.n | 需要预报天数<br>（n表示，需要预报的天数，1<= n <=7，1表示当天） | 支持 | 支持 | 支持 |  |  |
| w.conditionNum | 天气状态，晴、雨、雪等，以数字编码方式表示 | 字符串 | 支持 | 支持 | 是 | 支持7天内数据 |

### 注：如果是分包帧，所有的 App Data Packet 组包后 成为 App Data 。

| 编号<br>conditionNum | 天气（中文）<br>condition | 中文UTF-8编码（16进制） | 天气（英文）<br>condition |
|---|---|---|---|
| 中文UTF-8编码（16进制） | 天气（英文）<br>condition | E699B4 | no precipitation |
| no precipitation | 101 | E5A4A7 E99BA8 | heavy rain |
| E5A4A7 E99BA8 | heavy rain | E99BB7 E69AB4 | thunderstorms\|possible thunderstorms |
| 103 | E99BB7 E69AB4 | E6B299 E5B098 E69AB4 | thunderstorms\|possible thunderstorms |
| 104 | 小雪 | E5B08F E99BAA | light snow\|possible light snow |
| dangerously windy | 104 | E99BAA | light snow\|possible light snow |
| 106 | 冻雾 | E586BB E99BBE | frozen fog |
| 107 | 暴雨 | E69AB4 E99BA8 | heavy precipitation |
| frozen fog | 局部阵雨 | E5B180 E983A8 E998B5 E99BA8 | partially rainy |
| heavy precipitation | 108 | E6B5AE E5B098 | E5B180 E983A8 E998B5 E99BA8 |
| partially rainy | partially rainy | E99BB7 E794B5 | thunder flash |
| 111 | E6B5AE E5B098 | E5B08F E998B5 E99BA8 | light precipitation\|possible light precipitation |
| 112 | 雷电 | E99BA8 | precipitation |
| thunder flash | 雨夹雪 | E99BA8 E5A4B9 E99BAA | possible light sleet\|light sleet |
| light precipitation\|possible light precipitation | light precipitation\|possible light precipitation | E5B098 E58DB7 E9A38E | dust devil |
| 115 | 冰粒 | E586B0 E7B292 | precipitation |
| precipitation | 强沙尘暴 | E5BCBA E6B299 E5B098 E69AB4 | strong sand storm |
| E99BA8 E5A4B9 E99BAA | E99BA8 E5A4B9 E99BAA | possible light sleet\|light sleet | blowing sand |
| 118 | 小到中雨 | E5B08F E588B0 E4B8AD E99BA8 | small to medium rain |
| 119 | 大部晴朗 | E5A4A7 E983A8 E699B4 E69C97 | mixed precipitation |
| ice particle | ice particle | E99BBE | foggy |
| 强沙尘暴 | 阵雨 | E5BCBA E6B299 E5B098 E69AB4 | strong sand storm |
| 123 | 强阵雨 | E5BCBA E998B5 E99BA8 | strong shower |
| E689AC E6B299 | blowing sand | E5A4A7 E99BAA | heavy snow |
| 125 | E5B08F E588B0 E4B8AD E99BA8 | E789B9 E5A4A7 E69AB4 E99BA8 | small to medium rain |
| 126 | 大部晴朗 | E5A4A7 E983A8 E699B4 E69C97 | mixed precipitation |
| 127 | 冰雹 | E586B0 E99BB9 | sleet |
| foggy | 小到中雪 | E5B08F E588B0 E4B8AD E99BAA | small to medium snow |
| 129 | E998B5 E99BA8 | E5B091 E4BA91 | partly cloudy |
| 130 | 小阵雪 | E5B08F E998B5 E99BAA | E5BCBA E998B5 E99BA8 |
| strong shower | strong shower | E4B8AD E99BAA | snow |
| 132 | E5A4A7 E99BAA | heavy snow | overcast |
| 133 | 特大暴雨 | E789B9 E5A4A7 E69AB4 E99BA8 | E789B9 E5A4A7 E69AB4 E99BA8 |
| very heavy snow | 大暴雨 | E5A4A7 E69AB4 E99BA8 | rainy big |
| E69AB4 E99BAA | blizzard | E99BB7 E998B5 E99BA8 E4BCB4 E69C89 E586B0 E99BB9 | shower with hail |
| E586B0 E99BB9 | sleet | E586BB E99BA8 | ice rain |
| E5B08F E588B0 E4B8AD E99BAA | small to medium snow | small to medium snow | flurries |
| 139 | E5B091 E4BA91 | partly cloudy | possible  drizzle\|drizzle\|possible light rain\|light rain |
| 140 | 小阵雪 | E5B08F E998B5 E99BAA | possible flurries |
| 141 | 中雨 | E4B8AD E99BA8 | E4B8AD E99BAA |
| snow | 132 | E5A49A E4BA91 | mostly cloudy |
| overcast | 雷阵雨 | E99BB7 E998B5 E99BA8 | shower with thunder |
| 144 | E586B0 E99288 | E4B8AD E588B0 E5A4A7 E99BA8 | medium to big rain |
| 145 | 大到暴雨 | E5A4A7 E588B0 E69AB4 E99BA8 | big to heavy rain |

subCmd
	
subCmd Data


2字节
	
x字节
