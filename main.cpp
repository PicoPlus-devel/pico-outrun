/*
 * picoOutRun - OutRun arcade port (Cannonball engine) for RP2350.
 *
 * MILESTONE 1: skeleton only. This boots the pico_shared framework the way the
 * finished port will, and then draws a static test pattern instead of running
 * the game. Its job is to prove the toolchain, the board matrix and both video
 * back-ends before any engine code exists.
 *
 * Deliberately different from the sibling emulators, and it stays that way:
 *   - no ROM browser. menu() is never called.
 *   - a missing/unmountable SD card is NOT fatal on its own. Settings simply
 *     fall back to defaults - and if the game data has been flashed, which is
 *     the normal case, nothing else on the card is needed either.
 *
 * The game data comes from one of two places, tried in this order: the data
 * .uf2 in flash, or - as a failsafe - a raw OutRun romset in /roms/ORUN, packed
 * and decoded into PSRAM at boot. See port/outrun_data.h. When neither is
 * present the board says so on screen rather than showing a test pattern.
 */

#include <cstdio>
#include <cstring>

#include "pico/stdlib.h"
#include "hardware/vreg.h"

#include "ff.h"
#include "tusb.h"

#include "FrensHelpers.h"
#include "gamepad.h"
#include "menu.h"
#include "menu_settings.h"
#include "nespad.h"
#include "settings.h"
#include "vumeter.h"
#include "wiipad.h"

#include "outrun_data.h"
#include "outrun_pack.h"
#include "outrun_screen.hpp"
#include "glue.hpp"
#include "sdl2/input.hpp"

// ---------------------------------------------------------------------------
// Clocks.
//
// Cannonball runs single-core on core0: core1 is owned by the display driver on
// every path (coreFB_main on PicoDVI, video_output_core1_run on HSTX), so all
// the engine and render headroom has to come out of the core clock. These are
// the pairings pico_shared/FlashParams.cpp knows about.
// ---------------------------------------------------------------------------
#if !HSTX
#define OUTRUN_CLOCKFREQ_KHZ 324000
#define OUTRUN_MAX_CLOCKFREQ_KHZ 324000
#define OUTRUN_VOLTAGE VREG_VOLTAGE_1_30
#define OUTRUN_MAX_VOLTAGE VREG_VOLTAGE_1_30
#else
/* Defaults to 378 MHz / 1.50 V, with 504 MHz / 1.70 V available as an opt-in.
 * This is exactly pico_snesPlus's arrangement, and for the same reason: the
 * higher clock is worth having on a CPU-bound engine, but not worth shipping as
 * the default.
 *
 * 504 MHz needs 1.70 V - snesPlus records that 1.60-1.65 V hardfaults in heavy
 * scenes - and its comment warns that 1.7 V "CAN CAUSE DAMAGE", being above the
 * RP2350's nominal core voltage. So it stays behind a deliberate choice rather
 * than being the value everyone gets.
 *
 * Options -> Overclock in the settings menu writes the chosen clock/voltage pair
 * to flash params, which initAll picks up on the next boot. No reflash needed to
 * move between the two. */
#define OUTRUN_CLOCKFREQ_KHZ 378000
#define OUTRUN_VOLTAGE VREG_VOLTAGE_1_50
#define OUTRUN_MIN_CLOCKFREQ_KHZ 378000
#define OUTRUN_MIN_VOLTAGE VREG_VOLTAGE_1_50
#define OUTRUN_MAX_CLOCKFREQ_KHZ 504000
#define OUTRUN_MAX_VOLTAGE VREG_VOLTAGE_1_70
#endif

#ifndef OUTRUN_MIN_CLOCKFREQ_KHZ
#define OUTRUN_MIN_CLOCKFREQ_KHZ OUTRUN_CLOCKFREQ_KHZ
#define OUTRUN_MIN_VOLTAGE OUTRUN_VOLTAGE
#endif

static uint32_t CPUFreqKHz = OUTRUN_CLOCKFREQ_KHZ;

// OUTRUN_WIDTH / OUTRUN_HEIGHT / OUTRUN_YOFFSET, the framebuffer line accessor
// and the RGB packer all come from port/outrun_screen.hpp, which the error and
// progress screens share with this file.

