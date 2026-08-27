#ifndef EDU_CAMERA_DEMO_H
#define EDU_CAMERA_DEMO_H

/**
 * @brief Run image understanding demo using steam-sdk Open layer.
 *
 * @param token      Session token from iot_client_get_session_token().
 * @param local_key  Device local key used as encrypt_key for the session.
 * @param session_id Unique session identifier string.
 * @param img_path   Path to the image file (JPEG/PNG).
 * @param prompt     Text prompt to accompany the image.
 * @param audio_path Path to save the output TTS audio.
 * @return 0 on success, non-zero on error.
 */
int demo_image_understand_run(const char *token, const char *local_key,
                              const char *session_id, const char *img_path,
                              const char *prompt, const char *audio_path);

#endif /* EDU_CAMERA_DEMO_H */
