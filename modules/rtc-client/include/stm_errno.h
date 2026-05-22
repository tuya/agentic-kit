#ifndef __STM_ERRNO_H__
#define __STM_ERRNO_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int32_t stm_ret;
#define STM_OK                          (-0x0000)   // 0, 执行成功
#define STM_ENOT_INIT                   (-0x0001)   // -1, 未初始化
#define STM_EINIT_MORE_THAN_ONCE        (-0x0002)   // -2, 初始化超过一次
#define STM_ETIMEOUT                    (-0x0003)   // -3, 超时
#define STM_EINVALID_PARM               (-0x0004)   // -4, 无效的入参
#define STM_ENOT_CONNECTED              (-0x0005)   // -5, 连接未建立
#define STM_ECONNECTION_CLOSED          (-0x0006)   // -6, 连接已关闭
#define STM_EOUT_OF_CONNECTION          (-0x0007)   // -7, 连接数已满
#define STM_EMALLOC_FAILED              (-0x0008)   // -8, 内存分配失败
#define STM_EAUTH_FAILED                (-0x0009)   // -9, 校验失败
#define STM_EHEARTBEAT_TIMEOUT          (-0x000a)   // -10, 心跳超时
#define STM_EINVALID_TOKEN              (-0x000b)   // -11, 无效的TOKEN
#define STM_ETIME_OUT_NO_ANSWER         (-0x000c)   // -12, 超时未收到响应
#define STM_ETIME_OUT_LOCAL_NAT         (-0x000d)   // -13, 本地网络异常
#define STM_ETIME_OUT_REMOTE_NAT        (-0x000e)   // -14, 对端网络异常
#define STM_ETOKEN_EXPIRED              (-0x000f)   // -15, token过期
#define STM_ETOKEN_AUTH_FAILED          (-0x0010)   // -16, token校验失败
#define STM_EINTERNAL_ERROR             (-0x0011)   // -17, 内部错误
#define STM_ENET_ERROR                  (-0x0012)   // -18, 网络错误
#define STM_ENOT_SUPPORTED              (-0x0013)   // -19, 不支持
#define STM_ENOT_FOUND                  (-0x0014)   // -20, 没有找到对象
#define STM_EBUFFER_NOT_ENOUGH          (-0x0015)   // -21, 缓存不足
#define STM_ERECV_DA_NOT_ENOUGH         (-0x0016)   // -22, 接收数据不完整
#define STM_ECMD_BUFFER_FULL            (-0x0017)   // -23, CMD缓存满
#define STM_ECREATE_STREAM_FAILED       (-0x0018)   // -24, 创建流失败
#define STM_ERECV_NO_DATA               (-0x0019)   // -25, 无接收数据
#define STM_ERECV_DATA_UNMARSHAL_FAILED (-0x001a)   // -26, 接收数据反序列化失败
#define STM_EAGAIN                      (-0x001b)   // -27, 需要重试（非阻塞操作）
#define STM_ETIMEDOUT                   (-0x001c)   // -28, 操作超时
#define STM_ESSL_HANDSHAKE_FAILED       (-0x001d)   // -29, SSL/TLS/DTLS 握手失败
#define STM_ESSL_WRITE_FAILED           (-0x001e)   // -30, SSL/TLS/DTLS 写入失败
#define STM_ESSL_READ_FAILED            (-0x001f)   // -31, SSL/TLS/DTLS 读取失败
#define STM_ESSL_CONN_CLOSED            (-0x0020)   // -32, SSL/TLS/DTLS 连接关闭
#define STM_EENCRYPT_FAILED             (-0x0021)   // -33, 加密失败
#define STM_EDECRYPT_FAILED             (-0x0022)   // -34, 解密失败
#define STM_ESSL_CERT_PARSE_FAILED      (-0x0023)   // -35, 证书解析失败
#define STM_ESSL_CERT_VERIFY_FAILED     (-0x0024)   // -36, 证书验证失败
#define STM_ESSL_NO_PEER_CERT           (-0x0025)   // -37, 无对等证书
#define STM_ESSL_CERT_INFO_FAILED       (-0x0026)   // -38, 获取证书信息失败

#ifdef __cplusplus
}
#endif

#endif /* __STM_ERRNO_H__ */
