/*
 * verify_decode - prove that tools/mkoutrundata produces exactly what
 *                 Cannonball would produce at runtime.
 *
 *   verify_decode <romset-dir> <outrun-data.bin>
 *
 * mkoutrundata.c transcribes three decoders out of Cannonball so it can run
 * them at build time. A transcription can drift from its source, and the
 * failure mode - subtly wrong graphics on hardware, hours from a debugger - is
 * expensive. So this links Cannonball's REAL hwtiles.cpp, hwsprites.cpp and
 * hwroad.cpp, unmodified, calls their init() on the same ROM set, and compares
 * the results byte for byte against the packed image.
 *
 * The only thing it fakes is `config` and `video`, via hosttest/stubs, because
 * the real ones drag in Boost and the SDL frontend. Neither affects decoding.
 *
 * This is the regression test for the whole flash-baking idea. Run it whenever
 * the vendored engine is updated.
 */

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "frontend/config.hpp"
#include "video.hpp"

// The decoded arrays (hwtiles::tiles, hwsprites::sprites, HWRoad::roads) are
// private, and the point of this test is to compare them without touching the
// vendored sources - cannonball/README.md asks that the subset stay diffable
// against upstream. Test-only, and scoped to these three headers.
#define private public
#include "hwvideo/hwroad.hpp"
#include "hwvideo/hwsprites.hpp"
#include "hwvideo/hwtiles.hpp"
#undef private

// `config` comes from port/config.cpp - the firmware's own, not a test copy.
// `hwroad` is defined by hwroad.cpp itself. Only `video` is ours.
Video video;

// Mirrors ../port/outrun_data.h
enum
{
    R_ROM0 = 0,
    R_ROM1,
    R_TILES,
    R_SPRITES,
    R_ROAD,
    R_Z80,
    R_PCM,
    R_COUNT
};
static const char *region_name[R_COUNT] = {"rom0", "rom1", "tiles", "sprites", "road", "z80", "pcm"};

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static bool read_file(const char *path, std::vector<uint8_t> &out)
{
    FILE *f = fopen(path, "rb");
    if (!f)
    {
        return false;
    }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    out.resize((size_t)n);
    bool ok = fread(out.data(), 1, (size_t)n, f) == (size_t)n;
    fclose(f);
    return ok;
}

// Cannonball's RomLoader::load_rom() scatter, minus the CRC check that
// mkoutrundata already performs.
static bool load_into(const char *dir, const char *file, std::vector<uint8_t> &dst, uint32_t offset,
                      uint32_t length, uint32_t interleave)
{
    char path[4096];
    snprintf(path, sizeof(path), "%s/%s", dir, file);
    std::vector<uint8_t> buf;
    if (!read_file(path, buf) || buf.size() < length)
    {
        fprintf(stderr, "cannot read %s\n", path);
        return false;
    }
    for (uint32_t i = 0; i < length; i++)
    {
        dst[(i * interleave) + offset] = buf[i];
    }
    return true;
}

static int compare(const char *what, const uint8_t *got, const uint8_t *want, size_t len)
{
    if (memcmp(got, want, len) == 0)
    {
        printf("  OK    %-8s %zu bytes identical\n", what, len);
        return 0;
    }
    size_t first = 0;
    while (first < len && got[first] == want[first])
    {
        first++;
    }
    size_t ndiff = 0;
    for (size_t i = 0; i < len; i++)
    {
        ndiff += (got[i] != want[i]);
    }
    printf("  FAIL  %-8s %zu of %zu bytes differ, first at offset %zu (cannonball %02x, packed %02x)\n",
           what, ndiff, len, first, want[first], got[first]);
    return 1;
}

