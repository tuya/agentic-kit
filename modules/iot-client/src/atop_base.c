/**
 * @file atop_base.c
 * @brief This file contains the implementation of the ATOP protocol base
 * functions, including URL parameter encoding and decoding, request data
 * encoding, response data decoding, and response result parsing.
 *
 * The ATOP base functions are designed to facilitate communication between
 * devices and the Tuya cloud platform. They provide mechanisms for secure data
 * transmission through encryption and decryption, as well as data integrity
 * verification through MD5 signatures.
 *
 * The functions implemented in this file are essential for devices to correctly
 * format requests to the Tuya cloud platform and to parse responses from the
 * platform. They ensure that data is transmitted securely and efficiently,
 * adhering to the ATOP protocol specifications.
 *
 * @copyright Copyright (c) 2021-2024 Tuya Inc. All Rights Reserved.
 *
 */

#include <string.h>
#include <strings.h>
#include <inttypes.h>
#include <stdio.h>
#include "atop_base.h"
#include "http_client_interface.h"
#include "cipher_wrapper.h"

#include "iot_config_defaults.h"
#include "cJSON.h"
#include "mbedtls/base64.h"
#include "mbedtls/md5.h"


 #define MD5SUM_LENGTH               (16)
 #define POST_DATA_PREFIX            (5) // 'data='
 #define MAX_URL_LENGTH              (255)
 #define DEFAULT_RESPONSE_BUFFER_LEN (1024)
 #define AES_GCM128_NONCE_LEN        12
 #define AES_GCM128_TAG_LEN          16
 #define URL_PARAM_SIGN_BUFFER_LEN   (512)


 // Error check macro
 #define TUYA_CHECK_NULL_RETURN(ptr, ret) \
     do { \
         if ((ptr) == NULL) { \
             return (ret); \
         } \
     } while (0)

 typedef struct {
     char *key;
     char *value;
 } url_param_t;

 static int atop_url_params_sign(const pal_t *pal, const char *key, url_param_t *params, int param_num, uint8_t *out, size_t *olen)
 {
     int rt = OPRT_OK;
     size_t printlen = 0;
     int i = 0;
     uint8_t digest[MD5SUM_LENGTH];

     char *buffer = pal->malloc(URL_PARAM_SIGN_BUFFER_LEN);
     TUYA_CHECK_NULL_RETURN(buffer, OPRT_MALLOC_FAILED);

     for (i = 0; i < param_num; ++i) {
         int ret = snprintf(buffer + printlen, URL_PARAM_SIGN_BUFFER_LEN - printlen, "%s=%s||", params[i].key, params[i].value);
         if (ret < 0 || (size_t)ret >= URL_PARAM_SIGN_BUFFER_LEN - printlen) {
             pal->free(buffer);
             return OPRT_COMMUNICATION_ERROR;
         }
         printlen += (size_t)ret;
     }
     int ret = snprintf(buffer + printlen, URL_PARAM_SIGN_BUFFER_LEN - printlen, "%s", (char *)key);
     if (ret < 0 || (size_t)ret >= URL_PARAM_SIGN_BUFFER_LEN - printlen) {
     pal->free(buffer);
         return OPRT_COMMUNICATION_ERROR;
     }
     printlen += (size_t)ret;

     // make md5 digest bin
     mbedtls_md5_context ctx;
     mbedtls_md5_init(&ctx);
     int md5_ret = mbedtls_md5_starts(&ctx);
     if (md5_ret == 0) md5_ret = mbedtls_md5_update(&ctx, (const uint8_t *)buffer, printlen);
     if (md5_ret == 0) md5_ret = mbedtls_md5_finish(&ctx, digest);
     mbedtls_md5_free(&ctx);
     pal->free(buffer);
     if (md5_ret != 0) {
         log_error("MD5 computation failed: -0x%04x", -md5_ret);
         return OPRT_COMMUNICATION_ERROR;
     }

     // make digest hex
     char sign_buf[MD5SUM_LENGTH * 2 + 1] = {0};
     size_t sign_offset = 0;
     for (i = 0; i < MD5SUM_LENGTH; i++) {
         ret = snprintf(sign_buf + sign_offset, sizeof(sign_buf) - sign_offset, "%02x", digest[i]);
         if (ret < 0 || (size_t)ret >= sizeof(sign_buf) - sign_offset) {
             return OPRT_COMMUNICATION_ERROR;
         }
         sign_offset += (size_t)ret;
     }
     memcpy(out, sign_buf, sign_offset);
     out[sign_offset] = '\0';
     *olen = sign_offset;
     return rt;
 }

 static int atop_url_params_encode(const pal_t *pal, const char *key, url_param_t *params, int param_num, char *out, size_t out_len,
                                 size_t *olen)
 {
     int rt = OPRT_OK;
     char *buffer = out;
     size_t printlen = 0;
     size_t sign_len = 0;
     int i;

     // attach url params
     for (i = 0; i < param_num; i++) {
         int ret = snprintf(buffer + printlen, out_len - printlen, "%s=%s&", params[i].key, params[i].value);
         if (ret < 0 || (size_t)ret >= out_len - printlen) {
             return OPRT_COMMUNICATION_ERROR;
         }
         printlen += (size_t)ret;
     }

     // attach md5 signature
     int ret = snprintf(buffer + printlen, out_len - printlen, "sign=");
     if (ret < 0 || (size_t)ret >= out_len - printlen) {
         return OPRT_COMMUNICATION_ERROR;
     }
     printlen += (size_t)ret;
     rt = atop_url_params_sign(pal, key, params, param_num, (uint8_t *)buffer + printlen, &sign_len);
     if (rt != 0) {
         log_error("atop_url_params_sign error:%d", rt);
         return rt;
     }
     printlen += sign_len;
     if (printlen >= out_len) {
         return OPRT_COMMUNICATION_ERROR;
     }
     buffer[printlen] = '\0';
     *olen = printlen;
     return rt;
 }

 static int atop_request_data_encode(const pal_t *pal, const char *key, const uint8_t *input, int ilen, uint8_t *output,
                                     size_t output_len, size_t *olen)
 {
     if (key == NULL || output == NULL || olen == NULL) {
         return OPRT_INVALID_PARAMETER;
     }
     if (ilen > 0 && input == NULL) {
         return OPRT_INVALID_PARAMETER;
     }

     int ret = 0;
     size_t printlen = 0;
     int i;

     /* Encode buffer */
     size_t encrypt_olen = 0;
     size_t buflen = AES_GCM128_NONCE_LEN + ilen + AES_GCM128_TAG_LEN;
     uint8_t *encrypted_buffer = pal->malloc(buflen);
     if (encrypted_buffer == NULL) {
         return OPRT_MALLOC_FAILED;
     }

     /* Nonce - generate random nonce */
     int ret_rng = iot_random_bytes(encrypted_buffer, AES_GCM128_NONCE_LEN);
     if (ret_rng != 0) {
         log_error("Failed to generate nonce: -0x%04x", -ret_rng);
         pal->free(encrypted_buffer);
         return OPRT_INVALID_PARAMETER;
     }

     /* AES128-GCM */
     const uint8_t *data_ptr = (ilen > 0) ? (const uint8_t *)input : (const uint8_t *)"";
     ret = mbedtls_cipher_auth_encrypt_wrapper(&(const cipher_params_t){.cipher_type = CIPHER_TYPE_AES_128_GCM,
                                                                     .key = (const unsigned char *)key,
                                                                     .key_len = 16,
                                                                     .nonce = (const unsigned char *)encrypted_buffer,
                                                                     .nonce_len = AES_GCM128_NONCE_LEN,
                                                                     .ad = NULL,
                                                                     .ad_len = 0,
                                                                     .data = data_ptr,
                                                                     .data_len = ilen},
                                             encrypted_buffer + AES_GCM128_NONCE_LEN, &encrypt_olen,
                                             encrypted_buffer + AES_GCM128_NONCE_LEN + ilen, AES_GCM128_TAG_LEN);
     if (ret != OPRT_OK) {
         log_error("mbedtls_cipher_auth_encrypt_wrapper:0x%x", ret);
         pal->free(encrypted_buffer);
         return ret;
     }

     // output the hex data
     int write_len = snprintf((char *)output + printlen, output_len - printlen, "%s", "data=");
     if (write_len < 0 || (size_t)write_len >= output_len - printlen) {
         pal->free(encrypted_buffer);
         return OPRT_COMMUNICATION_ERROR;
     }
     printlen += (size_t)write_len;
     for (i = 0; i < (int)buflen; i++) {
         write_len =
             snprintf((char *)output + printlen, output_len - printlen, "%02X", (uint8_t)(encrypted_buffer[i]));
         if (write_len < 0 || (size_t)write_len >= output_len - printlen) {
             pal->free(encrypted_buffer);
             return OPRT_COMMUNICATION_ERROR;
         }
         printlen += (size_t)write_len;
     }

     pal->free(encrypted_buffer);
     if (printlen >= output_len) {
         return OPRT_COMMUNICATION_ERROR;
     }
     output[printlen] = '\0';
     *olen = printlen;
     return ret;
 }

 static int atop_response_result_decrpyt(const char *key, const uint8_t *input, size_t ilen, uint8_t *output, size_t *olen)
 {
     if (key == NULL || input == NULL || output == NULL || olen == NULL) {
         return OPRT_INVALID_PARAMETER;
     }
     if (ilen < AES_GCM128_NONCE_LEN + AES_GCM128_TAG_LEN) {
         return OPRT_INVALID_PARAMETER;
     }

     int rt = OPRT_OK;

     rt = mbedtls_cipher_auth_decrypt_wrapper(
         &(const cipher_params_t){.cipher_type = CIPHER_TYPE_AES_128_GCM,
                                 .key = (const unsigned char *)key,
                                 .key_len = 16,
                                 .nonce = (const unsigned char *)input,
                                 .nonce_len = AES_GCM128_NONCE_LEN,
                                 .ad = NULL,
                                 .ad_len = 0,
                                 .data = (const unsigned char *)(input + AES_GCM128_NONCE_LEN),
                                 .data_len = ilen - AES_GCM128_NONCE_LEN - AES_GCM128_TAG_LEN},
         output, olen, (unsigned char *)(input + (ilen - AES_GCM128_TAG_LEN)), AES_GCM128_TAG_LEN);
     if (rt != OPRT_OK) {
         log_error("aes128_ecb_decode error:%d", rt);
         return rt;
     }

     return rt;
 }

 static int atop_response_data_decode(const pal_t *pal, const char *key, const uint8_t *input, size_t ilen, uint8_t *output, size_t *olen)
 {
     int rt = OPRT_OK;
     (void)ilen;  // Parameter kept for API consistency

     char *value;
     size_t value_length;

     log_debug("atop_response_data_decode: %s", (char *)input);

     cJSON *root = cJSON_Parse((char *)input);
     if (NULL == root) {
         return OPRT_COMMUNICATION_ERROR;
     }

     cJSON *item = cJSON_GetObjectItem(root, "result");
     if (NULL == item) {
         log_error("no result");
         cJSON_Delete(root);
         return OPRT_COMMUNICATION_ERROR;
     }

     if (!cJSON_IsString(item) || item->valuestring == NULL) {
         log_error("result is not a string");
         cJSON_Delete(root);
         return OPRT_COMMUNICATION_ERROR;
     }
     value = item->valuestring;
     value_length = strlen(value);

     log_debug("base64 encode result:\r\n%.*s", (int)value_length, value);

     // base64 decode buffer
     size_t b64buffer_len = value_length * 3 / 4;
     uint8_t *b64buffer = pal->malloc(b64buffer_len);
     if (b64buffer == NULL) {
         log_error("Failed to allocate base64 decode buffer");
         cJSON_Delete(root);
         return OPRT_MALLOC_FAILED;
     }
     size_t b64buffer_olen = 0;

     // base64 decode
     rt = mbedtls_base64_decode(b64buffer, b64buffer_len, &b64buffer_olen, (const uint8_t *)value, value_length);
     if (rt != OPRT_OK) {
         log_error("base64 decode error:%d", rt);
         pal->free(b64buffer);
         cJSON_Delete(root);
         return rt;
     }

     rt = atop_response_result_decrpyt(key, (const uint8_t *)b64buffer, b64buffer_olen, output, olen);
     cJSON_Delete(root);
     pal->free(b64buffer);
     if (rt != OPRT_OK) {
         log_error("atop_data_decrpyt error: %d", rt);
         return rt;
     }
     log_debug("result:\r\n%.*s", (int)*olen, output);

     return rt;
 }

