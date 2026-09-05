/*
 * The globals cannonball/src/main/main.cpp would have defined, and the engine
 * boot sequence, for picoOutRun.
 *
 * Upstream's main.cpp owns the SDL window, the event pump and the frame loop.
 * picoOutRun's main.cpp owns none of that - pico_shared does - so what is left
 * is this: the `cannonball::` namespace globals the engine reads, and the two
 * entry points main.cpp calls, outrun_engine_init() and outrun_engine_tick().
 */

#include "glue.hpp"

#include <cstdio>

#include "frontend/config.hpp"
#include "globals.hpp"
#include "main.hpp"
#include "roms.hpp"
#include "video.hpp"

#include "engine/audio/osoundint.hpp"
#include "engine/oinputs.hpp"
#include "engine/outrun.hpp"

#include "FrensHelpers.h"
#include "outrun_audio.hpp"
#include "outrun_data.h"
#include "sdl2/input.hpp"

#if HSTX
#include "drivers/pico_hdmi/video_output.h"
#endif

namespace cannonball
{
    Audio audio;
    int frame = 0;
    bool tick_frame = true;
    double frame_ms = 0;
    int fps_counter = 0;
    int state = STATE_BOOT;
}

// Point the engine's RomLoader instances at the flash data image. Upstream fills
// these by reading files; here every region is already decoded and resident, so
// they only need a pointer and a length.
//
// tiles, sprites and road are absent on purpose: video.cpp takes those straight
// from flash under OUTRUN_GFX_IN_FLASH and never looks at roms.
static bool bind_roms(void)
{
    struct
    {
        RomLoader *dst;
        outrun_region_t region;
        const char *name;
    } bind[] = {
        {&roms.rom0, OUTRUN_REGION_ROM0, "rom0"},
        {&roms.rom1, OUTRUN_REGION_ROM1, "rom1"},
        {&roms.z80, OUTRUN_REGION_Z80, "z80"},
        {&roms.pcm, OUTRUN_REGION_PCM, "pcm"},
    };

    for (auto &b : bind)
    {
        uint32_t size = 0;
        const uint8_t *p = outrun_data_region(b.region, &size);
        if (!p)
        {
            printf("outrun: region %s missing from the flash data image\n", b.name);
            return false;
        }
        b.dst->set_flash(p, size);
    }

    /* rom0p/rom1p select the world or Japanese ROM set; outrun.cpp assigns
     * them itself (it points them at j_rom0/j_rom1 when config.engine.jap is
     * set, which this port leaves at 0).
     *
     * res/tilemap.bin and res/tilepatch.bin are not bound either: both are only
     * read by OMusic::load_widescreen_map(), which nothing in this subset calls
     * - upstream drove it from the main.cpp that picoOutRun replaces, and it is
     * a widescreen-only path regardless. */

    return true;
}

bool outrun_engine_init(void)
{
    if (!outrun_data_valid())
    {
        return false;
    }
    if (!bind_roms())
    {
        return false;
    }

    config.set_fps(config.video.fps);

    input.init();

    if (!video.init(&roms, &config.video))
    {
        printf("outrun: video.init failed\n");
        return false;
    }

    Frens::dumpHeapStats("after video.init");

    // set_fps() deliberately does not do this - see port/config.cpp.
    osoundint.init();

    Frens::dumpHeapStats("after osound.init");

    outrun_audio_reset();

    outrun.init();

#if HSTX
    /* Sound runs on core1 from here on - core0 must not touch osoundint, the
     * YM2151 or the SegaPCM again, only queue_sound(). See port/audio.cpp. */
    outrun_audio_start_core1();
#endif
    cannonball::state = cannonball::STATE_GAME;
    return true;
}

void outrun_engine_reset(void)
{
    outrun.init();
    cannonball::state = cannonball::STATE_GAME;
}

