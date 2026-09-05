/*
 * The two entry points main.cpp uses to drive the Cannonball engine.
 * See port/glue.cpp.
 */

#pragma once

// Binds the flash data image to the engine and boots it. False means there is
// no usable game data - main.cpp shows the "no game data" screen instead.
bool outrun_engine_init(void);

// One frame: controls, engine logic, sound, and the render into pico_shared's
// framebuffer. Call once per displayed frame.
void outrun_engine_tick(void);

// Restart the game from the beginning (settings menu -> Reset Game).
void outrun_engine_reset(void);

/* FPS overlay. outrun_fps_set() takes the two rates the once-a-second report
 * already computes; outrun_fps_overlay() paints them if the setting is on. */
void outrun_fps_set(unsigned long loop_fps, unsigned long drawn_fps);
void outrun_fps_overlay(void);
