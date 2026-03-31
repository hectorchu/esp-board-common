# The app uses the unified board_camera API from esp-board-common.
# The DVP camera component is only needed on non-P4 targets (S3, etc.).
# PPA (Pixel Processing Accelerator) is used on P4 for hardware-accelerated scaling.
set(EXTRA_REQS "")
if(NOT IDF_TARGET STREQUAL "esp32p4")
    list(APPEND EXTRA_REQS "espressif__esp32-camera")
endif()
if(IDF_TARGET STREQUAL "esp32p4")
    list(APPEND EXTRA_REQS "esp_driver_ppa")
endif()

idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES "esp-board-common" "esp_lvgl_port" ${EXTRA_REQS}
)
