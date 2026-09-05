/*
 * A minimal text screen over pico_shared's framebuffer.
 *
 * picoOutRun needs to say things to the user before the engine exists - "no game
 * data", "building it from the SD card, file 7 of 31" - and pico_shared's own
 * text renderer cannot help: putText() writes into `screenBuffer`, which is
 * allocated only inside menu()/showSettingsMenu(), and the functions that turn
 * it into pixels (drawline, DrawScreen) are file-local to menu.cpp. The exported
 * 8x8 font is, however, all this actually needs.
 *
 * THE 30-COLUMN WINDOW IS NOT ARBITRARY. In 8:7 mode only a window of each
 * source line reaches the display - 252 pixels starting at x=34, stretched to
 * 576 (port/render.cpp, drivers/pico_hdmi/hstx.c). A full 40-column screen would
 * lose characters off both edges. Columns 5..34 are fully visible in BOTH screen
 * modes, so laying everything out in that window means a screen-mode change from
 * the settings menu needs no re-layout at all. The FPS overlay already dodges
 * the same edge with its `scaleMode8_7_ ? 40 : 5`.
 */

#pragma once

#include <stdint.h>

#include "FrensHelpers.h"

// OutRun is 320x224 inside pico_shared's 320x240 framebuffer: 8 blank lines top
// and bottom.
#define OUTRUN_WIDTH 320
#define OUTRUN_HEIGHT 224
#define OUTRUN_YOFFSET ((SCREENHEIGHT - OUTRUN_HEIGHT) / 2)

// The always-visible text window, in 8x8 character cells.
#define OUTRUN_TEXT_COL0 5
#define OUTRUN_TEXT_COLS 30
#define OUTRUN_TEXT_ROWS (OUTRUN_HEIGHT / 8) // 28

// Framebuffer access, the one place the two video back-ends differ for us.
static inline uint16_t *outrun_fb_line(int line)
{
#if HSTX
    return hstx_getlineFromFramebuffer(line);
#else
    return &Frens::framebuffer[line * SCREENWIDTH];
#endif
}

// RGB888 -> the back-end's native 16-bit format.
static inline uint16_t outrun_rgb(uint8_t r, uint8_t g, uint8_t b)
{
#if HSTX // RGB555: 0RRRRRGG GGGBBBBB
    return (uint16_t)(((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3));
#else    // RGB444: 0000RRRR GGGGBBBB
    return (uint16_t)(((r >> 4) << 8) | ((g >> 4) << 4) | (b >> 4));
#endif
}

// Fills every line, letterbox included, so the screen reads as deliberate rather
// than as a game that failed to draw.
void outrun_screen_clear(uint16_t bg);

// col/row are cells within the 30x28 window; text past its right edge is
// clipped. Pass col < 0 to centre the string in the window.
void outrun_screen_text(int col, int row, const char *s, uint16_t fg, uint16_t bg);

// A full-width [####....] progress bar occupying one row.
void outrun_screen_bar(int row, int percent, uint16_t fg, uint16_t bg);