// Margins are a PicoDVI line-buffer concept and are forced to 0 when a
// framebuffer is in use; pass 0 explicitly so the intent is visible.
#define MARGINTOP 0
#define MARGINBOTTOM 0

// Must be a power of two - util::RingBuffer::setBuffer asserts it. 1024 is the
// framebuffer-path convention; 256 has caused intermittent startup deadlocks.
#define AUDIOBUFFERSIZE 1024

// ---------------------------------------------------------------------------
// Settings menu wiring.
//
// Positional, indexed by MenuSettingsIndex - append only, never reorder.
// 1 = shown, 0 = hidden. Every entry is listed: C++ designated initializers may
// not skip members, so a gap is a compile error, not a zero.
// ---------------------------------------------------------------------------
int8_t g_settings_visibility_outrun[MOPT_COUNT] = {
    [MOPT_EXIT_GAME] = -1,               // nowhere to exit to: there is no ROM browser
    [MOPT_RESET_GAME] = 1,
    [MOPT_REBOOT_TO_LOADER] = 0,        // Phase 2: Frens::isLaunchedFromBootloader()
    [MOPT_SAVE_RESTORE_STATE] = -1,      // no save states
    [MOPT_SCREENMODE] = 1,
    [MOPT_SCANLINES] = 0,
    [MOPT_SCANLINE_TYPE] = 1,
    [MOPT_FPS_OVERLAY] = 1,
    [MOPT_AUDIO_ENABLE] = 1,
    [MOPT_FRAMESKIP] = 0,
    [MOPT_DISPLAY_MODE] = 1,
    [MOPT_EXTERNAL_AUDIO] = 1,
    [MOPT_FONT_COLOR] = 0,
    [MOPT_FONT_BACK_COLOR] = 0,
    [MOPT_FRUITJAM_VUMETER] = 1,
    [MOPT_FRUITJAM_VOLUME_CONTROL] = 1,
    [MOPT_DMG_PALETTE] = 0,             // Game Boy
    [MOPT_BORDER_MODE] = 0,             // Game Boy
    [MOPT_RAPID_FIRE_ON_A] = 0,
    [MOPT_RAPID_FIRE_ON_B] = 0,
    [MOPT_AUTO_INSERT_FDS_DISK_A] = 0,  // Famicom Disk System
    [MOPT_AUTO_SWAP_FDS_DISK] = 0,      // Famicom Disk System
    [MOPT_FDS_DISK_SWAP] = 0,           // Famicom Disk System
    [MOPT_OVERCLOCK] = 1,               // 378 MHz / 1.50 V .. 504 MHz / 1.70 V
    [MOPT_FM_AUDIO] = 0,                // Master System YM2413
    [MOPT_ENTER_BOOTSEL_MODE] = 1,
    [MOPT_CONTROLLER_TEST] = 1,
    [MOPT_RECENT_GAMES] = 0,            // ROM browser only
    [MOPT_USB_DRIVE_MODE] = 0,
};

const uint8_t g_available_screen_modes_outrun[] = {
    1, // SCANLINE_8_7
    1, // NOSCANLINE_8_7
    1, // SCANLINE_1_1
    1, // NOSCANLINE_1_1
};

// Eight SMPTE-ish bars over the 224 active lines, with the 8-line letterbox
// left black, so geometry, colour order and the active area are all verifiable
// at a glance on a real display. Reached only from draw_tile_page below, when a
// region it wanted turns out to be empty; a board with no game data at all gets
// draw_error_screen(), which explains itself.
static void draw_test_pattern(void)
{
    static const uint8_t bars[8][3] = {
        {255, 255, 255}, {255, 255, 0}, {0, 255, 255}, {0, 255, 0},
        {255, 0, 255},   {255, 0, 0},   {0, 0, 255},   {0, 0, 0},
    };

    for (int y = 0; y < SCREENHEIGHT; y++)
    {
        uint16_t *dst = outrun_fb_line(y);
        bool active = (y >= OUTRUN_YOFFSET) && (y < OUTRUN_YOFFSET + OUTRUN_HEIGHT);
        for (int x = 0; x < SCREENWIDTH; x++)
        {
            if (!active)
            {
                dst[x] = 0;
                continue;
            }
            const uint8_t *c = bars[(x * 8) / SCREENWIDTH];
            dst[x] = outrun_rgb(c[0], c[1], c[2]);
        }
    }
}

