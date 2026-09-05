/*
 * picoOutRun's replacement for cannonball/src/main/sdl2/renderbase.hpp.
 *
 * Upstream's RenderBase abstracts three SDL back-ends (OpenGL, GLES, software
 * surface) over an SDL_Surface, and holds the palette lookup table that turns
 * System 16 palette indices into output pixels.
 *
 * picoOutRun keeps the lookup table - it is exactly what the port needs - and
 * throws away everything else. Two differences from upstream:
 *
 *   - `rgb` is uint16_t, not uint32_t. The output format is RGB444 on PicoDVI
 *     and RGB555 on HSTX, both 16-bit, which also halves the table from 32 KB
 *     to 16 KB. That matters: see port/README.md on the RAM budget.
 *
 *   - There is no window, scaling or scanline handling. pico_shared owns all of
 *     that (Frens::applyScreenMode), and the output is always 320x224 inside its
 *     320x240 framebuffer.
 */

#pragma once

#include "globals.hpp"
#include "stdint.hpp"

class RenderBase
{
public:
    RenderBase() = default;
    virtual ~RenderBase() = default;

    virtual bool init(int src_width, int src_height, int scale, int video_mode, int scanlines) = 0;
    virtual void disable() = 0;
    virtual bool start_frame() = 0;
    virtual bool finalize_frame() = 0;
    virtual void draw_frame(uint16_t *pixels) = 0;

    // Called by Video::refresh_palette() for each palette entry the game
    // writes. r1/g1/b1 are 5-bit System 16 components.
    void convert_palette(uint32_t adr, uint32_t r1, uint32_t g1, uint32_t b1);
    void set_shadow_intensity(float f);

    virtual bool supports_window() { return false; }
    virtual bool supports_vsync() { return true; }

protected:
    // Palette lookup, extended to hold the shadow colours in the upper half -
    // hwsprites writes `index + S16_PALETTE_ENTRIES` for a shadowed pixel.
    uint16_t rgb[S16_PALETTE_ENTRIES * 2] = {};

    // Original screen width and height
    int src_width = 0, src_height = 0;

    int shadow_multi = 0;
};
