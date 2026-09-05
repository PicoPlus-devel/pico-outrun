/***************************************************************************
    Video Rendering. 
    
    - Renders the System 16 Video Layers
    - Handles Reads and Writes to these layers from the main game code
    - Interfaces with platform specific rendering code

    Copyright Chris White.
    See license.txt for more details.
***************************************************************************/

#include <iostream>

#include "video.hpp"
#ifdef OUTRUN_GFX_IN_FLASH
#include "outrun_gfx.hpp"
#include "outrun_alloc.hpp"
#include <new>
#endif
#include "globals.hpp"
#include "frontend/config.hpp"
#include "engine/oroad.hpp"

#ifdef WITH_OPENGL
#include "sdl2/rendergl.hpp"
#elif WITH_OPENGLES
#include "sdl2/rendergles.hpp"
#else
#include "sdl2/rendersurface.hpp"
#endif

Video video;

Video::Video(void)
{
#ifdef OUTRUN_GFX_IN_FLASH
    /* picoOutRun: `Video video;` is a global, so this constructor runs BEFORE
     * main() - and therefore before Frens::initAll() has brought up PSRAM or
     * the display. Allocating here took ~90 KB of SRAM out of the heap before
     * the framework had started, leaving 3 KB free by the time the HSTX driver
     * asked for its data-island ring buffer.
     *
     * So nothing is allocated here any more; init() does it, once PSRAM exists.
     * Do NOT move these back into the constructor. */
    renderer     = NULL;
    pixels       = NULL;
    sprite_layer = NULL;
    tile_layer   = NULL;
    enabled      = false;
#else
    renderer     = new Render();
    pixels       = NULL;
    sprite_layer = new hwsprites();
    tile_layer   = new hwtiles();

    set_shadow_intensity(shadow::ORIGINAL);
    enabled      = false;
#endif
}

Video::~Video(void)
{
#ifdef OUTRUN_GFX_IN_FLASH
    /* tile_layer was placement-new'd into PSRAM, so it is destroyed by hand
     * rather than deleted. In practice this global is never destroyed on the
     * target; this keeps the code honest rather than useful. */
    if (sprite_layer) { sprite_layer->~hwsprites(); outrun_psram_free(sprite_layer); }
    if (tile_layer) { tile_layer->~hwtiles(); outrun_psram_free(tile_layer); }
    if (pixels) outrun_psram_free(pixels);
#else
    delete sprite_layer;
    delete tile_layer;
    if (pixels) delete[] pixels;
#endif
    renderer->disable();
    delete renderer;
}

