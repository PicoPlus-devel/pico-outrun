/*
 * picoOutRun's replacement for cannonball/src/main/frontend/config.hpp.
 *
 * Upstream's Config loads and saves config.xml through Boost property_tree and
 * drives the SDL frontend menu. picoOutRun has neither: it boots straight into
 * attract mode, and user-facing settings come from the pico_shared menu. So the
 * data layout below is upstream's, member for member, with the XML, the hi-score
 * files and the frontend plumbing removed and the values fixed at their
 * upstream defaults (config.cpp's load()).
 *
 * It is reached as "frontend/config.hpp" because port/ is on the include path
 * AHEAD of cannonball/src/main/, so the vendored engine compiles unchanged.
 *
 * No Pico dependencies here on purpose: hosttest/ builds this same file for the
 * host, so the tests and the firmware agree on every setting.
 */

#pragma once

#include <string>
#include <vector>

#include "stdint.hpp"

struct data_settings_t
{
    /* Upstream reads tilemap.bin / tilepatch.bin from res_path at runtime.
     * picoOutRun has no filesystem for them; see port/ for how they are reached. */
    std::string res_path;
    int crc32 = 0;
};

struct music_t
{
    const static int IS_YM_INT = 0; // Internal YM track (from the OutRun ROMs)
    const static int IS_YM_EXT = 1; // External YM track (from a binary)
    const static int IS_WAV = 2;    // External WAV track

    int type = IS_YM_INT;
    int cmd = 0;
    std::string title;
    std::string filename;
};

struct ttrial_settings_t
{
    int laps = 5;
    int traffic = 3;
    uint16_t best_times[15] = {};
};

struct menu_settings_t
{
    int enabled = 0; // no frontend menu in this port
    int road_scroll_speed = 50;
};

struct video_settings_t
{
    const static int MODE_WINDOW = 0;
    const static int MODE_FULL = 1;
    const static int MODE_STRETCH = 2;

    int mode = MODE_FULL;
    int scale = 1;
    int scanlines = 0;   // pico_shared applies its own scanline effect
    int widescreen = 0;  // 320x224; keeps s16_x_off at 0
    /* 0 = 30fps, 1 = 60fps with 30fps sprite tick, 2 = 60fps throughout.
     *
     * MUST be a 60fps mode here, and it is an audio requirement before it is a
     * smoothness one. SoundChip sizes its buffer as rate/fps, so at 0 the engine
     * emits 1470 samples per tick and has to tick exactly 30x/sec to make
     * 44100 - with no margin, and pico_shared paces the display at 60 Hz, not
     * 30. At 60 it emits 735 per tick, once per displayed frame, which lines up
     * with the pacing exactly.
     *
     * 1, NOT upstream's default of 2. The difference is which branch of
     * Outrun::tick() runs:
     *
     *   fps==60, tick_fps==30 (this)  jump_table() + oroad.tick() on alternate
     *                                 frames, vint() every frame. Upstream's
     *                                 own comment: "the same as the original
     *                                 game".
     *   fps==60, tick_fps==60 (2)     "Smooth Mode" - game logic EVERY frame,
     *                                 i.e. double the original logic rate.
     *
     * Smooth Mode is an enhancement that assumes a solid 60 Hz loop. This port
     * delivers an irregular 36-60 with frame skip on top, and running OutRun's
     * logic at double rate under that produced visibly wrong motion - the road
     * appearing to scroll backwards at times.
     *
     * It is also cheaper, which the CPU-bound renderer wants anyway. The audio
     * rate depends on config.fps (60) and is unaffected by either choice. */
    /* 0 = 30 fps, the original arcade logic rate, and the value this port ran
     * before the audio work.
     *
     * It was changed to a 60 fps mode ONLY to make the sample count work out:
     * SoundChip sizes its buffer as rate/config.fps, and core1 was pacing audio
     * off the 60 Hz display counter. That was an audio fix that silently altered
     * game timing - it moves Outrun::tick() to a different branch and doubles
     * the rate the engine expects - and it is when the road started appearing to
     * scroll backwards at times.
     *
     * port/audio.cpp now divides its pacing by config.fps instead, so 30 is once
     * again free to be the right answer for the GAME rather than a constraint
     * imposed by the sound. */
    int fps = 0;
    int fps_count = 0;
    int hires = 0; // doubles the render resolution; far out of budget here
    int filtering = 0;
    int vsync = 1;
    int shadow = 0;
};

