/*
 * OUTRUN_HOT - place a function in SRAM instead of flash.
 *
 * The engine's inner render loops run from flash over XIP, and every
 * instruction-fetch miss stalls core0 while it is already the bottleneck.
 * Moving just the lores render path costs ~10 KB of SRAM and takes the fetches
 * off the critical path. (The DATA those loops read - 1 MB of sprites, 257 KB
 * of road - has to stay in flash; there is nowhere else for it.)
 *
 * A no-op off-device, so hosttest/ still builds these same files.
 */

#pragma once

/* OFF BY DEFAULT. Enable with -DOUTRUN_HOT_IN_SRAM=1.
 *
 * It costs ~10 KB of SRAM, and that is not spare: paying for it meant shrinking
 * the HSTX data-island queue, which starved the HDMI audio sink and made the
 * sound noticeably worse. Its own benefit was never measured in isolation, so
 * it is the wrong side of that trade until it is.
 *
 * Re-enable only alongside a plan for where the 10 KB comes from - NOT from the
 * audio buffering. */
#if defined(PICO_ON_DEVICE) && defined(OUTRUN_HOT_IN_SRAM)
#include "pico.h"
#define OUTRUN_HOT(f) __not_in_flash_func(f)
#else
#define OUTRUN_HOT(f) f
#endif
