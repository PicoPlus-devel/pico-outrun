/*
 * The decoded graphics regions, as C++ pointers for the vendored engine.
 *
 * port/outrun_data.h is the C interface to the whole data image; this is the
 * thin C++ face of it that video.cpp uses under OUTRUN_GFX_IN_FLASH, so that the
 * vendored file needs one include and three calls rather than knowledge of the
 * container format.
 *
 * The regions may be in flash (the data .uf2, the normal case) or in PSRAM
 * (built at boot from the romset on the SD card). Nothing here or in the engine
 * cares which: both are ordinary dereferenceable pointers. The macro is still
 * called OUTRUN_GFX_IN_FLASH for historical reasons - renaming it would touch
 * eight vendored files for no gain.
 *
 * All three return NULL when no image is in place. Callers should have checked
 * outrun_data_valid() long before reaching video.cpp.
 */

#pragma once

#include "outrun_data.h"

static inline const uint32_t *outrun_gfx_tiles(void)
{
    return (const uint32_t *)outrun_data_region(OUTRUN_REGION_TILES, 0);
}

static inline const uint32_t *outrun_gfx_sprites(void)
{
    return (const uint32_t *)outrun_data_region(OUTRUN_REGION_SPRITES, 0);
}

static inline const uint8_t *outrun_gfx_road(void)
{
    return outrun_data_region(OUTRUN_REGION_ROAD, 0);
}
