cmake_minimum_required(VERSION 3.20)

set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

project(agentic_kit C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

option(AGENTIC_KIT_BUILD_TESTS "Build tests" ON)
option(AGENTIC_KIT_BUILD_EXAMPLES "Build posix examples" OFF)

include(CTest)
include(GNUInstallDirs)

find_package(Threads REQUIRED)
find_package(Python3 COMPONENTS Interpreter REQUIRED)

set(AGENTIC_KIT_ENABLE_PROJECT_TESTS ${AGENTIC_KIT_BUILD_TESTS})
set(BUILD_TESTING OFF CACHE BOOL "Disable third-party tests" FORCE)
set(ENABLE_TESTING OFF CACHE BOOL "Disable mbedTLS tests" FORCE)
set(ENABLE_PROGRAMS OFF CACHE BOOL "Disable mbedTLS programs" FORCE)
set(ENABLE_TESTING OFF CACHE BOOL "Disable cJSON tests" FORCE)
set(ENABLE_CJSON_TEST OFF CACHE BOOL "Disable cJSON tests" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build static libraries" FORCE)
# Don't let the vendored mbedTLS treat its own compiler warnings as errors:
# its warning flags (e.g. -Werror=unterminated-string-initialization) are not
# portable across compilers and break the build on some AppleClang versions.
set(MBEDTLS_FATAL_WARNINGS OFF CACHE BOOL "Disable mbedTLS -Werror" FORCE)
# Tolerate unknown -W options so a compiler that doesn't recognise a flag warns
# instead of erroring (older/newer AppleClang differ on which warnings exist).
add_compile_options($<$<COMPILE_LANG_AND_ID:C,AppleClang>:-Wno-unknown-warning-option>)

function(agentic_kit_require_dependency name)
    string(TOLOWER "${name}" name_lc)
    set(dep_dir "${CMAKE_CURRENT_SOURCE_DIR}/third_party/${name}")
    if(EXISTS "${dep_dir}/CMakeLists.txt" OR EXISTS "${dep_dir}/source")
        set(${name_lc}_source_dir "${dep_dir}" PARENT_SCOPE)
    else()
        message(FATAL_ERROR "Missing dependency third_party/${name}. Run git submodule update --init --recursive from the repository root.")
    endif()
endfunction()

agentic_kit_require_dependency(mbedtls)
agentic_kit_require_dependency(cJSON)
agentic_kit_require_dependency(coreHTTP)
agentic_kit_require_dependency(coreMQTT)

if(NOT TARGET MbedTLS::mbedtls)
    add_subdirectory("${mbedtls_source_dir}" "${CMAKE_BINARY_DIR}/third_party/mbedtls" EXCLUDE_FROM_ALL)
endif()

if(NOT TARGET cjson AND NOT TARGET cJSON)
    add_subdirectory("${cjson_source_dir}" "${CMAKE_BINARY_DIR}/third_party/cJSON" EXCLUDE_FROM_ALL)
endif()

if(TARGET cjson)
    set(AGENTIC_KIT_CJSON_TARGET cjson)
elseif(TARGET cJSON)
    set(AGENTIC_KIT_CJSON_TARGET cJSON)
else()
    message(FATAL_ERROR "cJSON target was not created")
endif()

if(TARGET MbedTLS::mbedtls)
    set(AGENTIC_KIT_MBEDTLS_TARGETS MbedTLS::mbedtls MbedTLS::mbedx509 MbedTLS::mbedcrypto)
else()
    set(AGENTIC_KIT_MBEDTLS_TARGETS mbedtls mbedx509 mbedcrypto)
endif()

add_library(core_http STATIC
    "${corehttp_source_dir}/source/core_http_client.c"
    "${corehttp_source_dir}/source/dependency/3rdparty/llhttp/src/api.c"
    "${corehttp_source_dir}/source/dependency/3rdparty/llhttp/src/http.c"
    "${corehttp_source_dir}/source/dependency/3rdparty/llhttp/src/llhttp.c"
)
target_compile_definitions(core_http PUBLIC HTTP_DO_NOT_USE_CUSTOM_CONFIG)

target_include_directories(core_http PUBLIC
    "${corehttp_source_dir}/source/include"
    "${corehttp_source_dir}/source/interface"
    "${corehttp_source_dir}/source/dependency/3rdparty/llhttp/include"
)

add_library(core_mqtt STATIC
    "${coremqtt_source_dir}/source/core_mqtt.c"
    "${coremqtt_source_dir}/source/core_mqtt_serializer.c"
    "${coremqtt_source_dir}/source/core_mqtt_state.c"
)
target_compile_definitions(core_mqtt PUBLIC MQTT_DO_NOT_USE_CUSTOM_CONFIG)

target_include_directories(core_mqtt PUBLIC
    "${coremqtt_source_dir}/source/include"
    "${coremqtt_source_dir}/source/interface"
)

# -----------------------------------------------------------------------------
# Common library (process-wide log facade, shared TLS transport, etc.)
# -----------------------------------------------------------------------------
set(COMMON_DIR "${CMAKE_CURRENT_SOURCE_DIR}/common")

add_library(agentic_kit_common STATIC
    "${COMMON_DIR}/log.c"
    "${COMMON_DIR}/tls.c"
)
target_include_directories(agentic_kit_common PUBLIC
    "${COMMON_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/pal"   # tls.c / tls.h need pal.h
)
target_link_libraries(agentic_kit_common PUBLIC ${AGENTIC_KIT_MBEDTLS_TARGETS})

# -----------------------------------------------------------------------------
# PAL (Platform Abstraction Layer)
# -----------------------------------------------------------------------------
set(PAL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/pal")

add_library(agentic_kit_pal STATIC
    "${PAL_DIR}/pal_posix.c"
)
target_include_directories(agentic_kit_pal PUBLIC "${PAL_DIR}")
target_link_libraries(agentic_kit_pal PUBLIC agentic_kit_common Threads::Threads)

# -----------------------------------------------------------------------------
# rtc-tcp-client module (Tuya AI Foundation 2.1)
# -----------------------------------------------------------------------------
set(AI_TCP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/modules/rtc-tcp-client")

set(AI_TCP_SOURCES
    "${AI_TCP_DIR}/src/tai_attrs.c"
    "${AI_TCP_DIR}/src/tai_client.c"
    "${AI_TCP_DIR}/src/tai_crypto.c"
    "${AI_TCP_DIR}/src/tai_packet.c"
    "${AI_TCP_DIR}/src/tai_pkt_log.c"
    "${AI_TCP_DIR}/src/tai_protocol.c"
    "${AI_TCP_DIR}/src/tai_transport.c"
)

add_library(tuya_ai_tcp STATIC ${AI_TCP_SOURCES})
target_include_directories(tuya_ai_tcp PUBLIC
    "${AI_TCP_DIR}/include"
    "${PAL_DIR}"
    "${COMMON_DIR}"
)
target_link_libraries(tuya_ai_tcp PUBLIC agentic_kit_pal agentic_kit_common Threads::Threads)

# -----------------------------------------------------------------------------
# iot-client module
# -----------------------------------------------------------------------------
set(IOT_CLIENT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/modules/iot-client")

set(IOT_CLIENT_SOURCES
    "${IOT_CLIENT_DIR}/src/atop.c"
    "${IOT_CLIENT_DIR}/src/atop_base.c"
    "${IOT_CLIENT_DIR}/src/cipher_wrapper.c"
    "${IOT_CLIENT_DIR}/src/http_client_interface.c"
    "${IOT_CLIENT_DIR}/src/iot_client.c"
    "${IOT_CLIENT_DIR}/src/iot_client_message.c"
    "${IOT_CLIENT_DIR}/src/iot_dns.c"
    "${IOT_CLIENT_DIR}/src/iot_dp.c"
    "${IOT_CLIENT_DIR}/src/iot_on_boarding.c"
    "${IOT_CLIENT_DIR}/src/iot_pal_defaults.c"
    "${IOT_CLIENT_DIR}/src/mqtt.c"
)

add_library(tuya_iot_client STATIC ${IOT_CLIENT_SOURCES})
target_include_directories(tuya_iot_client PUBLIC
    "${IOT_CLIENT_DIR}/include"
    "${IOT_CLIENT_DIR}/src"
    "${cjson_source_dir}"
    "${PAL_DIR}"
    "${COMMON_DIR}"
)
target_link_libraries(tuya_iot_client PUBLIC
    ${AGENTIC_KIT_CJSON_TARGET}
    agentic_kit_pal
    agentic_kit_common          # provides mbedTLS transitively (shared TLS lives here)
    core_http
    core_mqtt
    Threads::Threads
)

# -----------------------------------------------------------------------------
# rtc-client module (prebuilt binary)
# -----------------------------------------------------------------------------
set(STEAM_CLIENT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/modules/rtc-client")

if(APPLE AND CMAKE_SYSTEM_PROCESSOR STREQUAL "arm64")
    set(STEAM_CLIENT_LIB_DIR "${STEAM_CLIENT_DIR}/libs/macos_arm64")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|amd64")
    set(STEAM_CLIENT_LIB_DIR "${STEAM_CLIENT_DIR}/libs/linux-gnu-amd64")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
    set(STEAM_CLIENT_LIB_DIR "${STEAM_CLIENT_DIR}/libs/linux-gnu-aarch64")
else()
    set(STEAM_CLIENT_LIB_DIR "")
endif()

if(STEAM_CLIENT_LIB_DIR AND EXISTS "${STEAM_CLIENT_LIB_DIR}/libstm.a")
    add_library(tuya_steam_client STATIC IMPORTED GLOBAL)
    set_target_properties(tuya_steam_client PROPERTIES
        IMPORTED_LOCATION "${STEAM_CLIENT_LIB_DIR}/libstm.a"
        INTERFACE_INCLUDE_DIRECTORIES "${STEAM_CLIENT_DIR}/include"
    )
endif()

# -----------------------------------------------------------------------------
# tuya-ble module
# -----------------------------------------------------------------------------
set(TUYA_BLE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/modules/tuya-ble")

add_library(tuya_ble STATIC
    "${TUYA_BLE_DIR}/src/tuya_ble_prov.c"
)
target_include_directories(tuya_ble PUBLIC
    "${TUYA_BLE_DIR}/include"
    "${cjson_source_dir}"
)
target_link_libraries(tuya_ble PUBLIC
    ${AGENTIC_KIT_CJSON_TARGET}
    ${AGENTIC_KIT_MBEDTLS_TARGETS}
)

# -----------------------------------------------------------------------------
# Tests
# -----------------------------------------------------------------------------
if(AGENTIC_KIT_BUILD_TESTS AND AGENTIC_KIT_ENABLE_PROJECT_TESTS)
    set(AI_TCP_TESTS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/modules/rtc-tcp-client/test")
    set(IOT_CLIENT_TESTS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/modules/iot-client/test")
    set(TUYA_BLE_TESTS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/modules/tuya-ble/test")

    add_executable(tai_unit_tests
        ${AI_TCP_SOURCES}
        "${AI_TCP_TESTS_DIR}/test_core.c"
    )
    target_include_directories(tai_unit_tests PRIVATE
        "${AI_TCP_DIR}/include"
        "${AI_TCP_DIR}/src"
        "${PAL_DIR}"
    )
    target_link_libraries(tai_unit_tests PRIVATE
        agentic_kit_pal          # pulls agentic_kit_common -> mbedTLS transitively
        Threads::Threads
    )
    add_test(NAME tai_unit_tests COMMAND tai_unit_tests)

    add_executable(tai_integration_tests
        ${AI_TCP_SOURCES}
        "${AI_TCP_TESTS_DIR}/tai_pal_loopback.c"
        "${AI_TCP_TESTS_DIR}/test_integration.c"
    )
    target_include_directories(tai_integration_tests PRIVATE
        "${AI_TCP_DIR}/include"
        "${AI_TCP_DIR}/src"
        "${AI_TCP_TESTS_DIR}"
        "${PAL_DIR}"
    )
    target_link_libraries(tai_integration_tests PRIVATE
        agentic_kit_pal          # pulls agentic_kit_common -> mbedTLS transitively
        Threads::Threads
    )
    add_test(NAME tai_integration_tests COMMAND tai_integration_tests)

    function(agentic_kit_add_iot_test target source)
        add_executable(${target} "${source}")
        target_link_libraries(${target} PRIVATE tuya_iot_client)
        target_compile_definitions(${target} PRIVATE
            PYTHON3_EXEC="${Python3_EXECUTABLE}"
            TEST_CONFIG_DIR="${IOT_CLIENT_TESTS_DIR}/config"
            DNS_MOCK_PATH="${IOT_CLIENT_TESTS_DIR}/mock/dns_mock.py"
            MOCK_SCRIPT_PATH="${IOT_CLIENT_TESTS_DIR}/mock/atop_mock.py"
            ATOP_MOCK_PATH="${IOT_CLIENT_TESTS_DIR}/mock/atop_mock.py"
            MQTT_MOCK_SCRIPT_PATH="${IOT_CLIENT_TESTS_DIR}/mock/mqtt_test_mock.py"
            ONBOARDING_MQTT_MOCK_PATH="${IOT_CLIENT_TESTS_DIR}/mock/on_boarding_mock.py"
            MESSAGE_MOCK_PATH="${IOT_CLIENT_TESTS_DIR}/mock/message_mock.py"
        )
        add_test(NAME ${target} COMMAND ${target})
    endfunction()

    agentic_kit_add_iot_test(iot_cipher_test "${IOT_CLIENT_TESTS_DIR}/cipher_test.c")
    agentic_kit_add_iot_test(iot_dns_test "${IOT_CLIENT_TESTS_DIR}/dns_test.c")
    agentic_kit_add_iot_test(iot_atop_test "${IOT_CLIENT_TESTS_DIR}/atop_test.c")
    agentic_kit_add_iot_test(iot_mqtt_test "${IOT_CLIENT_TESTS_DIR}/mqtt_test.c")
    agentic_kit_add_iot_test(iot_message_test "${IOT_CLIENT_TESTS_DIR}/iot_client_message_test.c")
    agentic_kit_add_iot_test(iot_on_boarding_test "${IOT_CLIENT_TESTS_DIR}/on_boarding_test.c")
    agentic_kit_add_iot_test(iot_dp_test "${IOT_CLIENT_TESTS_DIR}/iot_dp_test.c")

    add_executable(tuya_ble_test
        "${TUYA_BLE_TESTS_DIR}/test_prov.c"
        "${TUYA_BLE_TESTS_DIR}/tuya_ble_hal_host.c"
    )
    target_link_libraries(tuya_ble_test PRIVATE tuya_ble)
    add_test(NAME tuya_ble_test COMMAND tuya_ble_test)
endif()

# -----------------------------------------------------------------------------
# Examples (standalone build recommended: cmake -S examples/posix -B build)
# -----------------------------------------------------------------------------
if(AGENTIC_KIT_BUILD_EXAMPLES)
    add_subdirectory(examples/posix)
endif()


