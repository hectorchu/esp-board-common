#include "board.h"
#include "board_config.h"

#if BOARD_HAS_SDCARD

#include "board_sdcard.h"

#include <sys/unistd.h>
#include <sys/stat.h>
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdmmc_host.h"
#include "nvs_flash.h"
#include "esp_log.h"

static const char *TAG = "board_sdcard";
static sdmmc_card_t *card = NULL;

uint64_t board_sdcard_get_size(void)
{
    if (card == NULL) return 0;
    return ((uint64_t)card->csd.capacity) * card->csd.sector_size;
}

void board_sdcard_init(void)
{
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024,
    };
    const char mount_point[] = "/sdcard";
    ESP_LOGI(TAG, "Initializing SD card");

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    sdmmc_slot_config_t slot_config = {};
    slot_config.cd = SDMMC_SLOT_NO_CD;
    slot_config.wp = SDMMC_SLOT_NO_WP;
    slot_config.width = 1;
    slot_config.flags = SDMMC_SLOT_FLAG_INTERNAL_PULLUP;
    slot_config.clk = BOARD_PIN_SD_CLK;
    slot_config.cmd = BOARD_PIN_SD_CMD;
    slot_config.d0 = BOARD_PIN_SD_D0;

    ESP_LOGI(TAG, "Mounting filesystem");
    esp_err_t ret = esp_vfs_fat_sdmmc_mount(mount_point, &host, &slot_config, &mount_config, &card);

    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount filesystem.");
        } else {
            ESP_LOGE(TAG, "Failed to initialize the card (%s).", esp_err_to_name(ret));
        }
        return;
    }
    ESP_LOGI(TAG, "Filesystem mounted");
    sdmmc_card_print_info(stdout, card);
}

#endif /* BOARD_HAS_SDCARD */
