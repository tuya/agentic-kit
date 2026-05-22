#ifndef __STM_TYPEDEF_H__
#define __STM_TYPEDEF_H__

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if OPERATING_SYSTEM == SYSTEM_LINUX
#include <ctype.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief token max length
 */
#define STM_TOKEN_MAX_LEN            2048

/**
 * @brief encrypt key max length
 */
#define STM_ENCRYPT_KEY_MAX_LEN      64

/**
 * @brief crypt key max length
 */
#define STM_CRYPT_KEY_MAX_LEN        32

/**
 * @brief data ids max number of session
 */
#define STM_SESSION_DATA_IDS_MAX_LEN 16

/**
 * @brief uuid max len
 */
#define STM_UUID_MAX_LEN             38

/**
 * @brief connection ID max length
 */
#define STM_CID_MAX_LEN              32

/**
 * @brief Session ID max length
 */
#define STM_SID_MAX_LEN              64

/**
 * @brief Agent Token max length
 */
#define STM_AGENT_TOKEN_MAX_LEN      128

/**
 * @brief Event ID max length
 */
#define STM_EVENT_ID_MAX_LEN         64

/**
 * @brief buffer reserved length
 */
#define STM_BUFFER_RESERVED_SIZE     32

/**
 * @brief file name max length
 */
#define STM_FILE_NAME_MAX_LEN        255

/**
 * @brief signaling stream id
 */
#define STM_SIGNALING_ID             0

/**
 * @brief log level
 */
typedef uint8_t stm_log_level_e;
#define STM_LOG_LEVEL_VERBOSE 0
#define STM_LOG_LEVEL_DEBUG   1
#define STM_LOG_LEVEL_INFO    2
#define STM_LOG_LEVEL_WARN    3
#define STM_LOG_LEVEL_ERROR   4
#define STM_LOG_LEVEL_FATAL   5
#define STM_LOG_LEVEL_NONE    6

/**
 * @brief network status
 */
typedef uint8_t stm_network_flag_e;
#define STM_NETWORK_FLAG_UNKNOWN  0
#define STM_NETWORK_FLAG_WIFI     1
#define STM_NETWORK_FLAG_MOBILE   2
#define STM_NETWORK_FLAG_ETHERNET 3

/**
 * @brief connection/session state detail
 */
typedef int16_t stm_state_detail_e;
#define STM_STATE_DETAIL_OK                     (0)    // 客户端正常关闭
#define STM_STATE_DETAIL_HEARTBEAT_TIMEOUT      (1)    // 连接心跳超时
#define STM_STATE_DETAIL_SERVER_CONFIG_EXPIRE   (2)    // 服务端配置过期
#define STM_STATE_DETAIL_AUTHEN_FAILED          (3)    // 服务端认证失败
#define STM_STATE_DETAIL_AUTHOR_FAILED          (4)    // 服务端鉴权失败
#define STM_STATE_DETAIL_DATA_INVALID           (5)    // 数据包不合法(服务端下发)
#define STM_STATE_DETAIL_SERVER_OFFLINE         (6)    // 服务端下线
#define STM_STATE_DETAIL_INTERNAL_ERROR         (7)    // SDK内部错误
#define STM_STATE_DETAIL_NOT_GET_SERVER_CONFIG  (8)    // 未获取到服务端配置
#define STM_STATE_DETAIL_IDLE_TIMEOUT           (9)    // 空闲超时(12小时),需要重新获取token连接
#define STM_STATE_DETAIL_MAX_ONLINE_TIME        (10)   // 达到最大在线时长(24小时),需要重新获取token连接
#define STM_STATE_DETAIL_NETWORK_CHANGED        (11)   // 网络发生变化,wifi和移动网络切换
#define STM_STATE_DETAIL_SERVER_GEN_SIGN_FAILED (12)   // 服务端生成握手响应签名失败
#define STM_STATE_DETAIL_SERVER_CONN_CLOSE      (13)   // 服务端连接不存在（服务端重启后，之前的连接已失效）
#define STM_STATE_DETAIL_RECOVER_TIMEOUT        (14)   // 恢复重连超时
#define STM_STATE_DETAIL_RECOVER_FAILED         (15)   // 恢复重连失败

