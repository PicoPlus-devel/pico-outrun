/*
 * picoOutRun's replacement for cannonball/src/main/sdl2/rendersurface.hpp.
 *
 * The whole renderer is draw_frame(): one pass turning the engine's 320x224
 * buffer of System 16 palette indices into pixels in the framebuffer that
 * pico_shared already owns - Frens::framebuffer[] on PicoDVI, the HSTX driver's
 * own FRAMEBUFFER reached through hstx_getlineFromFramebuffer() on HSTX.
 *
 * WHY THERE IS STILL AN INDEX BUFFER. The obvious optimisation is to render
 * straight into the framebuffer through the palette LUT and drop the 143 KB
 * index buffer. It does not work: hwsprites' draw_pixel() READS the buffer back
 * to implement shadows -
 *     pPixel[x] &= 0xfff; pPixel[x] += S16_PALETTE_ENTRIES;
 * and clears the shadow flag on the pixel before it. Those are operations on a
 * palette index, not a colour, so the engine's buffer has to stay indices and
 * the conversion has to be a separate pass. OutRun uses shadows heavily, so this
 * is not optional.
 *
 * Rendering into the framebuffer as indices and converting in place would save
 * the memory, but the framebuffer is scanned out live by DMA - the display would
 * show raw index values for most of every frame.
 */

#pragma once

#include "renderbase.hpp"

class Render : public RenderBase
{
public:
    Render() = default;
    ~Render() override = default;

    bool init(int src_width, int src_height, int scale, int video_mode, int scanlines) override;
    void disable() override;
    bool start_frame() override;
    bool finalize_frame() override;
    void draw_frame(uint16_t *pixels) override;
};
