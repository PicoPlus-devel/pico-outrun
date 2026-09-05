/*
 * The decoded graphics regions, as C++ pointers for the vendored engine.
 *
 * port/outrun_data.h is the C interface to the whole flash image; this is the
 * thin C++ face of it that video.cpp uses under OUTRUN_GFX_IN_FLASH, so that
 * the vendored file needs one include and three calls rather than knowledge of
 * the container format.
 *
 * All three return NULL when no data image has been flashed. Callers should
 * have checked outrun_data_valid() long before reaching video.cpp.
 */

#pragma once

#include "outrun_data.h"

static inline const uint32_t *outrun_flash_tiles(void)
{
    return (const uint32_t *)outrun_data_region(OUTRUN_REGION_TILES, 0);
}

static inline const uint32_t *outrun_flash_sprites(void)
{
    return (const uint32_t *)outrun_data_region(OUTRUN_REGION_SPRITES, 0);
}

static inline const uint8_t *outrun_flash_road(void)
{
    return outrun_data_region(OUTRUN_REGION_ROAD, 0);
}