/**
 * @brief connection state
 */
typedef uint8_t stm_connection_state_e;
#define STM_CONNECTION_STATE_NEW          (0)   // 初始化
#define STM_CONNECTION_STATE_CONNECTING   (1)   // 连接中
#define STM_CONNECTION_STATE_CONNECTED    (2)   // 已连接
#define STM_CONNECTION_STATE_RECOVERING   (3)   // 恢复连接中(心跳超时，保留会话，仅重连socket)
#define STM_CONNECTION_STATE_DISCONNECTED (4)   // 连接断开，如数据包不合法，云端服务器下线，恢复连接失败等
#define STM_CONNECTION_STATE_FAILED       (5)   // 建立失败，如认证失败，鉴权失败，服务端配置过期，内部错误等

/**
 * @brief session state
 */
typedef uint8_t stm_session_state_e;
#define STM_SESSION_STATE_UNKNOWN (0)
#define STM_SESSION_STATE_NEW     (1)
#define STM_SESSION_STATE_CLOSE   (2)

/**
 * @brief stream state
 */
typedef uint8_t stm_stream_state_e;
#define STM_STREAM_STATE_NEW    (0)
#define STM_STREAM_STATE_CLOSED (1)

/**
 * @brief stream type
 */
typedef int8_t stm_stream_type_e;
#define STM_STREAM_TYPE_VIDEO      (1)
#define STM_STREAM_TYPE_AUDIO      (2)
#define STM_STREAM_TYPE_RELIABLE   (3)
#define STM_STREAM_TYPE_UNRELIABLE (4)

/**
 * @brief data type
 */
typedef uint8_t stm_data_type_e;
#define STM_DATA_TYPE_CMD   (1)
#define STM_DATA_TYPE_VIDEO (2)
#define STM_DATA_TYPE_AUDIO (3)
#define STM_DATA_TYPE_IMAGE (4)
#define STM_DATA_TYPE_FILE  (5)
#define STM_DATA_TYPE_TEXT  (6)

/**
 * @brief command type
 */
typedef uint8_t stm_cmd_type_e;
#define STM_CMD_TYPE_UNKNOWN (0)
#define STM_CMD_TYPE_BREAK   (1)

/**
 * @brief command parameters
 */
typedef struct stm_cmd_params_s {
    stm_cmd_type_e cmd_type;
} stm_cmd_params_t;

/**
 * @brief recv packet type
 */
typedef uint8_t stm_recv_packet_type_e;
#define STM_RECV_PACKET_TYPE_INSTRUCTION (1)
#define STM_RECV_PACKET_TYPE_DATA        (2)

/**
 * @brief client type
 */
typedef uint8_t stm_client_type_e;
#define STM_CLIENT_TYPE_DEVICE  (1)
#define STM_CLIENT_TYPE_APP     (2)
#define STM_CLIENT_TYPE_OME_APP (3)   // TODO 开发者

#if defined(__cplusplus) && __cplusplus >= 201103L
/**
 * @brief stream_flag
 */
enum stm_stream_flag_e : uint8_t {
    STM_STREAM_FLAG_TYPE_ONE = 0,
    STM_STREAM_FLAG_TYPE_START = 1,
    STM_STREAM_FLAG_TYPE_MID = 2,
    STM_STREAM_FLAG_TYPE_END = 3
};

/**
 * @brief frag_flag
 */
enum stm_frag_flag_e : uint8_t {
    STM_FRAG_FLAG_NO_FRAG = 0,
    STM_FRAG_FLAG_START = 1,
    STM_FRAG_FLAG_MID = 2,
    STM_FRAG_FLAG_END = 3
};
#elif __STDC_VERSION__ >= 202311L
/**
 * @brief stream_flag
 */
enum stm_stream_flag_e : uint8_t {
    STM_STREAM_FLAG_TYPE_ONE = 0,
    STM_STREAM_FLAG_TYPE_START = 1,
    STM_STREAM_FLAG_TYPE_MID = 2,
    STM_STREAM_FLAG_TYPE_END = 3
};