int Video::init(Roms* roms, video_settings_t* settings)
{
#ifdef OUTRUN_GFX_IN_FLASH
    /* Deferred out of the constructor - see there. hwtiles carries 68 KB of
     * tile/text RAM and goes to PSRAM; Render's 16 KB palette LUT is read once
     * per output pixel in draw_frame(), so it stays in SRAM. */
    if (!renderer)
    {
        renderer = new Render();
        set_shadow_intensity(shadow::ORIGINAL);
    }
    /* PSRAM: sprite RAM is read ~128 times a frame (once per sprite entry), not
     * per pixel, so it tolerates the slower memory - and it funds the larger
     * audio ring. */
    if (!sprite_layer)
        sprite_layer = new (outrun_psram_alloc(sizeof(hwsprites))) hwsprites();
    /* PSRAM. Its 68 KB of tile/text RAM is the last large video buffer, and the
     * SRAM it frees goes to the audio hot path instead - audio is what is still
     * wrong, and core1 running the YM2151 out of flash is the likely reason.
     * Tile RAM is read per tile of a scanline, not per pixel, so it is the
     * cheaper of the two to slow down. */
    if (!tile_layer)
        tile_layer = new (outrun_psram_alloc(sizeof(hwtiles))) hwtiles();
#endif

    if (!set_video_mode(settings))
        return 0;

    // Internal pixel array. The size of this is always constant
#ifdef OUTRUN_GFX_IN_FLASH
    /* picoOutRun: 143 KB in PSRAM. This is the ONE thing left there, and it is
     * a deliberate trade rather than a free choice - it is also the hottest
     * buffer in the port (71,680 writes and 71,680 reads per frame), and an
     * earlier build with it in PSRAM ran at 18 fps.
     *
     * What buys that back is what the freed SRAM is spent on: the YM2151's
     * lookup tables and hwtiles both moved INTO SRAM, so core1 can now make its
     * audio deadline and tile rendering got faster. Frame skip absorbs the rest.
     * Revert all three together or not at all. */
    if (pixels) outrun_psram_free(pixels);
    pixels = (uint16_t*) outrun_psram_alloc(config.s16_width * config.s16_height * sizeof(uint16_t));
    if (!pixels) return 0;
#else
    if (pixels) delete[] pixels;
    pixels = new uint16_t[config.s16_width * config.s16_height];
#endif

#ifdef OUTRUN_GFX_IN_FLASH
    /* picoOutRun: tiles, sprites and road were decoded at build time by
     * tools/mkoutrundata and live in flash. Nothing to convert and nothing to
     * free - the layers just take a pointer. */
    tile_layer->init(outrun_gfx_tiles(), config.video.hires != 0);
    clear_tile_ram();
    clear_text_ram();
    sprite_layer->init(outrun_gfx_sprites());
    hwroad.init(outrun_gfx_road(), config.video.hires != 0);
#else
    // Convert S16 tiles to a more useable format
    tile_layer->init(roms->tiles.rom, config.video.hires != 0);
    
    clear_tile_ram();
    clear_text_ram();
    if (roms->tiles.rom)
    {
        delete[] roms->tiles.rom;
        roms->tiles.rom = NULL;
    }

    // Convert S16 sprites
    sprite_layer->init(roms->sprites.rom);
    if (roms->sprites.rom)
    {
        delete[] roms->sprites.rom;
        roms->sprites.rom = NULL;
    }

    // Convert S16 Road Stuff
    hwroad.init(roms->road.rom, config.video.hires != 0);
    if (roms->road.rom)
    {
        delete[] roms->road.rom;
        roms->road.rom = NULL;
    }
#endif

    enabled = true;
    return 1;
}

void Video::disable()
{
    renderer->disable();
    enabled = false;
}

// ------------------------------------------------------------------------------------------------
// Configure video settings from config file
// ------------------------------------------------------------------------------------------------

int Video::set_video_mode(video_settings_t* settings)
{
    if (settings->widescreen)
    {
        config.s16_width  = S16_WIDTH_WIDE;
        config.s16_x_off = (S16_WIDTH_WIDE - S16_WIDTH) / 2;
    }
    else
    {
        config.s16_width = S16_WIDTH;
        config.s16_x_off = 0;
    }

    config.s16_height = S16_HEIGHT;

    // Internal video buffer is doubled in hi-res mode.
    if (settings->hires)
    {
        config.s16_width  <<= 1;
        config.s16_height <<= 1;
    }

    if (settings->scanlines < 0) settings->scanlines = 0;
    else if (settings->scanlines > 100) settings->scanlines = 100;

    if (settings->scale < 1)
        settings->scale = 1;

    set_shadow_intensity(settings->shadow == 0 ? shadow::ORIGINAL : shadow::MAME);

    renderer->init(config.s16_width, config.s16_height, settings->scale, settings->mode, settings->scanlines);

    return 1;
}

// --------------------------------------------------------------------------------------------
// Shadow Colours. 
// 63% Intensity is the correct value derived from hardware as follows:
//
// 1/ Shadows are just an extra 220 ohm resistor that goes to ground when enabled.
// 2/ This is in parallel with the resistor-"DAC" (3.9k, 2k, 1k, 0.5k, 0.25k), 
//    and otherwise left floating.
//
// Static calculation example:
// 
// const float rDAC   = 1.f / (1.f/3900.f + 1.f/2000.f + 1.f/1000.f + 1.f/500.f + 1.f/250.f); 
// const float rShade = 220.f;                                                             
// const float shadeAttenuation = rShade / (rShade + rDAC); // 0.63f
// 
// (MAME uses an incorrect value which is closer to 78% Intensity)
// --------------------------------------------------------------------------------------------

void Video::set_shadow_intensity(float f)
{
    renderer->set_shadow_intensity(f);
}

