idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES "esp-board-common" "esp_lvgl_port" "espressif__esp32-camera"
)