/**
 * @brief frag_flag
 */
enum stm_frag_flag_e : uint8_t {
    STM_FRAG_FLAG_NO_FRAG = 0,
    STM_FRAG_FLAG_START = 1,
    STM_FRAG_FLAG_MID = 2,
    STM_FRAG_FLAG_END = 3
};
#else
/**
 * @brief stream_flag
 */
typedef uint8_t stm_stream_flag_e;
#define STM_STREAM_FLAG_TYPE_ONE   ((stm_stream_flag_e)0)
#define STM_STREAM_FLAG_TYPE_START ((stm_stream_flag_e)1)
#define STM_STREAM_FLAG_TYPE_MID   ((stm_stream_flag_e)2)
#define STM_STREAM_FLAG_TYPE_END   ((stm_stream_flag_e)3)

/**
 * @brief frag_flag
 */
typedef uint8_t stm_frag_flag_e;
#define STM_FRAG_FLAG_NO_FRAG      ((stm_frag_flag_e)0)
#define STM_FRAG_FLAG_START        ((stm_frag_flag_e)1)
#define STM_FRAG_FLAG_MID          ((stm_frag_flag_e)2)
#define STM_FRAG_FLAG_END          ((stm_frag_flag_e)3)
#endif

/**
 * @brief priority level
 */
typedef uint8_t stm_priority_level_e;
#define STM_PRIORITY_LEVEL_NORMAL (0)
#define STM_PRIORITY_LEVEL_HIGH   (1)

/**
 * @brief connection id
 */
typedef struct stm_cid {
    uint16_t handle;
    char id[STM_CID_MAX_LEN + 1];
} stm_cid_t;

/**
 * @brief session id
 */
typedef struct stm_sid {
    uint16_t conn_handle;
    char id[STM_SID_MAX_LEN + 1];
} stm_sid_t;

/**
 * @brief event id
 */
typedef struct stm_event_id {
    uint16_t conn_handle;
    char id[STM_EVENT_ID_MAX_LEN + 1];
    char session_id[STM_SID_MAX_LEN + 1];
} stm_event_id_t;

/**
 * @brief stream id
 */
typedef struct stm_stream_id {
    uint16_t id;
    uint16_t conn_handle;
} stm_stream_id_t;

/**
 * @brief attribute
 */
typedef struct stm_attribute {
    uint16_t attribute_type;
    uint32_t payload_length;
    uint8_t *payload;   // big endian for integer types
} stm_attribute_t;

/**
 * @brief attributes
 */
typedef struct stm_attributes {
    uint32_t attributes_length;
    stm_attribute_t *attributes;
} stm_attributes_t;

/**
 * @brief video parameters
 */
typedef struct stm_video_params {
    /** video codec type **/
    uint16_t codec_type;
    /** video sample rate **/
    uint32_t sample_rate;
    /** video width **/
    uint16_t width;
    /** video height **/
    uint16_t height;
    /** video fps **/
    uint16_t fps;
    /** video container type **/
    uint16_t container;
    /** video bitrate **/
    uint32_t bitrate;
} stm_video_params_t;

/**
 * @brief audio parameters
 */
typedef struct stm_audio_params {
    /** audio codec type **/
    uint16_t codec_type;
    /** audio sample rate **/
    uint32_t sample_rate;
    /** audio channels **/
    uint16_t channels;
    /** audio bit depth **/
    uint16_t bit_depth;
    /** audio container type **/
    uint16_t container;
    /** audio bitrate **/
    uint32_t bitrate;
    /** audio frame duration in ms **/
    uint16_t frame_duration;
    /** audio frame size in bytes **/
    uint16_t frame_size;
} stm_audio_params_t;

/**
 * @brief image parameters
 */
typedef struct stm_image_params {
    /** image payload type, 0-raw 1-base64 2-url **/
    uint8_t payload_type;
    /** image format, 1-jpeg 2-png **/
    uint8_t format;
    /** image width **/
    uint16_t width;
    /** image height **/
    uint16_t height;
} stm_image_params_t;

/**
 * @brief file parameters
 */
