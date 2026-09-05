/*
 * Audio output for picoOutRun.
 *
 * Upstream's SDL Audio class mixes the YM2151 and SegaPCM streams and feeds an
 * SDL device, adjusting the frame rate to keep video synced to audio. Here the
 * mix is the same but the sink is whichever one pico_shared gives this board -
 * the HDMI data island on HSTX, the PicoDVI audio ring, or I2S/TLV320 - and
 * frames are paced by Frens::PaceFrames60fps instead.
 *
 * WHY THERE IS A RING IN BETWEEN. SoundChip::init() sizes its buffer as
 * rate/fps, so at 44100 Hz and 30 fps the engine hands over 1470 stereo frames
 * in one burst, once per engine tick. No sink here can swallow that: the HSTX
 * data-island queue holds about 800 samples (HSTX_AUDIO_DI_HIGH_WATERMARK * 4,
 * ~18 ms) and silently DROPS anything above the watermark. So the burst lands in
 * this ring and is drained a slice at a time, every displayed frame.
 */

#include <algorithm>

#include "FrensHelpers.h"
#include "settings.h"

#include "engine/audio/osoundint.hpp"
#include "frontend/config.hpp"
#include "outrun_audio.hpp"
#include "outrun_hot.hpp"

#if HSTX
#include "drivers/pico_hdmi/video_output.h"
#endif

/* What the HDMI data island / I2S sink actually runs at. hstx_init() fixes it
 * at 44100, and 22050 is not a valid HDMI rate, so the sink stays here and
 * port/audio.cpp upsamples into it. */
#define OUTRUN_SINK_RATE 44100u

/* Power of two, in stereo frames.
 *
 * Must comfortably exceed one engine burst PLUS whatever the sink could not
 * take from the previous one. At config.fps=30 a burst is 735 source frames ->
 * 1470 ring frames after the 2x upsample, while the HDMI data-island queue
 * accepts at most ~576 samples at a time. So ~894 frames routinely carry over,
 * and 2048 overflowed whenever a burst landed on top of that: `dropped` hit
 * 11,300/sec while `starved` stayed at 0. Enlarging this to 4096 did NOT fix
 * that - the real cause was the undersized HDMI data-island queue - so it is
 * back to 2048, which fits the budget.
 *
 * (At config.fps=60 bursts were half the size, which is why this only appeared
 * when the engine went back to the original 30 Hz rate.) */
#define RING_FRAMES 2048
#define RING_MASK (RING_FRAMES - 1)

static int16_t ring[RING_FRAMES * 2];
static volatile uint32_t ring_head; // write position, in frames
static volatile uint32_t ring_tail; // read position, in frames

// Diagnostics. `dropped` counting up means the engine is producing faster than
// the sink drains; `starved` means the opposite, and is what bad sound sounds
// like - the sink running dry between bursts.
static uint32_t stat_pushed, stat_dropped, stat_starved;

static inline uint32_t ring_avail(void)
{
    return ring_head - ring_tail;
}

/* ---------------------------------------------------------------------------
 * Core1 offload.
 *
 * Audio has to advance at a fixed 60 Hz. Driving it from core0's frame loop tied
 * it to the renderer, and when that fell to ~28 fps the engine produced ~20,000
 * samples/sec against the 44,100 the sink consumes - which is what "awful sound"
 * was. Nothing in the audio path was wrong; it was simply being called too
 * seldom.
 *
 * So the whole sound chain now runs on core1, in the spare cycles of the HSTX
 * driver's supervisor loop, paced by the display's own frame counter. Core0's
 * frame rate no longer affects audio at all.
 *
 * Cross-core state is deliberately minimal: everything below - osound, the
 * YM2151, the SegaPCM, pcm_ram and this ring - is touched only by core1 once
 * running. The single crossing is OSoundInt's sound-command queue, which core0
 * writes via queue_sound() and core1 drains. That is a single-producer/
 * single-consumer byte queue; a torn index there costs a wrong or missed sound
 * effect, never a crash.
 * ------------------------------------------------------------------------- */

static volatile bool core1_audio_enabled;
static uint32_t core1_last_frame;

static void OUTRUN_HOT(core1_audio_task)(void)
{
    if (!core1_audio_enabled)
    {
        return;
    }

    /* Pace off the DMA-IRQ frame counter: a true 60 Hz source, and the only
     * clock here that does not depend on how fast core0 is rendering.
     *
     * Divided down to config.fps, because SoundChip sizes its buffer as
     * rate/config.fps - at 30 the engine emits twice as many samples per tick,
     * so it must be ticked half as often. Getting this wrong is what forced
     * config.fps to 60 earlier, which in turn changed the engine's timing. */
    const uint32_t div = (config.fps >= 60) ? 1u : 2u;

    uint32_t f = hstx_getframecounter() / div;
    if (f == core1_last_frame)
    {
        outrun_audio_pump(); // keep the sink topped up between frames
        return;
    }
    /* Frames core1 failed to service. An empty ring at the boundary is NOT a
     * problem - that is the steady state once the pump has drained everything,
     * and counting it reported ~60 "starvations" a second while throughput was
     * a perfect 44,100. What matters is the display counter advancing by more
     * than one between visits: those frames' audio is never generated. */
    uint32_t missed = f - core1_last_frame;
    if (missed > 1)
    {
        stat_starved += missed - 1;
    }
    core1_last_frame = f;

    /* Drain BEFORE generating, so the burst lands in the emptiest ring we can
     * arrange rather than on top of the previous one's remainder. */
    outrun_audio_pump();

    osoundint.tick();    // run the ported Z80 sound program
    outrun_audio_frame(); // render YM2151 + SegaPCM and mix into the ring
    outrun_audio_pump();
}

