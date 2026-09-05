/*
 * The behaviour behind port/frontend/config.hpp.
 *
 * Only the three methods the vendored engine actually calls, plus hi-score
 * stubs. Transcribed from cannonball/src/main/frontend/config.cpp, minus the
 * XML and the SDL audio restart.
 *
 * Deliberately free of Pico dependencies: hosttest/ compiles this file too.
 */

#include "frontend/config.hpp"

#include "engine/audio/commands.hpp"

Config config;

/* Transcribed from cannonball/src/main/frontend/config.cpp's constructor. The
 * three built-in tracks are added in code rather than loaded from config.xml,
 * so dropping the XML frontend silently dropped them too. */
Config::Config(void)
{
    music_t magical, breeze, splash;
    magical.title = "MAGICAL SOUND SHOWER";
    breeze.title = "PASSING BREEZE";
    splash.title = "SPLASH WAVE";
    magical.type = music_t::IS_YM_INT;
    breeze.type = music_t::IS_YM_INT;
    splash.type = music_t::IS_YM_INT;
    magical.cmd = sound::MUSIC_MAGICAL;
    breeze.cmd = sound::MUSIC_BREEZE;
    splash.cmd = sound::MUSIC_SPLASH;
    sound.music.push_back(magical);
    sound.music.push_back(breeze);
    sound.music.push_back(splash);
}

/* Config::set_fps(). Upstream also updates cannonball::frame_ms and stops,
 * re-inits and restarts SDL audio around osoundint.init().
 *
 * Neither belongs here. picoOutRun paces frames through Frens::PaceFrames60fps
 * and keeps its audio sink running, and nothing in the vendored engine calls
 * set_fps - every call site is our own - so THE CALLER re-inits the sound
 * engine if it changes the rate while running. Keeping the dependency out means
 * this file stays free of engine and Pico headers, which is what lets
 * hosttest/ build the firmware's real config instead of a copy. */
void Config::set_fps(int fps)
{
    video.fps = fps;

    // Core rate: 30fps or 60fps
    this->fps = video.fps == 0 ? 30 : 60;

    // The original ticks sprites at 30fps but scrolls the background at 60fps
    tick_fps = video.fps < 2 ? 30 : 60;
}

void Config::inc_time()
{
    if (engine.dip_time == 3)
    {
        if (!engine.freeze_timer)
        {
            engine.freeze_timer = 1;
        }
        else
        {
            engine.dip_time = 0;
            engine.freeze_timer = 0;
        }
    }
    else
    {
        engine.dip_time++;
    }
}

void Config::inc_traffic()
{
    if (engine.dip_traffic == 3)
    {
        if (!engine.disable_traffic)
        {
            engine.disable_traffic = 1;
        }
        else
        {
            engine.dip_traffic = 0;
            engine.disable_traffic = 0;
        }
    }
    else
    {
        engine.dip_traffic++;
    }
}

/* Hi-scores. Upstream keeps them in config.xml. picoOutRun will persist them
 * through pico_shared's flash parameters - there is no filesystem guarantee,
 * since the SD card is optional. Stubbed until the engine runs. */
void Config::load_scores(bool /*original_mode*/)
{
}

void Config::save_scores(bool /*original_mode*/)
{
}

bool Config::clear_scores()
{
    return false;
}
