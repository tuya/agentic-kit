---
title: 用 Wireshark 解密 TLS 抓包（TLS key log）
sidebar_label: TLS 抓包解密
sidebar_position: 9
---

# 用 Wireshark 解密 TLS 抓包

SDK 与云端的所有通信（IoT-DNS、ATOP HTTPS、MQTT、RTC/TAI）都走 TLS，抓包只能看到密文。排查
"云端说我发的字段不对"、"下行帧解析不出来"这类问题时，需要看到明文的应用层数据。

SDK 提供 TLS key log：握手时把会话密钥以 NSS `SSLKEYLOGFILE` 格式导出，Wireshark 读取这个文件
后即可解密同一份抓包。相比在中间架一个 TLS 代理，这种方式不改变设备的连接目标，也不需要给设备
换证书。

:::danger 导出的就是会话密钥本身

key log 文件的每一行都是一次连接的会话密钥。拿到它的人可以解密该设备这段时间的全部流量，其中包括
MQTT 密码、ATOP 签名和 AI session token。

- 只在排查问题时开启，且用测试账号 / 测试设备；
- 文件写到不会被打包进固件、不会随日志上传的位置；
- 排查结束后删除文件，并关掉这个开关。

默认是关闭的：应用不调用下面两个函数，SDK 不会导出任何东西。但这是**运行时**开关，不是编译期
开关——两个函数在所有构建里都存在。生产固件里唯一能发现"调试开关带上线了"的迹象，是下文那条
`[tls] key logging ENABLED` 警告日志。
:::

## 开启

两个函数都声明在 `common/tls.h`：

```c
#include "tls.h"

/* 方式一：直接写文件（POSIX 与 ESP-IDF 默认编入；其他目标需 -DTLS_KEYLOG_FILE_SINK=1） */
int  tls_keylog_open_file(const char *path);
void tls_keylog_close_file(void);

/* 方式二：自己接收每一行（串口、日志服务、环形缓冲……） */
typedef void (*tls_keylog_fn)(void *ctx, const char *line);
void tls_set_keylog_handler(tls_keylog_fn fn, void *ctx);
```

### 方式一：写文件

```c
#include "tls.h"

int main(void)
{
    /* 在第一次 TLS 连接之前调用——iot_client_init() 内部就会建连，
       所以要放在它前面。检查返回值：打不开文件时不会有任何一行被导出。 */
    if (tls_keylog_open_file("/var/tmp/tuya-debug/keylog.txt") != TLS_OK) {
        fprintf(stderr, "key log disabled\n");   /* 原因见 [tls] 日志 */
    }

    iot_client_t *client = iot_client_init(&cfg);
    /* ... 正常跑业务，同时用 tcpdump / Wireshark 抓包 ... */

    iot_client_deinit(client);
    tls_keylog_close_file();
    return 0;
}
```

路径不要放在 `/tmp` 这类多人可写的目录下的固定文件名：文件里是会话密钥，别人可以预先放一个同名文件
或符号链接等着。用自己目录下的路径。

在 POSIX 上文件以 `0600` 权限创建、不跟随符号链接，每行写入后立刻 `fsync`，所以设备中途重启也不会让
已抓到的包变成无法解密。其他平台（如 ESP-IDF 的 VFS）只保证不经过 stdio 缓冲，落盘时机取决于该
文件系统对未 flush 写入的处理。写入失败会打一条 `LOG_ERROR`（只打一次），之后的行都会丢。

### 方式二：自定义 sink

没有文件系统的目标（例如只有串口的 MCU）用这个：把每一行打到串口，在 PC 侧存成文件。

```c
static void keylog_to_uart(void *ctx, const char *line)
{
    (void)ctx;
    /* line 以 '\0' 结尾、自带换行，可直接输出 */
    uart_write_string(line);
}

tls_set_keylog_handler(keylog_to_uart, NULL);
```

`tls_set_keylog_handler(NULL, NULL)` 关闭导出。如果当时文件 sink 处于打开状态，会先把文件关掉。
和所有换 sink 的操作一样，要在没有 `tls_connect()` 正在进行时调用。

## 作用范围与时机

- **进程级，一次开启覆盖所有连接**：MQTT、ATOP HTTPS、IoT-DNS 与 RTC/TAI 通道都会导出，不需要
  逐个连接配置。