void Video::prepare_frame()
{
    // Renderer Specific Frame Setup
    if (!renderer->start_frame())
        return;

    if (!enabled)
    {
        // Fill with black pixels
        for (int i = 0; i < config.s16_width * config.s16_height; i++)
            pixels[i] = 0;
    }
    else
    {
        // OutRun Hardware Video Emulation
        tile_layer->update_tile_values();

        (hwroad.*hwroad.render_background)(pixels);
        tile_layer->render_tile_layer(pixels, 1, 0);      // background layer
        tile_layer->render_tile_layer(pixels, 0, 0);      // foreground layer

        if (!config.engine.fix_bugs || oroad.horizon_base != ORoad::HORIZON_OFF)
            (hwroad.*hwroad.render_foreground)(pixels);
        sprite_layer->render(8);
        tile_layer->render_text_layer(pixels, 1);
     }
}

void Video::render_frame()
{
    renderer->draw_frame(pixels);
    renderer->finalize_frame();
}

bool Video::supports_window()
{
    return renderer->supports_window();
}

bool Video::supports_vsync()
{
    return renderer->supports_vsync();
}

// ---------------------------------------------------------------------------
// Text Handling Code
// ---------------------------------------------------------------------------

void Video::clear_text_ram()
{
    for (uint32_t i = 0; i <= 0xFFF; i++)
        tile_layer->text_ram[i] = 0;
}

void Video::write_text8(uint32_t addr, const uint8_t data)
{
    tile_layer->text_ram[addr & 0xFFF] = data;
}

void Video::write_text16(uint32_t* addr, const uint16_t data)
{
    tile_layer->text_ram[*addr & 0xFFF] = (data >> 8) & 0xFF;
    tile_layer->text_ram[(*addr+1) & 0xFFF] = data & 0xFF;

    *addr += 2;
}

void Video::write_text16(uint32_t addr, const uint16_t data)
{
    tile_layer->text_ram[addr & 0xFFF] = (data >> 8) & 0xFF;
    tile_layer->text_ram[(addr+1) & 0xFFF] = data & 0xFF;
}

void Video::write_text32(uint32_t* addr, const uint32_t data)
{
    tile_layer->text_ram[*addr & 0xFFF] = (data >> 24) & 0xFF;
    tile_layer->text_ram[(*addr+1) & 0xFFF] = (data >> 16) & 0xFF;
    tile_layer->text_ram[(*addr+2) & 0xFFF] = (data >> 8) & 0xFF;
    tile_layer->text_ram[(*addr+3) & 0xFFF] = data & 0xFF;

    *addr += 4;
}

void Video::write_text32(uint32_t addr, const uint32_t data)
{
    tile_layer->text_ram[addr & 0xFFF] = (data >> 24) & 0xFF;
    tile_layer->text_ram[(addr+1) & 0xFFF] = (data >> 16) & 0xFF;
    tile_layer->text_ram[(addr+2) & 0xFFF] = (data >> 8) & 0xFF;
    tile_layer->text_ram[(addr+3) & 0xFFF] = data & 0xFF;
}

uint8_t Video::read_text8(uint32_t addr)
{
    return tile_layer->text_ram[addr & 0xFFF];
}

// ---------------------------------------------------------------------------
// Tile Handling Code
// ---------------------------------------------------------------------------

void Video::clear_tile_ram()
{
    for (uint32_t i = 0; i <= 0xFFFF; i++)
        tile_layer->tile_ram[i] = 0;
}

void Video::write_tile8(uint32_t addr, const uint8_t data)
{
    tile_layer->tile_ram[addr & 0xFFFF] = data;
} 

void Video::write_tile16(uint32_t* addr, const uint16_t data)
{
    tile_layer->tile_ram[*addr & 0xFFFF] = (data >> 8) & 0xFF;
    tile_layer->tile_ram[(*addr+1) & 0xFFFF] = data & 0xFF;

    *addr += 2;
}

void Video::write_tile16(uint32_t addr, const uint16_t data)
{
    tile_layer->tile_ram[addr & 0xFFFF] = (data >> 8) & 0xFF;
    tile_layer->tile_ram[(addr+1) & 0xFFFF] = data & 0xFF;
}   

void Video::write_tile32(uint32_t* addr, const uint32_t data)
{
    tile_layer->tile_ram[*addr & 0xFFFF] = (data >> 24) & 0xFF;
    tile_layer->tile_ram[(*addr+1) & 0xFFFF] = (data >> 16) & 0xFF;
    tile_layer->tile_ram[(*addr+2) & 0xFFFF] = (data >> 8) & 0xFF;
    tile_layer->tile_ram[(*addr+3) & 0xFFFF] = data & 0xFF;

    *addr += 4;
}

