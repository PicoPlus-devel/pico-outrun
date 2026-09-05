/*
 * picoOutRun's replacement for cannonball/src/main/sdl2/audio.hpp.
 *
 * Upstream's Audio owns an SDL audio device and mixes the YM2151 and SegaPCM
 * output into it, adjusting the frame rate to keep video synced to audio.
 * picoOutRun does neither: frames are paced by Frens::PaceFrames60fps, and the
 * mixed output goes to whichever pico_shared sink the board has - the HDMI data
 * island on HSTX, the PicoDVI audio ring, or I2S/TLV320 - from port/or_audio.
 *
 * This is essentially upstream's own non-SDL fallback (the #else branch of
 * COMPILE_SOUND_CODE), which exists for exactly this situation. The engine only
 * ever calls load_wav() and clear_wav(), both for external WAV music tracks that
 * this port does not carry.
 */

#pragma once

#include "globals.hpp"

class Audio
{
public:
    bool sound_enabled = false;

    Audio() = default;
    ~Audio() = default;

    void init() {}
    void tick() {}
    void start_audio() {}
    void stop_audio() {}
    double adjust_speed() { return 1.0; }

    // External WAV tracks are not supported: there is no filesystem the music
    // could come from, since the SD card is optional and holds no game data.
    void load_wav(const char * /*filename*/) {}
    void clear_wav() {}
};