static int atop_response_result_parse_cjson(const uint8_t *input, size_t ilen, atop_base_response_t *response)
{
    int rt = OPRT_OK;
    (void)ilen;

    if (NULL == input || NULL == response) {
        log_error("param error");
        return OPRT_INVALID_PARAMETER;
    }

    // json parse (input buffer is expected to be null-terminated)
    cJSON *root = cJSON_Parse((const char *)input);
    if (NULL == root) {
        log_error("Json parse error");
        return OPRT_COMMUNICATION_ERROR;
    }

    // verify success key
    if (!cJSON_HasObjectItem(root, "success")) {
        log_error("not found json success key");
        cJSON_Delete(root);
        return OPRT_COMMUNICATION_ERROR;
     }

     // sync timestamp
     if (cJSON_HasObjectItem(root, "t")) {
         response->t = cJSON_GetObjectItem(root, "t")->valueint;
     }

     // if 'success == True', copy the json object to result point
     if (cJSON_GetObjectItem(root, "success")->type == cJSON_True) {
         response->success = true;
         response->result = cJSON_DetachItemFromObject(root, "result");
         cJSON_Delete(root);
         return OPRT_OK;
     }

     // Exception parse
     char *errorCode = NULL;
     response->success = false;
     response->result = NULL;

     // error msg dump
     if (cJSON_GetObjectItem(root, "errorMsg")) {
         log_error("errorMsg:%s", cJSON_GetObjectItem(root, "errorMsg")->valuestring);
     }

     if (cJSON_GetObjectItem(root, "errorCode") == NULL) {
         cJSON_Delete(root);
         return OPRT_COMMUNICATION_ERROR;
     }

     errorCode = cJSON_GetObjectItem(root, "errorCode")->valuestring;

     if (strcasecmp(errorCode, "GATEWAY_NOT_EXISTS") == 0) {
         rt = OPRT_COMMUNICATION_ERROR;
     }

     // free cJSON object
     cJSON_Delete(root);
     return rt;
 }

  /**
   * Sends a request to the Tuya cloud service.
   *
   * This function sends a request to the Tuya cloud service using the provided
   * request parameters.
   *
   * @param request The request parameters for the Tuya cloud service.
   * @param response The response structure to store the response from the Tuya
   * cloud service.
   * @return Returns an integer value indicating the status of the request:
   *         - OPRT_OK: The request was successful.
   *         - OPRT_INVALID_PARAMETER: Invalid parameters were provided.
   *         - OPRT_MALLOC_FAILED: Memory allocation failed.
   *         - OPRT_COMMUNICATION_ERROR: Error occurred while sending
   * the HTTP request.
   */
  int atop_base_request(const pal_t *pal, const atop_base_request_t *request, atop_base_response_t *response)
  {
      if (NULL == request || NULL == response) {
          return OPRT_INVALID_PARAMETER;
      }
      if (request->path == NULL || request->key == NULL || request->api == NULL || request->path[0] == '\0' ||
          request->key[0] == '\0' || request->api[0] == '\0') {
          return OPRT_INVALID_PARAMETER;
      }
      if ((request->data == NULL && request->datalen > 0) || (request->data != NULL && request->datalen == 0)) {
          return OPRT_INVALID_PARAMETER;
      }

      int rt = OPRT_OK;
      http_client_status_t http_status;

      /* user data */
      response->user_data = (void *)request->user_data;

      /* params fill */
      url_param_t params[6];
      int idx = 0;
      params[idx].key = "a";
      params[idx++].value = (char *)request->api;

      if (request->devid) {
          params[idx].key = "devId";
          params[idx++].value = (char *)request->devid;
      }

      params[idx].key = "et";
      params[idx++].value = "3";

      char ts_str[11];
      snprintf(ts_str, sizeof(ts_str), "%" PRIu32, request->timestamp);
      params[idx].key = "t";
      params[idx++].value = ts_str;

      if (request->uuid) {
          params[idx].key = "uuid";
          params[idx++].value = (char *)request->uuid;
      }

      if (request->version) {
          params[idx].key = "v";
          params[idx++].value = (char *)request->version;
      }

      /* url param buffer make */
      char *path_buffer = pal->malloc(MAX_URL_LENGTH);
      if (NULL == path_buffer) {
          log_error("path_buffer malloc fail");
          return OPRT_MALLOC_FAILED;
      }

      /* attach path prefix */
      int path_buffer_len = snprintf(path_buffer, MAX_URL_LENGTH, "%s?", (char *)request->path);
      if (path_buffer_len < 0 || path_buffer_len >= MAX_URL_LENGTH) {
          log_error("path_buffer snprintf fail");
          pal->free(path_buffer);
          return OPRT_COMMUNICATION_ERROR;
      }

      /* param encode */
      size_t encode_len = 0;
      size_t path_buffer_remain = (path_buffer_len < MAX_URL_LENGTH) ? (MAX_URL_LENGTH - path_buffer_len) : 0;
      if (path_buffer_remain == 0) {
          pal->free(path_buffer);
          return OPRT_COMMUNICATION_ERROR;
      }
      rt = atop_url_params_encode(pal, (char *)request->key, params, idx, path_buffer + path_buffer_len, path_buffer_remain,
                                  &encode_len);
      if (rt != OPRT_OK) {
          log_error("url param encode error:%d", rt);
          pal->free(path_buffer);
          return rt;
      }
      path_buffer_len += encode_len;
      log_debug("request url len:%d: %s", path_buffer_len, path_buffer);

      /* POST data buffer */
      size_t body_length = 0;
      size_t raw_len = request->datalen + AES_GCM128_NONCE_LEN + AES_GCM128_TAG_LEN;
      if (raw_len < request->datalen || raw_len > SIZE_MAX / 2) {
          log_error("request data too large");
          pal->free(path_buffer);
          return OPRT_INVALID_PARAMETER;
      }
      size_t body_buffer_len = POST_DATA_PREFIX + raw_len * 2 + 1;
      uint8_t *body_buffer = pal->malloc(body_buffer_len);
      if (NULL == body_buffer) {
          log_error("body_buffer malloc fail");
          pal->free(path_buffer);
          return OPRT_MALLOC_FAILED;
      }

      /* POST data encode */
      log_debug("atop_request_data_encode");
      rt = atop_request_data_encode(pal, (char *)request->key, request->data, request->datalen, body_buffer, body_buffer_len,
                                    &body_length);
      if (rt != OPRT_OK) {
          log_error("atop_post_data_encrypt error:%d", rt);
          pal->free(path_buffer);
          pal->free(body_buffer);
          return rt;
      }
      log_debug("out post data len:%d, data:%s", (int)body_length, body_buffer);

      /* HTTP headers */
      http_client_header_t headers[] = {
          {.key = "User-Agent", .value = "TUYA_IOT_SDK"},
          {.key = "Content-Type", .value = "application/x-www-form-urlencoded;charset=UTF-8"},
      };
      uint8_t headers_count = sizeof(headers) / sizeof(http_client_header_t);

      http_client_response_t http_response = {0};

      /* HTTP Request send */
      log_debug("http request send!");

      // Use server address from request, or default to Tuya cloud server
      const char *server_host = request->host ? request->host : IOT_DEFAULT_HOST;
      uint16_t server_port = (request->port > 0) ? request->port : IOT_DEFAULT_PORT;
      log_info("Connecting to server: %s:%d", server_host, server_port);

      http_status = http_client_request(&(const http_client_request_t){.cacert = request->cacert,
                                                                       .host = server_host,
                                                                       .port = server_port,
                                                                       .method = "POST",
                                                                       .path = path_buffer,
                                                                       .headers = headers,
                                                                       .headers_count = headers_count,
                                                                       .body = body_buffer,
                                                                       .body_length = body_length,
                                                                       .timeout_ms = IOT_HTTP_TIMEOUT_MS_DEFAULT,
                                                                       .pal = pal},
                                        &http_response);

      /* Release http buffer */
      pal->free(path_buffer);
      pal->free(body_buffer);

      if (HTTP_CLIENT_SUCCESS != http_status) {
          log_error("http_request_send error:%d", http_status);
          return (http_status == HTTP_CLIENT_TLS_ERROR)
                     ? OPRT_TLS_HANDSHAKE_FAILED
                     : OPRT_COMMUNICATION_ERROR;
      }

    size_t result_buffer_length = 0;
      uint8_t *result_buffer = pal->malloc(http_response.body_length + 1);
      if (NULL == result_buffer) {
          log_error("result_buffer malloc fail");
          http_client_free(pal, &http_response);
          return OPRT_MALLOC_FAILED;
      }
      memset(result_buffer, 0, http_response.body_length + 1);

      /* Decoded response data */
      rt = atop_response_data_decode(pal, request->key, http_response.body, http_response.body_length, result_buffer,
                                     &result_buffer_length);

      if (OPRT_OK == rt) {
          rt = atop_response_result_parse_cjson(result_buffer, result_buffer_length, response);
      } else {
          log_info("atop_response_decode error:%d, try parse the plaintext data.", rt);
          rt = atop_response_result_parse_cjson(http_response.body, http_response.body_length, response);
      }

      http_client_free(pal, &http_response);
      pal->free(result_buffer);

      return rt;
  }

  /**
   * @brief Frees the memory allocated for an atop_base_response_t structure.
   *
   * This function frees the memory allocated for an atop_base_response_t
   * structure. If the response indicates success and contains a valid result, the
   * cJSON object associated with the result is deleted.
   *
   * @param response Pointer to the atop_base_response_t structure to be freed.
   */
  void atop_base_response_free(const pal_t *pal, atop_base_response_t *response)
  {
      if (response->success == true && response->result) {
          cJSON_Delete(response->result);
      }
  }
