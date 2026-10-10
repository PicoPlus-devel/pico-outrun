/*
 * outrun_host - run the Cannonball engine on Linux and dump frames.
 *
 *   outrun_host <outrun-data.bin> <frames> <dump-every-N> [outdir] [options]
 *
 * Options:
 *   --start F    press Start at frame F (held for 8 frames)
 *   --accel F    hold the accelerator from frame F on
 *   --scale N    write each frame N times its size (default 1)
 *
 * The engine, video.cpp and the decoded data image are the firmware's own; the
 * data image is the one tools/mkoutrundata writes, adopted through the same
 * outrun_data_adopt_psram() the SD-card route uses. What is replaced is only
 * what port/glue.cpp, port/render.cpp and port/alloc.cpp take from pico_shared:
 * the frame loop, the output framebuffer and the PSRAM allocator.
 *
 * Every dumped frame is outdir/frame_NNNNN.ppm, 320x224 times --scale, in the
 * RGB555 colours an HSTX board shows. Frames are counted at 60 per second, the
 * rate pico_shared displays them, so frame 600 is ten seconds after boot.
 * Convert them with hosttest/ppm2png.py.
 *
 * The README screenshots in docs/screenshots/ come from two runs:
 *   outrun_host outrun-data.bin 7200 60 out/attract --scale 2
 *   outrun_host outrun-data.bin 3000 600 out/game --start 300 --accel 1500 --scale 2
 */

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/stat.h>
#include <vector>

#include "frontend/config.hpp"
#include "globals.hpp"
#include "main.hpp"
#include "roms.hpp"
#include "video.hpp"

#include "engine/audio/osoundint.hpp"
#include "engine/oinputs.hpp"
#include "engine/omusic.hpp"
#include "engine/outrun.hpp"

#include "outrun_alloc.hpp"
#include "outrun_data.h"
#include "outrun_data_priv.h"
#include "sdl2/input.hpp"
#include "sdl2/rendersurface.hpp"

// The globals port/glue.cpp defines on the target.
namespace cannonball
{
    Audio audio;
    int frame = 0;
    bool tick_frame = true;
    double frame_ms = 0;
    int fps_counter = 0;
    int state = STATE_BOOT;
}

// ---------------------------------------------------------------------------
// port/alloc.cpp: there is no PSRAM here, so both pools are the heap.
// ---------------------------------------------------------------------------

extern "C" void *outrun_psram_alloc(size_t bytes) { return calloc(1, bytes); }
extern "C" void outrun_psram_free(void *p) { free(p); }
extern "C" void *outrun_sram_alloc(size_t bytes) { return malloc(bytes); }
extern "C" void outrun_sram_free(void *p) { free(p); }

// ---------------------------------------------------------------------------
// port/render.cpp, with a host buffer in place of the HSTX framebuffer.
// convert_palette() packs RGB555 exactly as the HSTX build does.
// ---------------------------------------------------------------------------

static uint16_t s_frame[S16_WIDTH * S16_HEIGHT];

