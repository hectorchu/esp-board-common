set(SRC_FILES
    "board_init.c"
    "board_i2c.c"
    "board_backlight.c"
    "board_pmic.cpp"
    "board_display_axs15231b.c"
    "board_display_st7796.c"
    "board_display_st7789.c"
    "board_touch_axs15231b.c"
    "board_touch_ft6336.c"
    "board_touch_cst816d.c"
    "board_camera.c"
    "board_sdcard.c"
    "board_audio.c"
    "board_rtc.c"
    "board_imu.c"
)

set(REQUIRES
    "esp_lcd" "driver" "esp_adc" "esp_lvgl_port" "lvgl"
    "espressif__esp_lcd_axs15231b" "espressif__esp_io_expander_tca9554"
    "espressif__esp_lcd_st7796"
    "espressif__esp_lcd_touch_ft5x06" "espressif__esp_lcd_touch_cst816s"
    "XPowersLib"
    "fatfs" "nvs_flash"
)

# Conditionally add camera and audio codec dependencies
if(TARGET __idf_esp32-camera)
    list(APPEND REQUIRES "esp32-camera")
endif()
if(TARGET __idf_espressif__esp_codec_dev)
    list(APPEND REQUIRES "espressif__esp_codec_dev")
endif()

idf_component_register(
    SRCS ${SRC_FILES}
    INCLUDE_DIRS "."
    REQUIRES ${REQUIRES}
)

# Board config header: selected at build time via BOARD variable.
# Consumer project sets BOARD_CONFIG_DIR to the directory containing board_config.h.
if(DEFINED BOARD_CONFIG_DIR AND EXISTS "${BOARD_CONFIG_DIR}")
    target_include_directories(${COMPONENT_LIB} PRIVATE "${BOARD_CONFIG_DIR}")
elseif(DEFINED BOARD AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/boards/${BOARD}")
    target_include_directories(${COMPONENT_LIB} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/boards/${BOARD}"
    )
else()
    message(WARNING "board_common: No board config directory found. "
                    "Set BOARD_CONFIG_DIR or BOARD CMake variable.")
endif()
