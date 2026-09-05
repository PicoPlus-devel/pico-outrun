/*
 * mkoutrundata - pack an OutRun revision B ROM set into picoOutRun's flash data
 *                image.
 *
 *   mkoutrundata <romset-dir> [-o outrun-data.bin] [--check] [-q]
 *
 * The ROM set is supplied by the user and is copyright SEGA. Neither it nor the
 * image produced here may be redistributed.
 *
 * WHY THIS EXISTS
 * ---------------
 * Cannonball decodes the tile, sprite and road ROMs into RAM at startup:
 * hwtiles::tiles + tiles_backup (256 KB each), hwsprites::sprites (1 MB) and
 * HWRoad::roads (257 KB). That is about 1.5 MB, which an RP2350 does not have.
 * Doing the same work here puts the result in flash instead, and makes
 * tiles_backup unnecessary - the flash copy is itself the unmodified original.
 *
 * The load table and the three decoders below are transcribed from Cannonball
 * and each cites its source. They are the authoritative definition of the
 * on-flash format; ../port/outrun_data.h defines the container. If you change a
 * decoder upstream, change it here too.
 *
 * NOT baked: hwtiles::patch_tiles(). Upstream applies it only on the music
 * selection screen and only in widescreen (omusic.cpp: `config.s16_x_off > 0`),
 * reverting it on exit. picoOutRun renders 320x224, where s16_x_off is 0
 * (video.cpp), so the patch never runs and the tiles are stored unpatched.
 */

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Must match ../port/outrun_data.h */
#define OUTRUN_DATA_MAGIC 0x4E55524Fu /* 'ORUN' little-endian */
#define OUTRUN_DATA_VERSION 1u
#define OUTRUN_REGION_COUNT 7
#define OUTRUN_HEADER_SIZE (16 + OUTRUN_REGION_COUNT * 8)

/* ------------------------------------------------------------------------- */
/* CRC-32 (reflected, poly 0xEDB88320) - what boost::crc_32_type computes, and
 * therefore what the CRCs in Cannonball's roms.cpp are.                       */
/* ------------------------------------------------------------------------- */

static uint32_t crc32_table[256];

static void crc32_init(void)
{
    for (uint32_t i = 0; i < 256; i++)
    {
        uint32_t c = i;
        for (int k = 0; k < 8; k++)
        {
            c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        }
        crc32_table[i] = c;
    }
}

static uint32_t crc32_buf(const uint8_t *buf, size_t len)
{
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++)
    {
        c = crc32_table[(c ^ buf[i]) & 0xFF] ^ (c >> 8);
    }
    return c ^ 0xFFFFFFFFu;
}

/* ------------------------------------------------------------------------- */
/* Source ROM regions, sized exactly as Cannonball's Roms::load_revb_roms()
 * initialises them.                                                           */
/* ------------------------------------------------------------------------- */

enum
{
    SRC_ROM0 = 0,
    SRC_ROM1,
    SRC_TILES,
    SRC_ROAD,
    SRC_SPRITES,
    SRC_Z80,
    SRC_PCM,
    SRC_COUNT
};

static const struct
{
    const char *name;
    uint32_t size;
} src_def[SRC_COUNT] = {
    [SRC_ROM0] = {"rom0", 0x40000},   [SRC_ROM1] = {"rom1", 0x40000},
    [SRC_TILES] = {"tiles", 0x30000}, [SRC_ROAD] = {"road", 0x10000},
    [SRC_SPRITES] = {"sprites", 0x100000},
    [SRC_Z80] = {"z80", 0x10000},     [SRC_PCM] = {"pcm", 0x60000},
};

static uint8_t *src[SRC_COUNT];

/* Transcribed verbatim from cannonball/src/main/roms.cpp,
 * Roms::load_revb_roms(). Note the sprite ROMs interleave in board-position
 * order (9, 10, 11, 12, 13, 14, 15, 16), which is NOT numeric ROM order - and
 * that the PCM region is banked, with the six samples 64 KB apart in a 384 KB
 * space rather than packed end to end. */
