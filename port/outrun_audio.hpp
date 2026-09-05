/*
 * The audio bridge between the Cannonball sound engine and pico_shared's sinks.
 * See port/audio.cpp.
 */

#pragma once

// Mix one engine frame (YM2151 + SegaPCM) into the ring. Call at the ENGINE
// rate, right after osoundint.tick().
void outrun_audio_frame(void);

// Drain the ring into whichever sink this board has. Call every DISPLAYED
// frame, so a 30 Hz burst is spread over the 60 Hz output.
void outrun_audio_pump(void);

void outrun_audio_reset(void);

/* Hand the sound chain to core1, paced by the display frame counter, so audio
 * keeps its rate no matter what core0's renderer manages. HSTX only - the
 * PicoDVI path has no equivalent spare-cycle hook. */
void outrun_audio_start_core1(void);

/* Diagnostics, reset on each read. `pushed` should settle at the sample rate
 * (44100/s); `starved` climbing is the sink running dry, which is what bad
 * sound sounds like. */
void outrun_audio_stats(uint32_t *pushed, uint32_t *dropped, uint32_t *starved, uint32_t *level);
