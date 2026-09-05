#include "outrun_screen.hpp"

#include <cstring>

#include "FrensFonts.h"

void outrun_screen_clear(uint16_t bg)
{
    for (int y = 0; y < SCREENHEIGHT; y++)
    {
        uint16_t *dst = outrun_fb_line(y);
        for (int x = 0; x < SCREENWIDTH; x++)
        {
            dst[x] = bg;
        }
    }
}

void outrun_screen_text(int col, int row, const char *s, uint16_t fg, uint16_t bg)
{
    if (!s || row < 0 || row >= OUTRUN_TEXT_ROWS)
    {
        return;
    }

    const int len = (int)strlen(s);
    if (col < 0)
    {
        col = (OUTRUN_TEXT_COLS - len) / 2;
        if (col < 0)
        {
            col = 0;
        }
    }

    const int y0 = OUTRUN_YOFFSET + row * FONT_CHAR_HEIGHT;

    for (int i = 0; i < len; i++)
    {
        const int cell = col + i;
        if (cell < 0 || cell >= OUTRUN_TEXT_COLS)
        {
            continue;
        }

        char c = s[i];
        if (c < FONT_FIRST_ASCII || c >= FONT_FIRST_ASCII + FONT_N_CHARS)
        {
            c = ' ';
        }

        const int x0 = (OUTRUN_TEXT_COL0 + cell) * FONT_CHAR_WIDTH;

        for (int line = 0; line < FONT_CHAR_HEIGHT; line++)
        {
            uint16_t *dst = outrun_fb_line(y0 + line) + x0;
            /* getcharslicefrom8x8font returns the row with the LEFTMOST pixel in
             * the LOW bit - see pico_shared/menu.cpp and the FPS overlay in
             * port/render.cpp, which both shift right as they walk left to
             * right. */
            char slice = getcharslicefrom8x8font(c, line);
            for (int bit = 0; bit < FONT_CHAR_WIDTH; bit++)
            {
                *dst++ = (slice & 1) ? fg : bg;
                slice >>= 1;
            }
        }
    }
}

void outrun_screen_bar(int row, int percent, uint16_t fg, uint16_t bg)
{
    /* "[" + 28 cells + "]" is exactly the 30-column window. */
    const int cells = OUTRUN_TEXT_COLS - 2;
    char buf[OUTRUN_TEXT_COLS + 1];

    if (percent < 0)
    {
        percent = 0;
    }
    if (percent > 100)
    {
        percent = 100;
    }
    const int filled = (percent * cells) / 100;

    buf[0] = '[';
    for (int i = 0; i < cells; i++)
    {
        buf[1 + i] = (i < filled) ? '#' : '.';
    }
    buf[1 + cells] = ']';
    buf[2 + cells] = '\0';

    outrun_screen_text(0, row, buf, fg, bg);
}