static const struct
{
    int region;
    const char *file;
    uint32_t offset;
    uint32_t length;
    uint32_t crc;
    uint8_t interleave;
} load_table[] = {
    /* Master CPU */
    {SRC_ROM0, "epr-10380b.133", 0x00000, 0x10000, 0x1f6cadad, 2},
    {SRC_ROM0, "epr-10382b.118", 0x00001, 0x10000, 0xc4c3fa1a, 2},
    {SRC_ROM0, "epr-10381b.132", 0x20000, 0x10000, 0xbe8c412b, 2},
    {SRC_ROM0, "epr-10383b.117", 0x20001, 0x10000, 0x10a2014a, 2},
    /* Slave CPU */
    {SRC_ROM1, "epr-10327a.76", 0x00000, 0x10000, 0xe28a5baf, 2},
    {SRC_ROM1, "epr-10329a.58", 0x00001, 0x10000, 0xda131c81, 2},
    {SRC_ROM1, "epr-10328a.75", 0x20000, 0x10000, 0xd5ec5e5d, 2},
    {SRC_ROM1, "epr-10330a.57", 0x20001, 0x10000, 0xba9ec82a, 2},
    /* Tiles */
    {SRC_TILES, "opr-10268.99", 0x00000, 0x08000, 0x95344b04, 1},
    {SRC_TILES, "opr-10232.102", 0x08000, 0x08000, 0x776ba1eb, 1},
    {SRC_TILES, "opr-10267.100", 0x10000, 0x08000, 0xa85bb823, 1},
    {SRC_TILES, "opr-10231.103", 0x18000, 0x08000, 0x8908bcbf, 1},
    {SRC_TILES, "opr-10266.101", 0x20000, 0x08000, 0x9f6f1a74, 1},
    {SRC_TILES, "opr-10230.104", 0x28000, 0x08000, 0x686f5e50, 1},
    /* Road - two identical ROMs, one per road */
    {SRC_ROAD, "opr-10185.11", 0x00000, 0x08000, 0x22794426, 1},
    {SRC_ROAD, "opr-10186.47", 0x08000, 0x08000, 0x22794426, 1},
    /* Sprites */
    {SRC_SPRITES, "mpr-10371.9", 0x000000, 0x20000, 0x7cc86208, 4},
    {SRC_SPRITES, "mpr-10373.10", 0x000001, 0x20000, 0xb0d26ac9, 4},
    {SRC_SPRITES, "mpr-10375.11", 0x000002, 0x20000, 0x59b60bd7, 4},
    {SRC_SPRITES, "mpr-10377.12", 0x000003, 0x20000, 0x17a1b04a, 4},
    {SRC_SPRITES, "mpr-10372.13", 0x080000, 0x20000, 0xb557078c, 4},
    {SRC_SPRITES, "mpr-10374.14", 0x080001, 0x20000, 0x8051e517, 4},
    {SRC_SPRITES, "mpr-10376.15", 0x080002, 0x20000, 0xf3b8f318, 4},
    {SRC_SPRITES, "mpr-10378.16", 0x080003, 0x20000, 0xa1062984, 4},
    /* Z80 sound program. Cannonball deliberately doubles the region to 0x10000
     * to make room for extra FM music. */
    {SRC_Z80, "epr-10187.88", 0x0000, 0x08000, 0xa10abaa9, 1},
    /* Sega PCM samples */
    {SRC_PCM, "opr-10193.66", 0x00000, 0x08000, 0xbcd10dde, 1},
    {SRC_PCM, "opr-10192.67", 0x10000, 0x08000, 0x770f1270, 1},
    {SRC_PCM, "opr-10191.68", 0x20000, 0x08000, 0x20a284ab, 1},
    {SRC_PCM, "opr-10190.69", 0x30000, 0x08000, 0x7cab70e2, 1},
    {SRC_PCM, "opr-10189.70", 0x40000, 0x08000, 0x01366b54, 1},
    {SRC_PCM, "opr-10188.71", 0x50000, 0x08000, 0xbad30ad9, 1},
};