// One iteration of upstream's tick(), minus the SDL event pump and the menu and
// time-trial states, which this port does not carry.
void outrun_engine_tick(void)
{
    using namespace cannonball;

    frame++;

    /* This is called once per DISPLAYED frame, and pico_shared paces the display
     * at 60 Hz. Upstream instead paced its whole main loop at config.fps, so
     * simply ticking the engine on every call ran it at double speed when
     * config.fps is 30 (the default here).
     *
     * At 30: run the engine every other displayed frame.
     * At 60: run every frame, but tick sprite logic on alternate ones, which is
     *        upstream's own behaviour (the original scrolls the background at
     *        60 but ticks sprites at 30). */
    bool run_engine;
    if (config.fps == 60)
    {
        run_engine = true;
        tick_frame = frame & 1;
    }
    else
    {
        run_engine = (frame & 1) == 0;
        tick_frame = true;
    }

    /* Adaptive frame skip.
     *
     * Game logic and (on HSTX) audio must advance 60 times a second or the game
     * runs slow and the sound rate collapses. Rendering is what actually costs
     * too much - measured at ~24 fps in traffic. So when the previous iteration
     * overran its 16.7 ms budget, run the logic but skip the draw.
     *
     * MAX_SKIP bounds it so the picture can never stall completely.
     *
     * THIS IS THE VIDEO/SPEED TRADE, and it is the one knob worth touching.
     *
     * Core0 is saturated: measured at both loop=60/drawn=20 and loop=36/drawn=12
     * it is doing the same total work per second. So every extra logic tick is
     * paid for in drawn frames, and the only question is which to protect.
     *
     * A varying loop rate means the game world speeds up and slows down with
     * scene complexity, which is worse than being uniformly slower - so the
     * limit is set to protect the 60 Hz logic rate and let the picture give.
     *   MAX_SKIP 1 -> render 1 in 2, loop fell to ~40 in traffic
     *   MAX_SKIP 2 -> render 1 in 3, loop 36-60 (still scene-dependent)
     *   MAX_SKIP 3 -> render 1 in 4, ~15 fps floor, steadiest game speed
     * Lower it if the choppiness bothers you more than the speed variation. */
    static uint32_t deadline_us;
    static int skipped;
    const uint32_t FRAME_US = 1000000u / 60u;
    /* TEMPORARILY 0 - isolation test for the "driving in reverse" report.
     *
     * 0 disables frame skip entirely: every engine tick is rendered, exactly as
     * upstream does it. The loop rate will drop (expect 20-30) and the game will
     * run slow, but render and tick are locked together again.
     *
     * If the reverse-scrolling STOPS, decoupling render from tick is implicated
     * and the skip logic needs rethinking. If it PERSISTS, frame skip is
     * innocent and the cause is the variable engine rate itself - the engine
     * assumes a fixed 60 Hz and we deliver 36-60.
     *
     * Put this back to 3 either way once we know. */
    const int MAX_SKIP = 0;

    uint32_t now_us = (uint32_t)Frens::time_us();
    if (deadline_us == 0)
    {
        deadline_us = now_us;
    }

    /* Judge lateness against an ACCUMULATED deadline, not against how long the
     * previous iteration took. Measuring the previous one cannot work: a
     * skipped frame is cheap, so the next always looks on time and renders
     * again. That capped it at alternating render/skip - drawn came out at
     * exactly loop/2 in every sample, however high MAX_SKIP was set. */
    bool do_render = true;
    if ((int32_t)(now_us - deadline_us) > 0 && skipped < MAX_SKIP)
    {
        do_render = false;
        skipped++;
    }
    else
    {
        skipped = 0;
    }

    deadline_us += FRAME_US;

    /* If we fall badly behind - a settings menu, a stall - forgive the debt
     * rather than skipping frames forever trying to repay it. */
    if ((int32_t)(now_us - deadline_us) > (int32_t)(4u * FRAME_US))
    {
        deadline_us = now_us;
    }

    if (run_engine)
    {
        if (tick_frame)
        {
            oinputs.tick();    // controls
            oinputs.do_gear(); // digital gear
        }

        outrun.tick(tick_frame);
        if (tick_frame)
        {
            input.frame_done();
        }

#if !HSTX
        /* PicoDVI: no core1 background-task hook, so the sound chain stays on
         * core0 and is therefore only as steady as the frame rate. */
        osoundint.tick();
        outrun_audio_frame();
#endif

        if (do_render)
        {
            video.prepare_frame();
            video.render_frame();
        }
    }

#if !HSTX
    outrun_audio_pump();
#endif

    /* Once a second, report what the loop is actually achieving. `fps` is the
     * real displayed rate - if it is not ~60 the audio rate cannot be right,
     * because the engine emits rate/config.fps samples per tick. `pushed`
     * should sit at ~44100. */
    static uint32_t last_us, frames_since, drawn_since;
    frames_since++;
    if (do_render && run_engine)
    {
        drawn_since++;
    }
    uint32_t now = (uint32_t)Frens::time_us();
    if (now - last_us >= 1000000u)
    {
        uint32_t pushed, dropped, starved, level;
        outrun_audio_stats(&pushed, &dropped, &starved, &level);
        printf("[outrun] loop=%lu drawn=%lu audio pushed=%lu dropped=%lu starved=%lu resync=%d\n",
               (unsigned long)frames_since, (unsigned long)drawn_since, (unsigned long)pushed,
               (unsigned long)dropped, (unsigned long)starved, get_video_output_resync_count());
        frames_since = 0;
        drawn_since = 0;
        last_us = now;
    }
}