// MILESTONE 3 check, and a preview of the real renderer.
//
// Draws a page of decoded OutRun tiles straight out of the data image, wherever
// it lives. Each uint32 is one 8-pixel row at 4bpp, leftmost pixel in the
// HIGHEST nibble (hwtiles::init builds it with `val = (val << 4) | pix`), and a
// tile is 8 consecutive words.
//
// The real palette lives in OutRun's palette RAM, which the game fills at
// runtime and no ROM contains, so this uses a synthetic ramp over the 3bpp tile
// values. The point is not correct colour - it is that recognisable arcade
// artwork appearing on screen proves the whole chain: the packer decoded
// correctly, the image landed where it was meant to, and reads from it work.
// That makes it a free acceptance test for the SD path too - recognisable tiles
// mean the on-device decoders ran correctly in PSRAM.
static void draw_tile_page(uint32_t first_tile)
{
    uint32_t size = 0;
    const uint32_t *tiles = (const uint32_t *)outrun_data_region(OUTRUN_REGION_TILES, &size);
    if (!tiles)
    {
        draw_test_pattern();
        return;
    }
    const uint32_t words = size / 4;

    static const uint8_t ramp[8][3] = {
        {0, 0, 0},       {64, 64, 96},    {128, 128, 160}, {192, 192, 224},
        {224, 160, 96},  {224, 96, 96},   {96, 192, 224},  {255, 255, 255},
    };
    uint16_t pal[16];
    for (int i = 0; i < 16; i++)
    {
        const uint8_t *c = ramp[i & 7];
        pal[i] = outrun_rgb(c[0], c[1], c[2]);
    }

    const int cols = SCREENWIDTH / 8;    // 40
    const int rows = OUTRUN_HEIGHT / 8;  // 28

    for (int y = 0; y < SCREENHEIGHT; y++)
    {
        uint16_t *dst = outrun_fb_line(y);
        int ty = y - OUTRUN_YOFFSET;
        if (ty < 0 || ty >= OUTRUN_HEIGHT)
        {
            memset(dst, 0, SCREENWIDTH * sizeof(uint16_t));
            continue;
        }
        for (int cx = 0; cx < cols; cx++)
        {
            uint32_t tile = first_tile + (uint32_t)(ty / 8) * cols + (uint32_t)cx;
            uint32_t idx = tile * 8 + (uint32_t)(ty & 7);
            uint32_t row = (idx < words) ? tiles[idx] : 0;
            for (int px = 0; px < 8; px++)
            {
                dst[cx * 8 + px] = pal[(row >> (4 * (7 - px))) & 0xF];
            }
        }
    }
    (void)rows;
}

// ---------------------------------------------------------------------------
// The two screens shown before the engine exists.
//
// Both use the 30-column window of port/outrun_screen.hpp, which is the only
// span visible in BOTH screen modes - so nothing has to be re-laid-out when the
// user changes the mode from the settings menu.
// ---------------------------------------------------------------------------
#define SCR_BG outrun_rgb(0, 0, 0)
#define SCR_FG outrun_rgb(200, 200, 200)
#define SCR_HI outrun_rgb(255, 176, 0)   // OutRun amber
#define SCR_FILE outrun_rgb(255, 255, 255)