#define LOAD_TABLE_LEN ((int)(sizeof(load_table) / sizeof(load_table[0])))

static bool quiet;

static void info(const char *fmt, ...)
{
    if (quiet)
    {
        return;
    }
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stdout, fmt, ap);
    va_end(ap);
}

/* Cannonball's RomLoader::load_rom(): read `length` bytes, verify the CRC over
 * what was actually read, then scatter with
 *     rom[(i * interleave) + offset] = buffer[i]
 */
static int load_rom(const char *dir, int idx)
{
    char path[4096];
    snprintf(path, sizeof(path), "%s/%s", dir, load_table[idx].file);

    FILE *f = fopen(path, "rb");
    if (!f)
    {
        fprintf(stderr, "  MISSING  %s\n", load_table[idx].file);
        return 1;
    }

    uint32_t length = load_table[idx].length;
    uint8_t *buf = malloc(length);
    if (!buf)
    {
        fprintf(stderr, "out of memory\n");
        fclose(f);
        return 1;
    }

    size_t got = fread(buf, 1, length, f);
    fclose(f);

    uint32_t crc = crc32_buf(buf, got);
    if (crc != load_table[idx].crc)
    {
        fprintf(stderr, "  BAD CRC  %-16s expected %08x, found %08x", load_table[idx].file,
                load_table[idx].crc, crc);
        if (got != length)
        {
            fprintf(stderr, " (read %zu of %u bytes)", got, length);
        }
        fprintf(stderr, "\n");
        free(buf);
        return 1;
    }

    uint8_t *dst = src[load_table[idx].region];
    uint32_t il = load_table[idx].interleave;
    uint32_t off = load_table[idx].offset;
    for (uint32_t i = 0; i < length; i++)
    {
        dst[(i * il) + off] = buf[i];
    }

    free(buf);
    return 0;
}

/* ------------------------------------------------------------------------- */
/* Decoders. Each is transcribed from the cited Cannonball function.           */
/* ------------------------------------------------------------------------- */

#define TILES_LENGTH 0x10000u   /* uint32 entries - hwtiles.hpp                */
#define SPRITES_LENGTH 0x40000u /* uint32 entries - hwsprites.hpp (0x100000>>2)*/
#define ROADS_LENGTH 0x40200u   /* bytes          - hwroad.hpp                 */
#define ROAD_ROM_SIZE 0x8000u   /* HWRoad::rom_size                            */

/* hwvideo/hwtiles.cpp, hwtiles::init(). Three 64 KB bitplanes -> 8 pixels of
 * 4bpp per uint32. */
static void decode_tiles(const uint8_t *s, uint32_t *out)
{
    for (uint32_t i = 0; i < TILES_LENGTH; i++)
    {
        uint8_t p0 = s[i];
        uint8_t p1 = s[i + 0x10000];
        uint8_t p2 = s[i + 0x20000];

        uint32_t val = 0;
        for (int ii = 0; ii < 8; ii++)
        {
            uint8_t bit = 7 - ii;
            uint8_t pix = ((((p0 >> bit)) & 1) | (((p1 >> bit) << 1) & 2) | (((p2 >> bit) << 2) & 4));
            val = (val << 4) | pix;
        }
        out[i] = val;
    }
}

/* hwvideo/hwsprites.cpp, hwsprites::init(). Four sequential bytes, read
 * d3 d2 d1 d0, packed big-endian into one word. */
