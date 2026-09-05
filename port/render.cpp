/*
 * The picoOutRun renderer: one pass from the engine's palette-index buffer into
 * the framebuffer that pico_shared already owns.
 *
 * See port/sdl2/rendersurface.hpp for why the index buffer has to exist at all
 * (short version: hwsprites reads pixels back to do shadows).
 */

#include "sdl2/rendersurface.hpp"

#include <cmath>
#include <cstring>

#include "FrensHelpers.h"

// OutRun is 320x224 inside pico_shared's 320x240 framebuffer: 8 blank lines top
// and bottom.
#define OUTRUN_YOFFSET ((SCREENHEIGHT - 224) / 2)

static inline uint16_t *fb_line(int line)
{
#if HSTX
    return hstx_getlineFromFramebuffer(line);
#else
    return &Frens::framebuffer[line * SCREENWIDTH];
#endif
}

/* RenderBase::convert_palette(), transcribed from
 * cannonball/src/main/sdl2/renderbase.cpp. r1/g1/b1 are 5-bit System 16
 * components; upstream widens them to 8 bits (x8) and packs for the SDL surface
 * format. Here they are packed straight into the board's native 16-bit format,
 * which is why the table is uint16_t rather than uint32_t.
 *
 * The shadow copy at adr + S16_PALETTE_ENTRIES is not optional: hwsprites
 * writes that index for shadowed pixels. */
void RenderBase::convert_palette(uint32_t adr, uint32_t r1, uint32_t g1, uint32_t b1)
{
    adr >>= 1;

    uint32_t r = r1 * 8;
    uint32_t g = g1 * 8;
    uint32_t b = b1 * 8;

#if HSTX // RGB555: 0RRRRRGG GGGBBBBB
#define PACK_RGB() (uint16_t)(((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3))
#else // RGB444: 0000RRRR GGGGBBBB
#define PACK_RGB() (uint16_t)(((r >> 4) << 8) | ((g >> 4) << 4) | (b >> 4))
#endif

    rgb[adr] = PACK_RGB();

    // Shadow colours live in the upper half of the table
    r = r1 * shadow_multi / 31;
    g = g1 * shadow_multi / 31;
    b = b1 * shadow_multi / 31;

    rgb[adr + S16_PALETTE_ENTRIES] = PACK_RGB();
}

void RenderBase::set_shadow_intensity(float f)
{
    shadow_multi = (int)std::round(255.0f * f);
}

bool Render::init(int src_width, int src_height, int /*scale*/, int /*video_mode*/,
                  int /*scanlines*/)
{
    this->src_width = src_width;
    this->src_height = src_height;

    // Blank the letterbox once; draw_frame() never touches those lines again.
    for (int y = 0; y < SCREENHEIGHT; y++)
    {
        if (y < OUTRUN_YOFFSET || y >= OUTRUN_YOFFSET + src_height)
        {
            memset(fb_line(y), 0, SCREENWIDTH * sizeof(uint16_t));
        }
    }
    return true;
}

void Render::disable()
{
}

bool Render::start_frame()
{
    return true;
}

bool Render::finalize_frame()
{
    return true;
}

/* The hot loop: 320x224 palette lookups per frame. Kept in SRAM - it runs every
 * frame on core0 while core1 is scanning the framebuffer out, and an XIP cache
 * miss here is paid 71,680 times. */
void __not_in_flash_func(Render::draw_frame)(uint16_t *pixels)
{
    const uint16_t *lut = rgb;
    const int w = src_width;

    for (int y = 0; y < src_height; y++)
    {
        uint16_t *dst = fb_line(y + OUTRUN_YOFFSET);
        const uint16_t *src = pixels + (y * w);
        for (int x = 0; x < w; x++)
        {
            dst[x] = lut[src[x]];
        }
    }
}