// Passed to outrun_data_init() and called as each ROM file is read and each
// region decoded. Repaints only the rows that change, so it costs nothing.
static void loading_progress(int step, int steps, const char *what)
{
    static bool painted;
    if (!painted)
    {
        painted = true;
        outrun_screen_clear(SCR_BG);
        outrun_screen_text(-1, 1, "picoOutRun", SCR_HI, SCR_BG);
        outrun_screen_text(0, 4, "Building game data from the", SCR_FG, SCR_BG);
        outrun_screen_text(0, 5, "romset on the SD card.", SCR_FG, SCR_BG);
    }

    const int pct = (steps > 0) ? (step * 100) / steps : 0;
    outrun_screen_bar(8, pct, SCR_HI, SCR_BG);

    /* Padded to the full window width so the previous, longer label is erased
     * rather than left showing through. The label already carries the file
     * count - see io_on_file in port/outrun_sdload.cpp. */
    char line[OUTRUN_TEXT_COLS + 1];
    snprintf(line, sizeof(line), "%-*s", OUTRUN_TEXT_COLS, what ? what : "");
    outrun_screen_text(0, 10, line, SCR_FILE, SCR_BG);
}

// Says what is wrong and what to do about it. Everything here has to fit 30
// columns; the reason block is rows 3..9 and the remedy block rows 11..20.
static void draw_error_screen(void)
{
    char buf[OUTRUN_TEXT_COLS + 1];
    int row = 3;

    outrun_screen_clear(SCR_BG);
    outrun_screen_text(-1, 1, "picoOutRun", SCR_HI, SCR_BG);

    switch (outrun_data_error())
    {
    case OUTRUN_DATA_ERR_NO_PSRAM:
        outrun_screen_text(0, row++, "This board has no PSRAM.", SCR_HI, SCR_BG);
        outrun_screen_text(0, row++, "picoOutRun cannot run on it.", SCR_FG, SCR_BG);
        break;

    case OUTRUN_DATA_ERR_NO_SD:
        outrun_screen_text(0, row++, "No game data, no SD card.", SCR_HI, SCR_BG);
        break;

    case OUTRUN_DATA_ERR_NO_ROMS:
        outrun_screen_text(0, row++, "No game data.", SCR_HI, SCR_BG);
        row++;
        outrun_screen_text(0, row++, "No OutRun ROM files were", SCR_FG, SCR_BG);
        outrun_screen_text(0, row++, "found in", SCR_FG, SCR_BG);
        snprintf(buf, sizeof(buf), "  %s", outrun_data_romdir());
        outrun_screen_text(0, row++, buf, SCR_FILE, SCR_BG);
        break;

    case OUTRUN_DATA_ERR_MISSING:
        outrun_screen_text(0, row++, "Incomplete romset.", SCR_HI, SCR_BG);
        row++;
        snprintf(buf, sizeof(buf), "%d of %d ROM files missing:", outrun_data_bad_file_count(),
                 OUTRUN_PACK_FILE_COUNT);
        outrun_screen_text(0, row++, buf, SCR_FG, SCR_BG);
        for (int i = 0; i < OUTRUN_DATA_MAX_BAD_FILES && outrun_data_bad_file(i); i++)
        {
            snprintf(buf, sizeof(buf), "  %s", outrun_data_bad_file(i));
            outrun_screen_text(0, row++, buf, SCR_FILE, SCR_BG);
        }
        if (outrun_data_bad_file_count() > OUTRUN_DATA_MAX_BAD_FILES)
        {
            snprintf(buf, sizeof(buf), "  ...and %d more",
                     outrun_data_bad_file_count() - OUTRUN_DATA_MAX_BAD_FILES);
            outrun_screen_text(0, row++, buf, SCR_FG, SCR_BG);
        }
        break;

    case OUTRUN_DATA_ERR_BAD_CRC:
        outrun_screen_text(0, row++, "Wrong or corrupt ROMs.", SCR_HI, SCR_BG);
        row++;
        outrun_screen_text(0, row++, "Checksum failed on:", SCR_FG, SCR_BG);
        for (int i = 0; i < OUTRUN_DATA_MAX_BAD_FILES && outrun_data_bad_file(i); i++)
        {
            snprintf(buf, sizeof(buf), "  %s", outrun_data_bad_file(i));
            outrun_screen_text(0, row++, buf, SCR_FILE, SCR_BG);
        }
        outrun_screen_text(0, row++, "Use the MAME \"outrun\"", SCR_FG, SCR_BG);
        outrun_screen_text(0, row++, "revision B parent set.", SCR_FG, SCR_BG);
        break;

    case OUTRUN_DATA_ERR_READ:
        outrun_screen_text(0, row++, "SD card read error.", SCR_HI, SCR_BG);
        row++;
        snprintf(buf, sizeof(buf), "FatFs error %d on:", outrun_data_error_detail());
        outrun_screen_text(0, row++, buf, SCR_FG, SCR_BG);
        if (outrun_data_bad_file(0))
        {
            snprintf(buf, sizeof(buf), "  %s", outrun_data_bad_file(0));
            outrun_screen_text(0, row++, buf, SCR_FILE, SCR_BG);
        }
        outrun_screen_text(0, row++, "Reseat the card and reset.", SCR_FG, SCR_BG);
        break;

    case OUTRUN_DATA_ERR_NO_MEMORY:
        outrun_screen_text(0, row++, "Not enough free PSRAM to", SCR_HI, SCR_BG);
        outrun_screen_text(0, row++, "build the game data here.", SCR_HI, SCR_BG);
        row++;
        snprintf(buf, sizeof(buf), "%d KB short.", outrun_data_error_detail());
        outrun_screen_text(0, row++, buf, SCR_FG, SCR_BG);
        break;

    default:
        outrun_screen_text(0, row++, "No game data.", SCR_HI, SCR_BG);
        break;
    }

    /* The remedies. Both routes need the user's own romset, which is never
     * shipped with the port. */
    row = 12;
    outrun_screen_text(0, row++, "Supply the OutRun rev B", SCR_FG, SCR_BG);
    outrun_screen_text(0, row++, "romset yourself, either by:", SCR_FG, SCR_BG);
    row++;
    outrun_screen_text(0, row++, "1) copying the ROM files,", SCR_FG, SCR_BG);
    outrun_screen_text(0, row++, "   unzipped, to /roms/ORUN", SCR_FG, SCR_BG);
    outrun_screen_text(0, row++, "   on the SD card, or", SCR_FG, SCR_BG);
    outrun_screen_text(0, row++, "2) building outrun-data.uf2", SCR_FG, SCR_BG);
    outrun_screen_text(0, row++, "   with tools/mkoutrundata.sh", SCR_FG, SCR_BG);
    outrun_screen_text(0, row++, "   and flashing it.", SCR_FG, SCR_BG);

    outrun_screen_text(0, 25, "SELECT+START opens settings,", SCR_HI, SCR_BG);
    outrun_screen_text(0, 26, "which can enter USB drive mode.", SCR_HI, SCR_BG);
}