static void decode_sprites(const uint8_t *s, uint32_t *out)
{
    const uint8_t *spr = s;
    for (uint32_t i = 0; i < SPRITES_LENGTH; i++)
    {
        uint8_t d3 = *spr++;
        uint8_t d2 = *spr++;
        uint8_t d1 = *spr++;
        uint8_t d0 = *spr++;
        out[i] = ((uint32_t)d0 << 24) | ((uint32_t)d1 << 16) | ((uint32_t)d2 << 8) | d3;
    }
}

/* hwvideo/hwroad.cpp, HWRoad::decode_road(). Two identical 512x256 2bpp maps,
 * plus a dummy road in the final 512-byte entry. */
static void decode_road(const uint8_t *s, uint8_t *out)
{
    for (int y = 0; y < 256 * 2; y++)
    {
        const int rd = ((y & 0xff) * 0x40 + (y >> 8) * 0x8000) % ROAD_ROM_SIZE;
        const int dst = y * 512;

        for (int x = 0; x < 512; x++)
        {
            out[dst + x] = (uint8_t)((((s[rd + (x / 8)] >> (~x & 7)) & 1) << 0) |
                                     (((s[rd + (x / 8 + 0x4000)] >> (~x & 7)) & 1) << 1));

            /* pre-mark road data in the "stripe" area with a high bit */
            if (x >= 256 - 8 && x < 256 && out[dst + x] == 3)
            {
                out[dst + x] |= 4;
            }
        }
    }

    /* dummy road in the last entry */
    for (int i = 0; i < 512; i++)
    {
        out[256 * 2 * 512 + i] = 3;
    }
}

/* ------------------------------------------------------------------------- */

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

/* Regions in ../port/outrun_data.h enum order. */
enum
{
    OUT_ROM0 = 0,
    OUT_ROM1,
    OUT_TILES,
    OUT_SPRITES,
    OUT_ROAD,
    OUT_Z80,
    OUT_PCM,
};

static void usage(void)
{
    fprintf(stderr,
            "usage: mkoutrundata <romset-dir> [-o out.bin] [--check] [-q]\n"
            "\n"
            "  Packs an OutRun revision B ROM set into picoOutRun's flash data image.\n"
            "  --check verifies the ROM set and, if the output file already exists,\n"
            "          byte-compares against it without writing.\n");
}