void RenderBase::convert_palette(uint32_t adr, uint32_t r1, uint32_t g1, uint32_t b1)
{
    adr >>= 1;

    uint32_t r = r1 * 8;
    uint32_t g = g1 * 8;
    uint32_t b = b1 * 8;
    rgb[adr] = (uint16_t)(((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3));

    r = r1 * shadow_multi / 31;
    g = g1 * shadow_multi / 31;
    b = b1 * shadow_multi / 31;
    rgb[adr + S16_PALETTE_ENTRIES] = (uint16_t)(((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3));
}

void RenderBase::set_shadow_intensity(float f)
{
    shadow_multi = (int)(255.0f * f + 0.5f);
}

bool Render::init(int src_width, int src_height, int, int, int)
{
    this->src_width = src_width;
    this->src_height = src_height;
    return true;
}

void Render::disable() {}
bool Render::start_frame() { return true; }
bool Render::finalize_frame() { return true; }

void Render::draw_frame(uint16_t *pixels)
{
    for (int i = 0; i < src_width * src_height; i++)
    {
        s_frame[i] = rgb[pixels[i]];
    }
}

// ---------------------------------------------------------------------------
// Frame output
// ---------------------------------------------------------------------------

static bool write_ppm(const char *path, int scale)
{
    FILE *f = fopen(path, "wb");
    if (!f)
    {
        perror(path);
        return false;
    }
    const int w = S16_WIDTH * scale, h = S16_HEIGHT * scale;
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    std::vector<uint8_t> line(w * 3);
    for (int y = 0; y < S16_HEIGHT; y++)
    {
        for (int x = 0; x < w; x++)
        {
            uint16_t c = s_frame[y * S16_WIDTH + x / scale];
            uint8_t r = (c >> 10) & 31, g = (c >> 5) & 31, b = c & 31;
            line[x * 3 + 0] = (uint8_t)((r << 3) | (r >> 2));
            line[x * 3 + 1] = (uint8_t)((g << 3) | (g >> 2));
            line[x * 3 + 2] = (uint8_t)((b << 3) | (b >> 2));
        }
        for (int i = 0; i < scale; i++)
        {
            fwrite(line.data(), 1, line.size(), f);
        }
    }
    fclose(f);
    return true;
}

static std::vector<uint8_t> load_file(const char *path)
{
    std::vector<uint8_t> data;
    FILE *f = fopen(path, "rb");
    if (!f)
    {
        perror(path);
        return data;
    }
    fseek(f, 0, SEEK_END);
    data.resize(ftell(f));
    fseek(f, 0, SEEK_SET);
    if (fread(data.data(), 1, data.size(), f) != data.size())
    {
        data.clear();
    }
    fclose(f);
    return data;
}

// ---------------------------------------------------------------------------
// port/glue.cpp's outrun_engine_init(), without the HSTX audio hand-off.
// ---------------------------------------------------------------------------

static bool engine_init(void)
{
    struct
    {
        RomLoader *dst;
        outrun_region_t region;
    } bind[] = {
        {&roms.rom0, OUTRUN_REGION_ROM0},
        {&roms.rom1, OUTRUN_REGION_ROM1},
        {&roms.z80, OUTRUN_REGION_Z80},
        {&roms.pcm, OUTRUN_REGION_PCM},
    };
    for (auto &b : bind)
    {
        uint32_t size = 0;
        const uint8_t *p = outrun_data_region(b.region, &size);
        if (!p)
        {
            return false;
        }
        b.dst->set_flash(p, size);
    }

    config.set_fps(config.video.fps);
    input.init();
    if (!video.init(&roms, &config.video))
    {
        return false;
    }
    osoundint.init();

    /* The firmware never calls this, so omusic's tilemap pointer stays NULL and
     * blit_music_select() reads tilemap->loaded through it. On the RP2350 that
     * read lands in boot ROM and is harmless (the s16_x_off > 0 test after it is
     * false at 320x224 anyway); here it is a SEGV. port/romloader.cpp's
     * load_binary() always fails, so this only creates the two loaders with
     * `loaded` false, and the screen takes the same branch as on the board. */
    omusic.load_widescreen_map("");

    outrun.init();
    cannonball::state = cannonball::STATE_GAME;
    return true;
}

int main(int argc, char **argv)
{
    if (argc < 4)
    {
        fprintf(stderr,
                "usage: %s <outrun-data.bin> <frames> <dump-every-N> [outdir]\n"
                "          [--start F] [--accel F] [--scale N]\n",
                argv[0]);
        return 2;
    }
    const char *image = argv[1];
    const int frames = atoi(argv[2]);
    const int every = atoi(argv[3]) > 0 ? atoi(argv[3]) : 1;
    const char *outdir = "hosttest/out/frames";
    int start_at = -1, accel_at = -1, scale = 1;

    for (int i = 4; i < argc; i++)
    {
        if (!strcmp(argv[i], "--start") && i + 1 < argc)
            start_at = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--accel") && i + 1 < argc)
            accel_at = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--scale") && i + 1 < argc)
            scale = atoi(argv[++i]) > 0 ? atoi(argv[i]) : 1;
        else if (argv[i][0] != '-')
            outdir = argv[i];
        else
        {
            fprintf(stderr, "unknown option %s\n", argv[i]);
            return 2;
        }
    }
    mkdir(outdir, 0755);

    // OAttractAI's constructor seeds rand() from the clock, and the attract
    // mode AI picks its route at each fork with it. A fixed seed makes every run
    // with the same options produce the same frames.
    srand(1);

    std::vector<uint8_t> data = load_file(image);
    if (data.empty() || !outrun_data_adopt_psram(data.data(), (uint32_t)data.size()))
    {
        fprintf(stderr, "%s: not a usable data image\n", image);
        return 1;
    }
    if (!engine_init())
    {
        fprintf(stderr, "engine init failed\n");
        return 1;
    }

    // The 60 Hz loop of port/glue.cpp's outrun_engine_tick(), with frame skip
    // off: every engine tick is rendered. At config.fps 30 the engine runs on
    // every other displayed frame, which then repeats the previous picture.
    using namespace cannonball;
    for (frame = 1; frame <= frames; frame++)
    {
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

        input.set_button(Input::START, start_at >= 0 && frame >= start_at && frame < start_at + 8);
        input.set_button(Input::ACCEL, accel_at >= 0 && frame >= accel_at);

        if (run_engine)
        {
            if (tick_frame)
            {
                oinputs.tick();
                oinputs.do_gear();
            }
            outrun.tick(tick_frame);
            if (tick_frame)
            {
                input.frame_done();
            }
            osoundint.tick();

            video.prepare_frame();
            video.render_frame();
        }

        if (frame % every == 0)
        {
            char path[1024];
            snprintf(path, sizeof(path), "%s/frame_%05d.ppm", outdir, frame);
            if (!write_ppm(path, scale))
            {
                return 1;
            }
        }
        if (frame % 600 == 0)
        {
            printf("frame %d: game_state %d\n", frame, outrun.game_state);
        }
    }
    return 0;
}
