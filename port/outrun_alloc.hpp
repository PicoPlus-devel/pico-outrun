/*
 * Allocator for the engine's large buffers.
 *
 * WHY THIS EXISTS. picoOutRun does not fit in RP2350 SRAM. Measured on a Fruit
 * Jam: ~290 KB of static RAM, plus ~90 KB that Video's *global constructor*
 * allocates before main() even runs (Render's 16 KB palette LUT, hwtiles' 68 KB
 * of tile/text RAM, hwsprites), plus Video::pixels at 143 KB. The first casualty
 * was pixels - video.cpp:59 panicked with 1 KB of arena left.
 *
 * So the biggest buffers go to PSRAM through Frens::f_malloc. That is a real
 * constraint, not a preference: **picoOutRun requires a board with PSRAM
 * fitted.** Boards without it cannot run this port.
 *
 * Frens::f_malloc panics rather than returning NULL when PSRAM is exhausted, and
 * falls back to plain malloc when no PSRAM is present - on such a board the
 * failure is the same panic, just later.
 */

#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C"
{
#endif

/* PSRAM (via Frens::f_malloc). For big, comparatively cold buffers. An access
 * that misses the XIP cache costs ~100 cycles, so nothing on a per-pixel path
 * belongs here. */
void *outrun_psram_alloc(size_t bytes);
void outrun_psram_free(void *p);

/* SRAM (plain malloc). For anything touched per pixel or per scanline. */
void *outrun_sram_alloc(size_t bytes);
void outrun_sram_free(void *p);

#ifdef __cplusplus
}
#endif
