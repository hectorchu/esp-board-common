# Touch test app — minimal board init + LVGL touch verification.
idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES "esp-board-common" "espressif__esp_lvgl_adapter" "espressif__esp_lcd_touch"
)
