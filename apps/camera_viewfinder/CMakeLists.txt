cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED BOARD)
    set(BOARD "waveshare_s3_lcd35b")
endif()
message(STATUS "Building for board: ${BOARD}")

# Reference esp-board-common as a direct component directory (two levels up).
# ESP-IDF detects the idf_component_register() in its CMakeLists.txt and
# registers it using the directory basename as the component name.
set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_SOURCE_DIR}/../.."
    "${CMAKE_CURRENT_SOURCE_DIR}/../../components/esp-camera-pipeline"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../components/esp-camera-pipeline/components"
)
# Board-specific sdkconfig lives alongside board_config.h in the component.
set(BOARD_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../boards/${BOARD}")
set(SDKCONFIG_DEFAULTS "sdkconfig.defaults;${BOARD_DIR}/sdkconfig.defaults")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(camera_viewfinder)
