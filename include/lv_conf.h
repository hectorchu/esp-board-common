#ifndef LV_CONF_H
#define LV_CONF_H

#include <stddef.h>

#define LV_MEM_POOL_ALLOC lvgl_psram_alloc

void *lvgl_psram_alloc(size_t size);

#endif