// ---------------------------------------------------------------------------
// Per-frame framework housekeeping. Same shape the finished port keeps.
// ---------------------------------------------------------------------------
static bool showSettings = false;
static uint32_t tilePage = 0;
static bool haveData = false;
static bool engineRunning = false;

#define TILES_PER_PAGE ((SCREENWIDTH / 8) * (OUTRUN_HEIGHT / 8)) // 40 x 28

// Merge the pad sources the way the finished port will. Returns pico_shared's
// io::GamePadState button bits; nespad is LSB-first on the wire
// (0x01 = A ... 0x80 = Right), whatever the comment in nespad.cpp says.
static uint32_t readPads(void)
{
    uint32_t b = 0;
    auto &gp = io::getCurrentGamePadState(0);
    if (gp.connected)
    {
        b |= gp.buttons;
    }
    uint8_t n = nespad_states[0];
    if (n & 0x80) b |= io::GamePadState::Button::RIGHT;
    if (n & 0x40) b |= io::GamePadState::Button::LEFT;
    if (n & 0x08) b |= io::GamePadState::Button::START;
    if (n & 0x04) b |= io::GamePadState::Button::SELECT;
    return b;
}

static void processPerFrame(void)
{
    Frens::PaceFrames60fps(false);

    Frens::pollHeadPhoneJack();
    EXT_AUDIO_POLL_HEADPHONE();

    nespad_read_start();
#if HSTX
    uint32_t frame = hstx_getframecounter();
#else
    uint32_t frame = dvi_->getFrameCounter();
#endif
    Frens::blinkLed((frame >> 5) & 1);
    nespad_read_finish();

    tuh_task();
#if WII_PIN_SDA >= 0 and WII_PIN_SCL >= 0
    wiipad_read(); // boards without the Wii port do not link wiipad at all
#endif

    static uint32_t prevButtons = 0;
    uint32_t buttons = readPads();
    uint32_t pressed = buttons & ~prevButtons;
    prevButtons = buttons;

    if (engineRunning)
    {
        typedef io::GamePadState::Button B;
        input.set_button(Input::LEFT, buttons & B::LEFT);
        input.set_button(Input::RIGHT, buttons & B::RIGHT);
        input.set_button(Input::UP, buttons & B::UP);
        input.set_button(Input::DOWN, buttons & B::DOWN);
        input.set_button(Input::ACCEL, buttons & B::A);
        input.set_button(Input::BRAKE, buttons & B::B);
        input.set_button(Input::GEAR1, buttons & B::X);
        input.set_button(Input::START, buttons & B::START);
        input.set_button(Input::COIN, buttons & B::SELECT);

        // Analog steering when the pad has a stick; axis[0] is 0x80-centred,
        // which is exactly the range the engine expects in a_wheel.
        auto &gp = io::getCurrentGamePadState(0);
        input.analog = gp.connected ? 1 : 0;
        if (input.analog)
        {
            input.a_wheel = gp.axis[0];
            input.a_accel = (buttons & B::A) ? 0xFF : 0;
            input.a_brake = (buttons & B::B) ? 0xFF : 0;
        }
    }

    /* SELECT + START opens the settings menu. This MUST come before the engine
     * tick below: it used to sit after it, behind an early return left over from
     * when the tile viewer was the main path, so the menu was unreachable the
     * moment the engine started. */
    if ((buttons & io::GamePadState::Button::SELECT) && (buttons & io::GamePadState::Button::START))
    {
        showSettings = true;
    }

    bool repaint = false;

    if (showSettings)
    {
        showSettings = false;

        /* Drop everything the engine thinks is held, or the car drives itself
         * while the menu is up and the combo re-triggers on the way out. */
        if (engineRunning)
        {
            input.init();
        }

        int rval = showSettingsMenu(true);
        prevButtons = 0;

        /* menu.cpp persists on its own, but only down the SAVE path - B backs
         * out as CANCEL and writes nothing. Write again here: `settings` only
         * ever holds committed values (the menu edits a `working` copy and
         * copies it over on SAVE), so this can never persist a cancelled edit,
         * and it means the file is written even if the menu took an exit path
         * that skips it. savesettings() logs its result, so a genuine SD
         * failure shows up on the UART rather than silently doing nothing. */
        FrensSettings::savesettings();

        // The menu can change screen mode; pico_shared needs this re-applied.
        scaleMode8_7_ = Frens::applyScreenMode(settings.screenMode);

        if (engineRunning)
        {
            if (rval == 5) // Reset Game
            {
                outrun_engine_reset();
            }
            // The engine repaints the whole frame on its next render, so there
            // is nothing to restore here.
        }
        else
        {
            repaint = true;
        }
        if ( !haveData) {
            printf("Rebooting due to missing data...\n");
            watchdog_reboot(0, 0, 0);
        }
    }

    if (engineRunning)
    {
        outrun_engine_tick();
        return;
    }

    if (haveData && (pressed & io::GamePadState::Button::RIGHT))
    {
        tilePage++;
        repaint = true;
    }
    if (haveData && (pressed & io::GamePadState::Button::LEFT) && tilePage > 0)
    {
        tilePage--;
        repaint = true;
    }

    if (repaint)
    {
        if (haveData)
        {
            printf("tile page %lu (tiles %lu..%lu)\n", (unsigned long)tilePage,
                   (unsigned long)(tilePage * TILES_PER_PAGE),
                   (unsigned long)((tilePage + 1) * TILES_PER_PAGE - 1));
            draw_tile_page(tilePage * TILES_PER_PAGE);
        }
        else
        {
            draw_error_screen();
        }
    }
}

