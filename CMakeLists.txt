set(SRC_FILES
    "src/board_init.c"
    "src/board_i2c.c"
    "src/board_backlight.c"
    "src/board_pmic.cpp"
    "src/board_display_axs15231b.c"
    "src/board_display_st7796.c"
    "src/board_display_st7789.c"
    "src/board_touch_axs15231b.c"
    "src/board_touch_ft6336.c"
    "src/board_touch_cst816d.c"
    "src/board_camera.c"
    "src/board_sdcard.c"
    "src/board_audio.c"
)

# All dependencies listed unconditionally. The #if BOARD_HAS_* guards in C source
# compile out unused peripheral code when the board config doesn't define them.
# The components just need to be present for include path resolution.
idf_component_register(
    SRCS ${SRC_FILES}
    INCLUDE_DIRS "src"
    REQUIRES "esp_lcd" "driver" "esp_adc" "esp_lvgl_port" "lvgl"
             "espressif__esp_lcd_axs15231b" "espressif__esp_io_expander_tca9554"
             "espressif__esp_lcd_st7796"
             "espressif__esp_lcd_touch_ft5x06" "espressif__esp_lcd_touch_cst816s"
             "XPowersLib"
             "fatfs" "nvs_flash"
             "espressif__esp32-camera" "espressif__esp_codec_dev"
             "waveshare__pcf85063a" "waveshare__qmi8658"
)

# Board config header: selected at build time via BOARD variable.
# Consumer project sets BOARD_CONFIG_DIR to the directory containing board_config.h.
if(DEFINED BOARD_CONFIG_DIR AND EXISTS "${BOARD_CONFIG_DIR}")
    target_include_directories(${COMPONENT_LIB} PUBLIC "${BOARD_CONFIG_DIR}")
elseif(DEFINED BOARD AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/boards/${BOARD}")
    target_include_directories(${COMPONENT_LIB} PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/boards/${BOARD}"
    )
else()
    message(WARNING "board_common: No board config directory found. "
                    "Set BOARD_CONFIG_DIR or BOARD CMake variable.")
endif()
