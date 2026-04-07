/**
 * Shared RGB565 utility functions for display overlays and pipeline code.
 *
 * Header-only: all functions are static inline to avoid link-time duplication
 * issues across translation units.
 */
#pragma once

#include <stdint.h>
#include <stddef.h>

/** Pack 8-bit RGB into RGB565. */
static inline uint16_t rgb565_pack(uint8_t r, uint8_t g, uint8_t b)
{
    return ((uint16_t)(r >> 3) << 11) | ((uint16_t)(g >> 2) << 5) | (b >> 3);
}

/** Alpha-blend an RGB565 color against black (pre-multiplied). */
static inline uint16_t alpha_blend_rgb565(uint16_t color, uint8_t alpha)
{
    if (alpha == 0) return 0;
    if (alpha >= 248) return color;

    uint32_t r = ((color >> 11) & 0x1F) * alpha;
    uint32_t g = ((color >> 5)  & 0x3F) * alpha;
    uint32_t b = ( color        & 0x1F) * alpha;

    uint16_t out = (uint16_t)(((r >> 8) << 11) | ((g >> 8) << 5) | (b >> 8));
    return out ? out : 1;
}

/** Byte-swap RGB565 pixels (host ↔ SPI panel endianness). */
static inline void copy_swap_u16(uint16_t *dst, const uint16_t *src, size_t count)
{
    while (count--) {
        uint16_t p = *src++;
        *dst++ = (uint16_t)((p << 8) | (p >> 8));
    }
}