void Video::write_tile32(uint32_t addr, const uint32_t data)
{
    tile_layer->tile_ram[addr & 0xFFFF] = (data >> 24) & 0xFF;
    tile_layer->tile_ram[(addr+1) & 0xFFFF] = (data >> 16) & 0xFF;
    tile_layer->tile_ram[(addr+2) & 0xFFFF] = (data >> 8) & 0xFF;
    tile_layer->tile_ram[(addr+3) & 0xFFFF] = data & 0xFF;
}

uint8_t Video::read_tile8(uint32_t addr)
{
    return tile_layer->tile_ram[addr & 0xFFFF];
}


// ---------------------------------------------------------------------------
// Sprite Handling Code
// ---------------------------------------------------------------------------

void Video::write_sprite16(uint32_t* addr, const uint16_t data)
{
    sprite_layer->write(*addr & 0xfff, data);
    *addr += 2;
}

// ---------------------------------------------------------------------------
// Palette Handling Code
// ---------------------------------------------------------------------------

void Video::write_pal8(uint32_t* palAddr, const uint8_t data)
{
    palette[*palAddr & 0x1fff] = data;
    refresh_palette(*palAddr & 0x1fff);
    *palAddr += 1;
}

void Video::write_pal16(uint32_t* palAddr, const uint16_t data)
{    
    uint32_t adr = *palAddr & 0x1fff;
    palette[adr]   = (data >> 8) & 0xFF;
    palette[adr+1] = data & 0xFF;
    refresh_palette(adr);
    *palAddr += 2;
}

void Video::write_pal32(uint32_t* palAddr, const uint32_t data)
{    
    uint32_t adr = *palAddr & 0x1fff;

    palette[adr]   = (data >> 24) & 0xFF;
    palette[adr+1] = (data >> 16) & 0xFF;
    palette[adr+2] = (data >> 8) & 0xFF;
    palette[adr+3] = data & 0xFF;

    refresh_palette(adr);
    refresh_palette(adr+2);

    *palAddr += 4;
}

void Video::write_pal32(uint32_t adr, const uint32_t data)
{    
    adr &= 0x1fff;

    palette[adr]   = (data >> 24) & 0xFF;
    palette[adr+1] = (data >> 16) & 0xFF;
    palette[adr+2] = (data >> 8) & 0xFF;
    palette[adr+3] = data & 0xFF;
    refresh_palette(adr);
    refresh_palette(adr+2);
}

uint8_t Video::read_pal8(uint32_t palAddr)
{
    return palette[palAddr & 0x1fff];
}

uint16_t Video::read_pal16(uint32_t palAddr)
{
    uint32_t adr = palAddr & 0x1fff;
    return (palette[adr] << 8) | palette[adr+1];
}

uint16_t Video::read_pal16(uint32_t* palAddr)
{
    uint32_t adr = *palAddr & 0x1fff;
    *palAddr += 2;
    return (palette[adr] << 8)| palette[adr+1];
}

uint32_t Video::read_pal32(uint32_t* palAddr)
{
    uint32_t adr = *palAddr & 0x1fff;
    *palAddr += 4;
    return (palette[adr] << 24) | (palette[adr+1] << 16) | (palette[adr+2] << 8) | palette[adr+3];
}

// Convert internal System 16 RRRR GGGG BBBB format palette to renderer output format
void Video::refresh_palette(uint32_t palAddr)
{
    palAddr &= ~1;
    uint32_t a = (palette[palAddr] << 8) | palette[palAddr + 1];
    uint32_t r = (a & 0x000f) << 1; // r rrr0
    uint32_t g = (a & 0x00f0) >> 3; // g ggg0
    uint32_t b = (a & 0x0f00) >> 7; // b bbb0
    if ((a & 0x1000) != 0)
        r |= 1; // r rrrr
    if ((a & 0x2000) != 0)
        g |= 1; // g gggg
    if ((a & 0x4000) != 0)
        b |= 1; // b bbbb

    renderer->convert_palette(palAddr, r, g, b);
}
