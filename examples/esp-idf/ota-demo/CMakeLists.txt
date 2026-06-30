cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/../components"
)

set(PROJECT_VER "1.1.0")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(ota_demo)