typedef struct stm_file_params {
    /** file payload type, 0-raw 1-base64 2-url **/
    uint8_t payload_type;
    /** file format, 1-mp4 2-ogg 3-pdf 4-json 5-log 6-map **/
    uint8_t format;
    /** file name **/
    char name[STM_FILE_NAME_MAX_LEN];
} stm_file_params_t;

/**
 * @brief text parameters
 */
typedef struct stm_text_params {
} stm_text_params_t;

/**
 * @brief data
 */
typedef struct stm_data_s {
    /** data type **/
    stm_data_type_e data_type;
    /** stream id **/
    uint16_t stream_id;
    /** stream flag **/
    stm_stream_flag_e stream_flag;

    /** data parameters **/
    union {
        /** video parameters **/
        stm_video_params_t video;
        /** audio parameters **/
        stm_audio_params_t audio;
        /** image parameters **/
        stm_image_params_t image;
        /** file parameters **/
        stm_file_params_t file;
        /** text parameters **/
        stm_text_params_t text;
    };

    /** timestamp, only video/audio/image valid **/
    uint64_t timestamp;
    /** data total length, for fragmented data, indicates total length; for large files fragmented sending, fragment
     * size should not exceed 200KB **/
    uint32_t total_length;
    /** data fragment flag, 0: not fragmented, 1: first fragment, 2: middle or last fragment **/
    uint8_t frag_flag;
    /** data payload, when payload size exceeds library remaining buffer size, sending fails, recommended not to exceed
     * 200KB **/
    uint8_t *payload;
    /** data payload length; when payload is fragmented data, indicates fragment length **/
    uint32_t payload_length;
    /** user data **/
    char *user_data;
} stm_data_t;

/**
 * @brief instruction
 */
typedef struct stm_instruction_s {
    /** instruction type **/
    uint16_t type;
    /** attributes list **/
    stm_attribute_t *attributes;
    /** attributes count **/
    uint32_t attributes_length;
    /** payload **/
    uint8_t *payload;
    /** payload length **/
    uint32_t payload_length;
} stm_instruction_t;

/**
 * @brief recv packet
 */
typedef struct stm_recv_packet {
    /** recv packet type **/
    stm_recv_packet_type_e type;
    /** priority level, 0: normal priority, 1: high priority, send immediately **/
    stm_priority_level_e priority;

    union {
        /** instruction packet **/
        stm_instruction_t instruction;
        /** data packet **/
        stm_data_t data;
    };
} stm_recv_packet_t;

/**
 * @brief network stats
 * rtt_us: round trip time in microseconds
 * srtt_us: smoothed round trip time in microseconds
 * loss_rate: loss rate
 */
typedef struct stm_network_stats {
    int64_t rtt_us;
    int64_t srtt_us;
    float loss_rate;
} stm_network_stats_t;

/**
 * @brief log callback
 */
typedef void (*stm_log_cb_t)(stm_log_level_e level, const char *log, uint32_t log_len);

/**
 * @brief connection state change callback
 */
typedef void (*stm_conn_on_state_cb_t)(stm_cid_t *cid,
                                       stm_connection_state_e state,
                                       stm_state_detail_e state_detail,
                                       void *user);

/**
 * @brief connection state change callback
 */
typedef void (*stm_conn_on_network_stats_cb_t)(stm_cid_t *cid, stm_network_stats_t *network_stats, void *user);

/**
 * @brief session state change callback
 */
typedef void (*stm_session_on_state_cb_t)(stm_sid_t *sid,
                                          stm_session_state_e state,
                                          stm_state_detail_e state_detail,
                                          void *user);

/**
 * @brief session on recv callback
 */
typedef void (*stm_session_on_recv_cb_t)(stm_sid_t *sid, stm_recv_packet_t *recv_packet, void *user);

/**
 * @brief Certificate chain verification callback function type
 *
 * Externally provided certificate chain verification function that uses system APIs for verification
 *
 * @param user_data User data
 * @param count Number of certificates in the chain
 * @param bufs Array of certificate DER data pointers (from leaf certificate to root certificate)
 * @param lens Array of certificate data lengths
 * @return 0 for success, non-zero for failure
 */