int main()
{
    // Must be first: sets vreg, sys clock, the HSTX clock and stdio.
    vreg_voltage voltage = OUTRUN_VOLTAGE;
    Frens::setOverclockLimits(OUTRUN_MIN_CLOCKFREQ_KHZ, OUTRUN_MAX_CLOCKFREQ_KHZ,
                              OUTRUN_MIN_VOLTAGE, OUTRUN_MAX_VOLTAGE);

    /* A clock/voltage pair chosen from the settings menu wins over the default,
     * so an unstable overclock can be walked back on the device. Same pattern as
     * pico_snesPlus. */
    Frens::FlashParams *flashParams = (Frens::FlashParams *)FLASHPARAM_ADDRESS;
    if (Frens::validateFlashParams(*flashParams))
    {
        CPUFreqKHz = flashParams->cpuFreqKHz;
        voltage = flashParams->voltage;
    }

    Frens::setClocksAndStartStdio(CPUFreqKHz, voltage);

    printf("\npicoOutRun %s (%s %s)\n", SWVERSION, __DATE__, __TIME__);
    printf("HW_CONFIG=%d  HSTX=%d  clk_sys=%lu kHz\n", HW_CONFIG, HSTX, CPUFreqKHz);

    FrensSettings::initSettings(FrensSettings::OUTRUN);

    // No ROM is ever selected: the game data is in flash. initAll() only looks
    // at this buffer on the menu's watchdog-reboot path, which we never take.
    char dummyRom[FF_MAX_LFN];
    dummyRom[0] = 0;
   
    // useFrameBuffer MUST be true. Passing false silently disables
    // framebuffer-direct drawing on the RP2350 PicoDVI path.
    bool sdOk = Frens::initAll(dummyRom, CPUFreqKHz, MARGINTOP, MARGINBOTTOM,
                               AUDIOBUFFERSIZE, false, true);
  
    if (!sdOk)
    {
        // Not fatal: only settings persistence lives on the card.
        printf("No SD card - continuing with default settings.\n");
    }
    else
    {
        /* loadsettings() inside initAll validates settings.currentDir and
         * resets EVERY setting if that directory is missing:
         *
         *     Read 280 bytes from /settings_ORUN.dat
         *     Directory /roms/ORUN does not exist
         *     Resetting settings
         *
         * currentDir is a ROM-browser concept, and this port has no ROM
         * browser, so nothing ever creates /roms/ORUN and saved settings were
         * discarded on every boot. Create it and load again - the second pass
         * passes the check and applies what was saved.
         *
         * Done here rather than in pico_shared because the directory check is
         * correct for the emulators; it is this port that is unusual. */
        f_mkdir("/roms");
        f_mkdir("/roms/ORUN"); // FR_EXIST on later boots, which is fine
        FrensSettings::loadsettings();
    }
    // No filebrowser in this port; force the currentDir to the expected ROM path.
    strcpy(settings.currentDir, "/roms/ORUN");
    g_settings_visibility = g_settings_visibility_outrun;
    g_available_screen_modes = g_available_screen_modes_outrun;
    scaleMode8_7_ = Frens::applyScreenMode(settings.screenMode);

    /* Flash first, then the romset on the SD card. This is the right point in
     * the sequence: initAll has brought up PSRAM, the SD card and the display,
     * settings.currentDir is final, and the screen mode has been applied - so
     * the progress and error screens can both be drawn from here on. */
    haveData = outrun_data_init(settings.currentDir, sdOk, loading_progress);
    if (haveData)
    {
        printf("Game data OK at %p (%s) - starting the engine.\n",
               (const void *)outrun_data_base(),
               outrun_data_source() == OUTRUN_DATA_SOURCE_FLASH ? "flash"
                                                                : "PSRAM, built from the SD card");
        Frens::dumpHeapStats("before engine init");
        engineRunning = outrun_engine_init();
        Frens::dumpHeapStats("after engine init");
        if (!engineRunning)
        {
            // Fall back to the tile viewer so the board still shows something
            // useful, and say why over UART.
            printf("Engine init failed - falling back to the tile viewer.\n");
            draw_tile_page(0);
        }
    }
    else
    {
        printf("[data] no game data (error %d, detail %d) - showing the error screen\n",
               (int)outrun_data_error(), outrun_data_error_detail());
        draw_error_screen();
    }

    Frens::PaceFrames60fps(true);
    while (true)
    {
        processPerFrame();
    }
}