- **在第一次 `tls_connect()` 之前、单线程环境下开启**。`tls_connect()` 在建连时读取一次开关：
  已经建立的连接不受影响，之后新建的连接才会导出。SDK 内部没有锁——在另一个线程正在握手时换
  sink 是未定义行为，不是"晚一行"。
- 回调在**调用 `tls_connect()` 的那个线程**上触发：iot-client 是调用 `iot_client_init()` /
  `iot_client_process()` 的线程，RTC/TAI 是调用 `tai_connect()` 的线程（`tai_connect()` 在调用方
  线程上同步完成握手，之后才拉起接收线程）。如果两者可能同时建连，自定义 sink 必须**线程安全**——
  整行原子写出（例如加互斥锁），否则两行会交错，Wireshark 两行都认不出。
- 导出需要 mbedTLS 3.x（仓库自带 3.6）。在 2.x 上 sink 可以装上，但不会有任何输出，日志里会有提示。
- 开启时会打一条 `LOG_WARN`：

  ```
  [tls] key logging ENABLED -- session secrets are being exported to /var/tmp/tuya-debug/keylog.txt; do not use in production
  ```

  在生产日志里看到这一行，说明有固件把调试开关带上线了。

## 导出的内容

行的格式取决于协商出的 TLS 版本：

| 通道 | TLS 版本 | 导出的行 |
|------|---------|---------|
| iot-client（MQTT / ATOP / DNS） | 固定 TLS 1.2 | 每条连接一行 `CLIENT_RANDOM` |
| RTC/TAI | 由服务端协商，可能是 TLS 1.3 | `CLIENT_HANDSHAKE_TRAFFIC_SECRET` / `SERVER_HANDSHAKE_TRAFFIC_SECRET` / `CLIENT_TRAFFIC_SECRET_0` / `SERVER_TRAFFIC_SECRET_0` 等多行 |

示例（TLS 1.2，密钥已改写）：

```
CLIENT_RANDOM 5f2e...（64 个十六进制字符） 9a41...（96 个十六进制字符）
```

## 在 Wireshark 中使用

1. 抓包。设备与 PC 同一台机器时：

   ```sh
   sudo tcpdump -i any -w tuya.pcap 'tcp port 8883 or tcp port 443'
   ```

   设备是独立硬件时，在设备与路由器之间的镜像口 / 旁路抓。

2. Wireshark 里指定 key log 文件：**Preferences → Protocols → TLS →
   `(Pre)-Master-Secret log filename`**，选刚才写出的文件。命令行等价写法：

   ```sh
   tshark -r tuya.pcap -o tls.keylog_file:/var/tmp/tuya-debug/keylog.txt -Y mqtt
   ```

3. 之前显示为 `Application Data` 的包会解出 MQTT / HTTP 明文。过滤器直接用 `mqtt`、`http` 即可。

抓包与 key log 必须来自**同一次运行**：密钥是每条连接一套的，换一次运行就对不上了。

## 解不出来时

| 现象 | 原因 |
|------|------|
| key log 文件不存在或是空的 | `tls_keylog_open_file()` 返回了错误（路径不可写、文件系统未挂载、该平台未编入文件 sink），看 `[tls] cannot open key log file` 日志里的原因；或开关设在第一次连接之后了（`iot_client_init()` 内部已经建连）；或者根本没有建成 TLS 连接，看 [TLS 证书验证](./tls-cert-verification.md#查看日志) 里的握手日志说明 |
| 文件有内容但中途断了 | 写入失败，日志里有一条 `[tls] key log write failed`（磁盘满、文件系统变只读） |
| 文件有内容但 Wireshark 仍显示密文 | 抓包和 key log 不是同一次运行；或抓包漏掉了握手（Client Hello 必须在包里，Wireshark 靠 client random 对应密钥） |
| 只解出一部分连接 | 该连接的握手发生在开启之前，或抓包中途才开始 |
| MQTT 明文解出来了，但 payload 还是乱码 | 正常：MQTT payload 之上还有一层 AES-GCM（用 `local_key` 前 16 字节加密），那是 SDK 的应用层加密，不是 TLS |

## 相关

- [TLS 证书验证](./tls-cert-verification.md)——生产环境该配的东西，与本页的调试开关无关。