typedef int (*stm_tls_verify_cert_chain_cb)(void *user_data, int count, const unsigned char **bufs, size_t *lens);

/*******base type definition*******/
typedef unsigned char uchat_t;
typedef char *pchar_t;
typedef void *pvoid_t;

typedef uint8_t stm_bool_t;
#define STM_TRUE  (1)
#define STM_FALSE (0)

typedef uint64_t stm_usec_t;

#define STM_INT8_MAX   127
#define STM_INT8_MIN   -128
#define STM_INT16_MAX  32767
#define STM_INT16_MIN  -32768
#define STM_INT32_MAX  2147483647
#define STM_INT32_MIN  (-2147483648)
#define STM_INT64_MAX  9223372036854775807LL
#define STM_INT64_MIN  (-9223372036854775807LL - 1LL)
#define STM_UINT8_MAX  255U
#define STM_UINT16_MAX 65535U
#define STM_UINT32_MAX 4294967295U
#define STM_UINT64_MAX 18446744073709551615ULL

#ifndef SCHAR_MAX
#define SCHAR_MAX STM_INT8_MAX
#endif
#ifndef SCHAR_MIN
#define SCHAR_MIN STM_INT8_MIN
#endif
#ifndef SHRT_MAX
#define SHRT_MAX STM_INT16_MAX
#endif
#ifndef SHRT_MIN
#define SHRT_MIN STM_INT16_MIN
#endif
#ifndef INT_MAX
#define INT_MAX STM_INT32_MAX
#endif
#ifndef INT_MIN
#define INT_MIN STM_INT32_MIN
#endif
#ifndef LONG_MAX
#define LONG_MAX STM_INT64_MAX
#endif
#ifndef LONG_MIN
#define LONG_MIN STM_INT64_MIN
#endif
#ifndef LLONG_MAX
#define LLONG_MAX STM_INT64_MAX
#endif
#ifndef LLONG_MIN
#define LLONG_MIN STM_INT64_MIN
#endif

#ifndef UCHAR_MAX
#define UCHAR_MAX STM_UINT8_MAX
#endif
#ifndef USHRT_MAX
#define USHRT_MAX STM_UINT16_MAX
#endif
#ifndef UINT_MAX
#define UINT_MAX STM_UINT32_MAX
#endif
#ifndef ULONG_MAX
#define ULONG_MAX STM_UINT64_MAX
#endif
#ifndef ULLONG_MAX
#define ULLONG_MAX STM_UINT64_MAX
#endif

#define STM_FLT_MAX      3.402823466e+38
#define STM_FLT_MIN      1.175494351e-38
#define STM_DBL_MAX      1.7976931348623157e+308
#define STM_DBL_MIN      2.2250738585072014e-308
#define STM_LDBL_MAX     1.7976931348623157e+308
#define STM_LDBL_MIN     2.2250738585072014e-308
#define STM_FLT_EPSILON  1.19209290e-7
#define STM_DBL_EPSILON  2.2204460492503131e-16
#define STM_LDBL_EPSILON 2.2204460492503131e-16

#ifndef FLT_MAX
#define FLT_MAX STM_FLT_MAX
#endif
#ifndef FLT_MIN
#define FLT_MIN STM_FLT_MIN
#endif
#ifndef DBL_MAX
#define DBL_MAX STM_DBL_MAX
#endif
#ifndef DBL_MIN
#define DBL_MIN STM_DBL_MIN
#endif
#ifndef LDBL_MAX
#define LDBL_MAX STM_LDBL_MAX
#endif
#ifndef LDBL_MIN
#define LDBL_MIN STM_LDBL_MIN
#endif
#ifndef FLT_EPSILON
#define FLT_EPSILON STM_FLT_EPSILON
#endif
#ifndef DBL_EPSILON
#define DBL_EPSILON STM_DBL_EPSILON
#endif
#ifndef LDBL_EPSILON
#define LDBL_EPSILON STM_LDBL_EPSILON
#endif

#define STM_EXPORT __attribute__((visibility("default")))

#ifdef __cplusplus
}
#endif

#endif   //__STM_DEFS_H__
