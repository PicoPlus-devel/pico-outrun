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

#include "FrensFonts.h"
#include "FrensHelpers.h"
#include "settings.h"

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

/* ---------------------------------------------------------------------------
 * FPS overlay (settings menu -> Show FPS).
 *
 * Modelled on the sibling emulators (see pico-genesisPlus drawFpsOverlay), but
 * this port has two rates worth seeing, not one:
 *
 *   loop  - how often the engine ticks. This is GAME SPEED: OutRun expects 60
 *           and anything less means the world runs slow.
 *   draw  - how many of those frames were actually rendered. Adaptive frame
 *           skip trades this away to protect `loop`.
 *
 * Showing only the drawn rate would hide the more important number, so it is
 * rendered as "loop/draw".
 *
 * Drawn straight into the framebuffer after the frame, in the 8 blank
 * letterbox lines above the 320x224 image, so it never overwrites the game.
 * ------------------------------------------------------------------------- */

#if HSTX
#define FPS_FG 0      // black (RGB555)
#define FPS_BG 0x7FFF // white (RGB555)
#else
#define FPS_FG 0     // black (RGB444)
#define FPS_BG 0xFFF // white (RGB444)
#endif

static char fps_text[8] = "--/--";
static int fps_len = 5;

void outrun_fps_set(unsigned long loop_fps, unsigned long drawn_fps)
{
    if (loop_fps > 99) loop_fps = 99;
    if (drawn_fps > 99) drawn_fps = 99;

    int n = 0;
    fps_text[n++] = (char)('0' + (loop_fps / 10) % 10);
    fps_text[n++] = (char)('0' + loop_fps % 10);
    fps_text[n++] = '/';
    fps_text[n++] = (char)('0' + (drawn_fps / 10) % 10);
    fps_text[n++] = (char)('0' + drawn_fps % 10);
    fps_len = n;
}

void outrun_fps_overlay(void)
{
    if (!settings.flags.displayFrameRate)
    {
        return;
    }

    /* The letterbox band above the image - drawing here costs the game nothing,
     * and OUTRUN_YOFFSET is exactly 8 lines, one character tall. */
    for (int line = 0; line < 8 && line < OUTRUN_YOFFSET; line++)
    {
        uint16_t *dst = fb_line(line) + 5;
        for (int i = 0; i < fps_len; i++)
        {
            char slice = getcharslicefrom8x8font(fps_text[i], line & 7);
            for (int bit = 0; bit < 8; bit++)
            {
                *dst++ = (slice & 1) ? FPS_FG : FPS_BG;
                slice >>= 1;
            }
        }
    }
}