int main(int argc, char **argv)
{
    if (argc != 3)
    {
        fprintf(stderr, "usage: verify_decode <romset-dir> <outrun-data.bin>\n");
        return 2;
    }
    const char *romdir = argv[1];

    std::vector<uint8_t> image;
    if (!read_file(argv[2], image) || image.size() < 72)
    {
        fprintf(stderr, "cannot read %s\n", argv[2]);
        return 1;
    }
    if (rd32(&image[0]) != 0x4E55524Fu)
    {
        fprintf(stderr, "%s is not an ORUN image\n", argv[2]);
        return 1;
    }

    // Load the raw source regions exactly as Roms::load_revb_roms() does.
    std::vector<uint8_t> tiles_src(0x30000), sprites_src(0x100000), road_src(0x10000);

    bool ok = true;
    ok &= load_into(romdir, "opr-10268.99", tiles_src, 0x00000, 0x08000, 1);
    ok &= load_into(romdir, "opr-10232.102", tiles_src, 0x08000, 0x08000, 1);
    ok &= load_into(romdir, "opr-10267.100", tiles_src, 0x10000, 0x08000, 1);
    ok &= load_into(romdir, "opr-10231.103", tiles_src, 0x18000, 0x08000, 1);
    ok &= load_into(romdir, "opr-10266.101", tiles_src, 0x20000, 0x08000, 1);
    ok &= load_into(romdir, "opr-10230.104", tiles_src, 0x28000, 0x08000, 1);

    ok &= load_into(romdir, "opr-10185.11", road_src, 0x00000, 0x08000, 1);
    ok &= load_into(romdir, "opr-10186.47", road_src, 0x08000, 0x08000, 1);

    ok &= load_into(romdir, "mpr-10371.9", sprites_src, 0x000000, 0x20000, 4);
    ok &= load_into(romdir, "mpr-10373.10", sprites_src, 0x000001, 0x20000, 4);
    ok &= load_into(romdir, "mpr-10375.11", sprites_src, 0x000002, 0x20000, 4);
    ok &= load_into(romdir, "mpr-10377.12", sprites_src, 0x000003, 0x20000, 4);
    ok &= load_into(romdir, "mpr-10372.13", sprites_src, 0x080000, 0x20000, 4);
    ok &= load_into(romdir, "mpr-10374.14", sprites_src, 0x080001, 0x20000, 4);
    ok &= load_into(romdir, "mpr-10376.15", sprites_src, 0x080002, 0x20000, 4);
    ok &= load_into(romdir, "mpr-10378.16", sprites_src, 0x080003, 0x20000, 4);
    if (!ok)
    {
        return 1;
    }

    printf("Comparing Cannonball's own decoders against %s\n", argv[2]);

    // Heap-allocated: these classes carry ~1.5 MB of member arrays between them.
    hwtiles *tile_layer = new hwtiles();
    hwsprites *sprite_layer = new hwsprites();

    tile_layer->init(tiles_src.data(), false);
    sprite_layer->init(sprites_src.data());
    hwroad.init(road_src.data(), false);

    int fails = 0;
    struct
    {
        int region;
        const uint8_t *data;
        size_t len;
    } cases[] = {
        {R_TILES, (const uint8_t *)tile_layer->tiles, 0x10000 * 4},
        {R_SPRITES, (const uint8_t *)sprite_layer->sprites, 0x40000 * 4},
        {R_ROAD, hwroad.roads, 0x40200},
    };

    for (auto &c : cases)
    {
        uint32_t off = rd32(&image[16 + c.region * 8]);
        uint32_t size = rd32(&image[16 + c.region * 8 + 4]);
        if (size != c.len)
        {
            printf("  FAIL  %-8s image says %u bytes, cannonball produced %zu\n",
                   region_name[c.region], size, c.len);
            fails++;
            continue;
        }
        fails += compare(region_name[c.region], &image[off], c.data, c.len);
    }

    delete tile_layer;
    delete sprite_layer;

    if (fails)
    {
        printf("\n%d region(s) differ - mkoutrundata has drifted from the vendored engine.\n", fails);
        return 1;
    }
    printf("\nAll decoded regions match Cannonball exactly.\n");
    return 0;
}