struct sound_settings_t
{
    int enabled = 1;
    /* The rate the YM2151 and SegaPCM are EMULATED at - not the rate the sink
     * runs at, which stays 44100. port/audio.cpp duplicates each frame on the
     * way out.
     *
     * Emulating the YM2151 is the single biggest cost on core1: 32 operators x
     * rate samples/sec. At 44100 core1 could only manage ~41 of the 60 frames
     * it owes per second, so the ring ran dry roughly 40 times a second - that
     * is what the bad sound was. Halving the rate halves that work.
     *
     * NOTE: port/ym2151_luts.h is generated FOR THIS RATE. Change it and you
     * must re-run ./hosttest/build.sh lutgen, or the pitch will be wrong;
     * `build.sh check` fails loudly if the two drift apart. */
    int rate = 22050;
    int advertise = 1;
    int preview = 1;
    /* Upstream defaults to 1, which needs the alternate opr-10188.71f sample
     * ROM. picoOutRun packs the standard set, so this stays 0. */
    int fix_samples = 0;
    /* globals.hpp MUSIC_TIMER: 30 seconds, BCD-encoded. Upstream substitutes
     * this whenever config.xml leaves the value at 0, so 0 is NOT a valid
     * default here - omusic feeds it straight to the countdown. */
    int music_timer = 0x30;
    /* The three built-in YM tracks. Populated by Config::Config(), exactly as
     * upstream does - this list is NOT read from config.xml, which is why
     * defaulting every other field from the XML did not cover it.
     * OMusic::play_music() does config.sound.music.at(index); leaving it empty
     * threw std::out_of_range and aborted the moment START was pressed. */
    std::vector<music_t> music;
};

struct controls_settings_t
{
    const static int GEAR_BUTTON = 0;
    const static int GEAR_PRESS = 1;    // for cabinets
    const static int GEAR_SEPARATE = 2; // separate button presses
    const static int GEAR_AUTO = 3;

    int gear = GEAR_BUTTON;
    int steer_speed = 3; // digital steering ramp
    int pedal_speed = 4; // digital pedal ramp
    int padconfig[15] = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
    int keyconfig[12] = {};
    int pad_id = 0;
    int analog = 0;
    int axis[4] = {-1, -1, -1, -1};
    int asettings[2] = {75, 0}; // wheel zone, dead zone
    bool invert[3] = {};

    float rumble = 0.0f; // no rumble path on this hardware
    int haptic = 0;
    int max_force = 9000;
    int min_force = 8500;
    int force_duration = 20;
};

struct smartypi_settings_t
{
    int enabled = 0; // arcade cabinet I/O board; not present
    int ouputs = 0;  // upstream's spelling
    int cabinet = 1; // CABINET_UPRIGHT
};

struct engine_settings_t
{
    int dip_time = 0;
    int dip_traffic = 1;
    bool freeplay = true; // no coin slot on a Pico
    bool freeze_timer = false;
    bool disable_traffic = false;
    int jap = 0;
    int prototype = 0;
    int randomgen = 1;
    int level_objects = 1;
    bool fix_bugs = true;
    bool fix_bugs_backup = true;
    bool fix_timer = false;
    bool layout_debug = false;
    bool hiscore_delete = false;
    int hiscore_timer = 0;
    int new_attract = 1;
    bool grippy_tyres = false;
    bool offroad = false;
    bool bumper = false;
    bool turbo = false;
    int car_pal = 0;
};

class Config
{
public:
    data_settings_t data;
    menu_settings_t menu;
    video_settings_t video;
    sound_settings_t sound;
    controls_settings_t controls;
    engine_settings_t engine;
    ttrial_settings_t ttrial;
    smartypi_settings_t smartypi;

    const static int CABINET_MOVING = 0;
    const static int CABINET_UPRIGHT = 1;
    const static int CABINET_MINI = 2;

    // Internal screen width and height (globals.hpp S16_WIDTH / S16_HEIGHT)
    uint16_t s16_width = 320;
    uint16_t s16_height = 224;

    // Non-zero only in widescreen, which this port does not use. Several
    // behaviours key off it - notably hwtiles::patch_tiles, which therefore
    // never runs here.
    uint16_t s16_x_off = 0;

    // 30 or 60: the engine's frame rate
    int fps = 30;

    // The original ticks sprites at 30fps but scrolls the background at 60fps
    int tick_fps = 30;

    // Continuous mode traffic setting
    int cont_traffic = 3;

    Config();
    ~Config() = default;

    void set_fps(int fps);
    void inc_time();
    void inc_traffic();

    // Hi-scores. Upstream reads and writes XML; picoOutRun persists them
    // through pico_shared's flash parameters instead. Stubs for now.
    void load_scores(bool original_mode);
    void save_scores(bool original_mode);
    bool clear_scores();
};

extern Config config;