int main(int argc, char **argv)
{
    const char *romdir = NULL;
    const char *outpath = "outrun-data.bin";
    bool check = false;

    for (int i = 1; i < argc; i++)
    {
        if (!strcmp(argv[i], "-o") && i + 1 < argc)
        {
            outpath = argv[++i];
        }
        else if (!strcmp(argv[i], "--check"))
        {
            check = true;
        }
        else if (!strcmp(argv[i], "-q"))
        {
            quiet = true;
        }
        else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help"))
        {
            usage();
            return 0;
        }
        else if (argv[i][0] == '-')
        {
            fprintf(stderr, "unknown option: %s\n", argv[i]);
            usage();
            return 2;
        }
        else if (!romdir)
        {
            romdir = argv[i];
        }
        else
        {
            usage();
            return 2;
        }
    }

    if (!romdir)
    {
        usage();
        return 2;
    }

    crc32_init();

    for (int i = 0; i < SRC_COUNT; i++)
    {
        src[i] = calloc(1, src_def[i].size);
        if (!src[i])
        {
            fprintf(stderr, "out of memory\n");
            return 1;
        }
    }

    info("Reading OutRun revision B ROM set from %s\n", romdir);
    int failed = 0;
    for (int i = 0; i < LOAD_TABLE_LEN; i++)
    {
        failed += load_rom(romdir, i);
    }
    if (failed)
    {
        fprintf(stderr,
                "\n%d of %d ROM files missing or corrupt - cannot build the data image.\n"
                "Expected the MAME 'outrun' (revision B) parent set.\n",
                failed, LOAD_TABLE_LEN);
        return 1;
    }
    info("  %d ROM files loaded, all CRCs match\n", LOAD_TABLE_LEN);

    /* Decode into their on-flash form. */
    uint32_t *tiles = malloc(TILES_LENGTH * 4);
    uint32_t *sprites = malloc(SPRITES_LENGTH * 4);
    uint8_t *roads = malloc(ROADS_LENGTH);
    if (!tiles || !sprites || !roads)
    {
        fprintf(stderr, "out of memory\n");
        return 1;
    }
    decode_tiles(src[SRC_TILES], tiles);
    decode_sprites(src[SRC_SPRITES], sprites);
    decode_road(src[SRC_ROAD], roads);
    info("  tiles, sprites and road decoded\n");

    /* Lay out the image. */
    struct
    {
        const void *data;
        uint32_t size;
        bool is_u32;
    } out[OUTRUN_REGION_COUNT] = {
        [OUT_ROM0] = {src[SRC_ROM0], src_def[SRC_ROM0].size, false},
        [OUT_ROM1] = {src[SRC_ROM1], src_def[SRC_ROM1].size, false},
        [OUT_TILES] = {tiles, TILES_LENGTH * 4, true},
        [OUT_SPRITES] = {sprites, SPRITES_LENGTH * 4, true},
        [OUT_ROAD] = {roads, ROADS_LENGTH, false},
        [OUT_Z80] = {src[SRC_Z80], src_def[SRC_Z80].size, false},
        [OUT_PCM] = {src[SRC_PCM], src_def[SRC_PCM].size, false},
    };

    uint32_t total = OUTRUN_HEADER_SIZE;
    uint32_t offset[OUTRUN_REGION_COUNT];
    for (int i = 0; i < OUTRUN_REGION_COUNT; i++)
    {
        total = (total + 3u) & ~3u; /* every region 4-byte aligned */
        offset[i] = total;
        total += out[i].size;
    }

    uint8_t *image = calloc(1, total);
    if (!image)
    {
        fprintf(stderr, "out of memory\n");
        return 1;
    }

    put32(image + 0, OUTRUN_DATA_MAGIC);
    put32(image + 4, OUTRUN_DATA_VERSION);
    put32(image + 8, total);
    /* image + 12 is the CRC, filled in once the payload is in place */
    for (int i = 0; i < OUTRUN_REGION_COUNT; i++)
    {
        put32(image + 16 + i * 8, offset[i]);
        put32(image + 16 + i * 8 + 4, out[i].size);

        if (out[i].is_u32)
        {
            /* Written explicitly little-endian: the RP2350 reads these as
             * uint32_t straight out of XIP. */
            const uint32_t *w = out[i].data;
            for (uint32_t k = 0; k < out[i].size / 4; k++)
            {
                put32(image + offset[i] + k * 4, w[k]);
            }
        }
        else
        {
            memcpy(image + offset[i], out[i].data, out[i].size);
        }
    }
    put32(image + 12, crc32_buf(image + 16, total - 16));

    info("  image is %u bytes (%.2f MB)\n", total, total / (1024.0 * 1024.0));

    if (check)
    {
        FILE *f = fopen(outpath, "rb");
        if (!f)
        {
            info("ROM set is good. %s does not exist, nothing to compare.\n", outpath);
            return 0;
        }
        uint8_t *have = malloc(total);
        size_t got = fread(have, 1, total, f);
        int extra = fgetc(f) != EOF;
        fclose(f);
        if (got != total || extra || memcmp(have, image, total) != 0)
        {
            fprintf(stderr, "MISMATCH: %s differs from what this ROM set produces.\n", outpath);
            return 1;
        }
        info("%s matches this ROM set exactly.\n", outpath);
        return 0;
    }

    FILE *f = fopen(outpath, "wb");
    if (!f)
    {
        fprintf(stderr, "cannot write %s\n", outpath);
        return 1;
    }
    if (fwrite(image, 1, total, f) != total)
    {
        fprintf(stderr, "short write to %s\n", outpath);
        fclose(f);
        return 1;
    }
    fclose(f);
    info("Wrote %s\n", outpath);
    return 0;
}
