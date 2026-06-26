set(AK_DIR ${CMAKE_CURRENT_LIST_DIR}/../../../..)

set(AK_SRCS
    # rtc-tcp-client (TAI 2.1)
    ${AK_DIR}/modules/rtc-tcp-client/src/tai_attrs.c
    ${AK_DIR}/modules/rtc-tcp-client/src/tai_client.c
    ${AK_DIR}/modules/rtc-tcp-client/src/tai_crypto.c
    ${AK_DIR}/modules/rtc-tcp-client/src/tai_packet.c
    ${AK_DIR}/modules/rtc-tcp-client/src/tai_pkt_log.c
    ${AK_DIR}/modules/rtc-tcp-client/src/tai_protocol.c
    ${AK_DIR}/modules/rtc-tcp-client/src/tai_transport.c

    # iot-client
    ${AK_DIR}/modules/iot-client/src/atop.c
    ${AK_DIR}/modules/iot-client/src/atop_base.c
    ${AK_DIR}/modules/iot-client/src/cipher_wrapper.c
    ${AK_DIR}/modules/iot-client/src/http_client_interface.c
    ${AK_DIR}/modules/iot-client/src/iot_client.c
    ${AK_DIR}/modules/iot-client/src/iot_client_message.c
    ${AK_DIR}/modules/iot-client/src/iot_dns.c
    ${AK_DIR}/modules/iot-client/src/iot_dp.c
    ${AK_DIR}/modules/iot-client/src/iot_on_boarding.c
    ${AK_DIR}/modules/iot-client/src/mqtt.c

    # tuya-ble
    ${AK_DIR}/modules/tuya-ble/src/tuya_ble_prov.c

    # PAL (FreeRTOS+lwIP) and shared common transport / log / RNG
    ${AK_DIR}/pal/pal_freertos.c
    ${AK_DIR}/common/tls.c
    ${AK_DIR}/common/log.c
    ${AK_DIR}/common/rng.c

    # vendored coreHTTP
    ${AK_DIR}/third_party/coreHTTP/source/core_http_client.c
    ${AK_DIR}/third_party/coreHTTP/source/dependency/3rdparty/llhttp/src/api.c
    ${AK_DIR}/third_party/coreHTTP/source/dependency/3rdparty/llhttp/src/http.c
    ${AK_DIR}/third_party/coreHTTP/source/dependency/3rdparty/llhttp/src/llhttp.c

    # vendored coreMQTT
    ${AK_DIR}/third_party/coreMQTT/source/core_mqtt.c
    ${AK_DIR}/third_party/coreMQTT/source/core_mqtt_serializer.c
    ${AK_DIR}/third_party/coreMQTT/source/core_mqtt_state.c
)

idf_component_register(
    SRCS ${AK_SRCS}
    INCLUDE_DIRS
        ${AK_DIR}/modules/rtc-tcp-client/include
        ${AK_DIR}/modules/iot-client/include
        ${AK_DIR}/modules/tuya-ble/include
        ${AK_DIR}/pal
        ${AK_DIR}/common
    PRIV_INCLUDE_DIRS
        ${AK_DIR}/modules/rtc-tcp-client/src
        ${AK_DIR}/modules/iot-client/src
        ${AK_DIR}/third_party/coreHTTP/source/include
        ${AK_DIR}/third_party/coreHTTP/source/interface
        ${AK_DIR}/third_party/coreHTTP/source/dependency/3rdparty/llhttp/include
        ${AK_DIR}/third_party/coreMQTT/source/include
        ${AK_DIR}/third_party/coreMQTT/source/interface
        ${AK_DIR}/third_party/cJSON
    REQUIRES mbedtls json freertos lwip esp_event esp_netif esp_timer esp_wifi
)

target_compile_definitions(${COMPONENT_LIB} PRIVATE
    HTTP_DO_NOT_USE_CUSTOM_CONFIG
    MQTT_DO_NOT_USE_CUSTOM_CONFIG
    IOT_DO_NOT_USE_CUSTOM_CONFIG
)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wno-error=format
)
