#ifndef DEMO_CHAT_H
#define DEMO_CHAT_H

/**
 * @brief Run audio/text chat demo using steam-sdk Open layer.
 *
 * @param token      Session token from iot_client_get_session_token()
 *                   (base64-encoded JSON; passed directly to stm_open_session_create).
 * @param local_key  Device local key used as encrypt_key for the session.
 * @param session_id Unique session identifier string.
 * @param input_pcm  Path to 16kHz/mono/16-bit PCM file, or NULL to send a text greeting.
 * @return 0 on success, non-zero on error.
 */
int demo_chat_run(const char *token, const char *local_key,
                  const char *session_id, const char *input_pcm);

#endif /* DEMO_CHAT_H */
