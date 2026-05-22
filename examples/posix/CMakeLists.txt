cmake_minimum_required(VERSION 3.20)
project(agentic_kit_posix_examples C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

# -- Import agentic-kit library (standalone mode) ----------------------------
# When built as a subdirectory of the root project, targets already exist.
# When built standalone (cmake -S examples/posix -B build), pull them in.
if(NOT TARGET tuya_iot_client)
    set(AGENTIC_KIT_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(AGENTIC_KIT_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../.." "${CMAKE_BINARY_DIR}/_agentic_kit" EXCLUDE_FROM_ALL)
endif()

# -- Example-only third-party libraries (fetched at configure time) ----------
include(FetchContent)

FetchContent_Declare(
    qrcodegen
    GIT_REPOSITORY https://github.com/nayuki/QR-Code-generator.git
    GIT_TAG        master
    SOURCE_SUBDIR  _no_cmake
)
FetchContent_Declare(
    quirc
    GIT_REPOSITORY https://github.com/dlbeer/quirc.git
    GIT_TAG        master
    SOURCE_SUBDIR  _no_cmake
)
FetchContent_Declare(
    stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG        master
    SOURCE_SUBDIR  _no_cmake
)
FetchContent_MakeAvailable(qrcodegen quirc stb)

add_library(qrcodegen STATIC "${qrcodegen_SOURCE_DIR}/c/qrcodegen.c")
target_include_directories(qrcodegen PUBLIC "${qrcodegen_SOURCE_DIR}/c")

add_library(stb_image INTERFACE)
target_include_directories(stb_image INTERFACE "${stb_SOURCE_DIR}")

add_library(quirc STATIC
    "${quirc_SOURCE_DIR}/lib/decode.c"
    "${quirc_SOURCE_DIR}/lib/identify.c"
    "${quirc_SOURCE_DIR}/lib/quirc.c"
    "${quirc_SOURCE_DIR}/lib/version_db.c"
)
target_include_directories(quirc PUBLIC "${quirc_SOURCE_DIR}/lib")

# -- pair / api-activate -----------------------------------------------------
add_executable(activate_demo
    "${CMAKE_CURRENT_SOURCE_DIR}/pair/api-activate/activate_demo.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/pair/api-activate/activate_demo_main.c"
)
target_include_directories(activate_demo PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/pair/api-activate")
target_link_libraries(activate_demo PRIVATE tuya_iot_client)

add_custom_command(TARGET activate_demo POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/pair/api-activate/tuya_openapi.py"
        "$<TARGET_FILE_DIR:activate_demo>/tuya_openapi.py"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/pair/api-activate/activate-demo.sh"
        "$<TARGET_FILE_DIR:activate_demo>/activate-demo.sh"
)

# -- pair / scan-by-app ------------------------------------------------------
add_executable(scan_by_app_pair_demo
    "${CMAKE_CURRENT_SOURCE_DIR}/pair/scan-by-app/scan_by_app_pair_demo.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/pair/scan-by-app/scan_by_app_pair_demo_main.c"
)
target_include_directories(scan_by_app_pair_demo PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/pair/scan-by-app")
target_link_libraries(scan_by_app_pair_demo PRIVATE tuya_iot_client qrcodegen)

# -- pair / scan-by-device ---------------------------------------------------
add_executable(scan_by_device_pair_demo
    "${CMAKE_CURRENT_SOURCE_DIR}/pair/scan-by-device/scan_by_device_pair_demo.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/pair/scan-by-device/scan_by_device_pair_demo_main.c"
)
target_include_directories(scan_by_device_pair_demo PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/pair/scan-by-device")
target_link_libraries(scan_by_device_pair_demo PRIVATE tuya_iot_client stb_image quirc)

# -- ai / rtc-client (prebuilt lib, optional) --------------------------------
if(TARGET tuya_steam_client)
    add_executable(chat_demo
        "${CMAKE_CURRENT_SOURCE_DIR}/ai/rtc-client/chat_demo.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/ai/rtc-client/chat_demo_main.c"
    )
    target_include_directories(chat_demo PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/ai/rtc-client")
    target_link_libraries(chat_demo PRIVATE tuya_iot_client tuya_steam_client)

    add_executable(edu_camera_demo
        "${CMAKE_CURRENT_SOURCE_DIR}/ai/rtc-client/edu_camera_demo.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/ai/rtc-client/edu_camera_demo_main.c"
    )
    target_include_directories(edu_camera_demo PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/ai/rtc-client")
    target_link_libraries(edu_camera_demo PRIVATE tuya_iot_client tuya_steam_client)
else()
    message(STATUS "rtc-client prebuilt lib not available; skipping chat_demo and edu_camera_demo")
endif()

# -- ai / rtc-tcp-client ----------------------------------------------------
add_executable(tai_text_query_demo "${CMAKE_CURRENT_SOURCE_DIR}/ai/rtc-tcp-client/text_query_demo.c")
target_link_libraries(tai_text_query_demo PRIVATE tuya_ai_tcp tuya_iot_client agentic_kit_pal)

add_executable(tai_edu_threaded_chat_demo "${CMAKE_CURRENT_SOURCE_DIR}/ai/rtc-tcp-client/edu_threaded_chat_demo.c")
target_link_libraries(tai_edu_threaded_chat_demo PRIVATE tuya_ai_tcp tuya_iot_client agentic_kit_pal)

add_executable(tai_edu_camera_demo "${CMAKE_CURRENT_SOURCE_DIR}/ai/rtc-tcp-client/edu_camera_demo.c")
target_link_libraries(tai_edu_camera_demo PRIVATE tuya_ai_tcp tuya_iot_client agentic_kit_pal)

find_library(OPUS_LIBRARY opus)
find_path(OPUS_INCLUDE_DIR opus/opus.h)
if(OPUS_LIBRARY AND OPUS_INCLUDE_DIR)
    add_executable(tai_opus_chat_demo "${CMAKE_CURRENT_SOURCE_DIR}/ai/rtc-tcp-client/opus_chat_demo.c")
    target_include_directories(tai_opus_chat_demo PRIVATE "${OPUS_INCLUDE_DIR}")
    target_link_libraries(tai_opus_chat_demo PRIVATE tuya_ai_tcp tuya_iot_client agentic_kit_pal "${OPUS_LIBRARY}")
else()
    message(STATUS "libopus not found; skipping tai_opus_chat_demo")
endif()
