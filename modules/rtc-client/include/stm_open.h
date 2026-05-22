#ifndef __STM_OPEN_H__
#define __STM_OPEN_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "stm_errno.h"
#include "stm_typedef.h"

typedef struct stm_open_config {
    /** log callback **/
    stm_log_cb_t on_log;
} stm_open_config_t;

typedef struct stm_open_session stm_open_session_t;

typedef struct stm_open_data {
    /** event id ,only for first packet of a event **/
    char *event_id;
    /** data type **/
    stm_data_type_e data_type;
    /** data parameters ,only for first packet of a event **/
    union {
        /** video parameters **/
        stm_video_params_t video_params;
        /** audio parameters **/
        stm_audio_params_t audio_params;
        /** image parameters **/
        stm_image_params_t image_params;
        /** file parameters **/
        stm_file_params_t file_params;
        /** text parameters **/
        stm_text_params_t text_params;
        /** command parameters **/
        stm_cmd_params_t cmd_params;
    };
    /** data payload **/
    uint8_t *payload;
    /** data payload length **/
    uint32_t payload_length;
    /** app data **/
    char *app_data;
    /** timestamp, only video/audio/image valid **/
    uint64_t timestamp;
} stm_open_data_t;

typedef void (*stm_open_session_on_state_cb_t)(stm_open_session_t *session, uint16_t state, void *user_data);
typedef void (*stm_open_session_on_data_recv_cb_t)(stm_open_session_t *session, stm_open_data_t *data,int8_t fin, void *user_data);

typedef struct stm_open_session_config {
    /** client type **/
    stm_client_type_e client_type;
    /** session token **/
    char *session_token;
    /** session id **/
    char *session_id;
    /** encrypt key **/
    char *encrypt_key;
    /** state change callback **/
    stm_open_session_on_state_cb_t on_state;
    /** recv callback **/
    stm_open_session_on_data_recv_cb_t on_data_recv;
    /** app data **/
    char* app_data;
    /** user data **/
    void *user_data;
} stm_open_session_config_t;

/**
 * @brief init stm open sdk
 * @param config open sdk config
 * @return STM_OK on success. Others on error
 */
stm_ret stm_open_init(stm_open_config_t *config);

/**
 * @brief  reset stm open sdk
 * @param config open sdk config
 * @return STM_OK on success. Others on error
 */
stm_ret stm_open_reset(stm_open_config_t *config);

/**
 * @brief deinit stm open sdk
 * @return void
 */
void stm_open_deinit(void);

/**
 * @brief get stm open sdk version
 * @return stm open sdk version
 */
uint32_t stm_open_get_version(void);

/**
 * @brief set stm open sdk log level
 * @param level log level
 * @return STM_OK on success. Others on error
 */
stm_ret stm_open_set_log_level(stm_log_level_e level);

/**
 * @brief create session
 * @param config session config
 * @return session on success. NULL on error
 */
 stm_open_session_t* stm_open_session_create(stm_open_session_config_t *config);

/**
 * @brief send data
 * @param session session
 * @param data data to send
 * @param fin fin flag, 0: not the last packet, 1: the last packet
 * @return STM_OK on success. Others on error
 */
stm_ret stm_open_session_send(stm_open_session_t *session, stm_open_data_t *data, int8_t fin);

/**
 * @brief close session
 * @param session session
 * @return void
 */
void stm_open_session_close(stm_open_session_t *session);


#ifdef __cplusplus
}
#endif

#endif // __STM_OPEN_H__