void outrun_audio_start_core1(void)
{
    core1_last_frame = hstx_getframecounter() / ((config.fps >= 60) ? 1u : 2u);
    core1_audio_enabled = true;
    /* pico_hdmi already provides this hook for exactly this purpose - its own
     * comment says "typically used for audio processing". It runs at the end of
     * core1's supervisor loop. No pico_shared change is needed. */
    video_output_set_background_task(core1_audio_task);
}

void outrun_audio_reset(void)
{
    ring_head = ring_tail = 0;
    stat_pushed = stat_dropped = stat_starved = 0;
}

void outrun_audio_stats(uint32_t *pushed, uint32_t *dropped, uint32_t *starved, uint32_t *level)
{
    *pushed = stat_pushed;
    *dropped = stat_dropped;
    *starved = stat_starved;
    *level = ring_avail();
    stat_pushed = stat_dropped = stat_starved = 0;
}

// Called once per ENGINE tick (30 Hz by default), after osoundint.tick().
void OUTRUN_HOT(outrun_audio_frame)(void)
{
    if (!config.sound.enabled)
    {
        return;
    }

    osoundint.pcm->stream_update();
    osoundint.ym->stream_update();

    const int16_t *pcm = osoundint.pcm->get_buffer();
    const int16_t *ym = osoundint.ym->get_buffer();

    /* buffer_size counts interleaved samples, i.e. frames * channels. Both
     * chips are initialised STEREO at the same rate and fps so these match, but
     * take the smaller rather than trusting that and reading off the end. */
    const uint32_t samples =
        std::min(osoundint.pcm->buffer_size, osoundint.ym->buffer_size);

    /* The chips are emulated at config.sound.rate but the sink runs at
     * OUTRUN_SINK_RATE, so each frame is repeated `up` times on the way out.
     * Sample-and-hold, not interpolation: it costs nothing, and at 2x the
     * difference is inaudible next to the YM2151's own quantisation. */
    const uint32_t up = OUTRUN_SINK_RATE / (uint32_t)config.sound.rate;

    for (uint32_t i = 0; i + 1 < samples; i += 2)
    {
        if (ring_avail() + up >= RING_FRAMES)
        {
            stat_dropped += (samples - i) / 2;
            break; // sink is not keeping up; drop the tail of this burst
        }

        int32_t l = (int32_t)pcm[i] + (int32_t)ym[i];
        int32_t r = (int32_t)pcm[i + 1] + (int32_t)ym[i + 1];

        // Same clip as upstream's mix loop.
        l = std::min<int32_t>(std::max<int32_t>(l, INT16_MIN), INT16_MAX);
        r = std::min<int32_t>(std::max<int32_t>(r, INT16_MIN), INT16_MAX);

        for (uint32_t k = 0; k < up; k++)
        {
            uint32_t w = (ring_head & RING_MASK) * 2;
            ring[w] = (int16_t)l;
            ring[w + 1] = (int16_t)r;
            ring_head++;
        }
    }
}

// Called every DISPLAYED frame. Moves whatever the sink will accept.
void OUTRUN_HOT(outrun_audio_pump)(void)
{
    uint32_t avail = ring_avail();
    if (!avail)
    {
        return;
    }

#if EXT_AUDIO_IS_ENABLED
    if (settings.flags.useExtAudio)
    {
        uint32_t space = (uint32_t)EXT_AUDIO_GET_FREE();
        uint32_t n = std::min(avail, space);
        for (uint32_t i = 0; i < n; i++)
        {
            uint32_t rd = (ring_tail & RING_MASK) * 2;
            EXT_AUDIO_ENQUEUE_SAMPLE(ring[rd], ring[rd + 1]);
            ring_tail++;
            stat_pushed++;
        }
        return;
    }
#endif

#if HSTX
    /* The data-island queue drops silently once it is over the watermark, so
     * only push what is genuinely free rather than letting it discard. */
    uint32_t level = hstx_di_queue_get_level();
    if (level >= HSTX_AUDIO_DI_HIGH_WATERMARK)
    {
        return;
    }
    uint32_t space = (HSTX_AUDIO_DI_HIGH_WATERMARK - level) * 4;
    uint32_t n = std::min(avail, space);
    for (uint32_t i = 0; i < n; i++)
    {
        uint32_t rd = (ring_tail & RING_MASK) * 2;
        hstx_push_audio_sample(ring[rd], ring[rd + 1]);
        ring_tail++;
        stat_pushed++;
    }
#else
    auto &rb = dvi_->getAudioRingBuffer();
    uint32_t n = std::min<uint32_t>(avail, rb.getWritableSize());
    if (!n)
    {
        return;
    }
    auto *p = rb.getWritePointer();
    for (uint32_t i = 0; i < n; i++)
    {
        uint32_t rd = (ring_tail & RING_MASK) * 2;
        (*p)[0] = ring[rd];
        (*p)[1] = ring[rd + 1];
        p++;
        ring_tail++;
        stat_pushed++;
    }
    rb.advanceWritePointer(n);
#endif
}
