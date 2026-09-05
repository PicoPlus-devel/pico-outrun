/*
 * outrun_pack - see outrun_pack.h for why this is shared between the host tool
 * and the firmware.
 *
 * The load table and the three decoders below are transcribed from Cannonball
 * and each cites its source. They are the authoritative definition of the packed
 * format; port/outrun_data.h defines the container. If a decoder changes
 * upstream, change it here too - and hosttest/verify_decode will tell you if you
 * get it wrong.
 *
 * NOT baked: hwtiles::patch_tiles(). Upstream applies it only on the music
 * selection screen and only in widescreen (omusic.cpp: `config.s16_x_off > 0`),
 * reverting it on exit. picoOutRun renders 320x224, where s16_x_off is 0
 * (video.cpp), so the patch never runs and the tiles are stored unpatched.
 */

#include "outrun_pack.h"

#include <string.h>

/* The TILES and SPRITES regions are written as native uint32 and are read back
 * as `const uint32_t *` by the engine, straight out of XIP or PSRAM. Everything
 * else - the header included - goes through put32 and stays byte-order neutral. */
#if defined(__BYTE_ORDER__) && (__BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__)
#error "outrun_pack writes the TILES and SPRITES regions in native uint32 order"
#endif

#define OUTRUN_HEADER_SIZE (16 + OUTRUN_REGION_COUNT * 8)
_Static_assert(sizeof(outrun_data_header_t) == OUTRUN_HEADER_SIZE,
               "the packed header must match outrun_data_header_t exactly");

/* ------------------------------------------------------------------------- */
/* CRC-32 (reflected, poly 0xEDB88320) - what boost::crc_32_type computes, and
 * therefore what the CRCs in Cannonball's roms.cpp are. The same table as
 * pico_shared/crc32.cpp, so crc32_update(0, p, n) == update_crc32(0, p, n) and
 * the firmware could use either.                                              */
/* ------------------------------------------------------------------------- */

static const uint32_t crc32_table[256] = {
    0x00000000, 0x77073096, 0xEE0E612C, 0x990951BA,
    0x076DC419, 0x706AF48F, 0xE963A535, 0x9E6495A3,
    0x0EDB8832, 0x79DCB8A4, 0xE0D5E91E, 0x97D2D988,
    0x09B64C2B, 0x7EB17CBD, 0xE7B82D07, 0x90BF1D91,
    0x1DB71064, 0x6AB020F2, 0xF3B97148, 0x84BE41DE,
    0x1ADAD47D, 0x6DDDE4EB, 0xF4D4B551, 0x83D385C7,
    0x136C9856, 0x646BA8C0, 0xFD62F97A, 0x8A65C9EC,
    0x14015C4F, 0x63066CD9, 0xFA0F3D63, 0x8D080DF5,
    0x3B6E20C8, 0x4C69105E, 0xD56041E4, 0xA2677172,
    0x3C03E4D1, 0x4B04D447, 0xD20D85FD, 0xA50AB56B,
    0x35B5A8FA, 0x42B2986C, 0xDBBBC9D6, 0xACBCF940,
    0x32D86CE3, 0x45DF5C75, 0xDCD60DCF, 0xABD13D59,
    0x26D930AC, 0x51DE003A, 0xC8D75180, 0xBFD06116,
    0x21B4F4B5, 0x56B3C423, 0xCFBA9599, 0xB8BDA50F,
    0x2802B89E, 0x5F058808, 0xC60CD9B2, 0xB10BE924,
    0x2F6F7C87, 0x58684C11, 0xC1611DAB, 0xB6662D3D,
    0x76DC4190, 0x01DB7106, 0x98D220BC, 0xEFD5102A,
    0x71B18589, 0x06B6B51F, 0x9FBFE4A5, 0xE8B8D433,
    0x7807C9A2, 0x0F00F934, 0x9609A88E, 0xE10E9818,
    0x7F6A0DBB, 0x086D3D2D, 0x91646C97, 0xE6635C01,
    0x6B6B51F4, 0x1C6C6162, 0x856530D8, 0xF262004E,
    0x6C0695ED, 0x1B01A57B, 0x8208F4C1, 0xF50FC457,
    0x65B0D9C6, 0x12B7E950, 0x8BBEB8EA, 0xFCB9887C,
    0x62DD1DDF, 0x15DA2D49, 0x8CD37CF3, 0xFBD44C65,
    0x4DB26158, 0x3AB551CE, 0xA3BC0074, 0xD4BB30E2,
    0x4ADFA541, 0x3DD895D7, 0xA4D1C46D, 0xD3D6F4FB,
    0x4369E96A, 0x346ED9FC, 0xAD678846, 0xDA60B8D0,
    0x44042D73, 0x33031DE5, 0xAA0A4C5F, 0xDD0D7CC9,
    0x5005713C, 0x270241AA, 0xBE0B1010, 0xC90C2086,
    0x5768B525, 0x206F85B3, 0xB966D409, 0xCE61E49F,
    0x5EDEF90E, 0x29D9C998, 0xB0D09822, 0xC7D7A8B4,
    0x59B33D17, 0x2EB40D81, 0xB7BD5C3B, 0xC0BA6CAD,
    0xEDB88320, 0x9ABFB3B6, 0x03B6E20C, 0x74B1D29A,
    0xEAD54739, 0x9DD277AF, 0x04DB2615, 0x73DC1683,
    0xE3630B12, 0x94643B84, 0x0D6D6A3E, 0x7A6A5AA8,
    0xE40ECF0B, 0x9309FF9D, 0x0A00AE27, 0x7D079EB1,
    0xF00F9344, 0x8708A3D2, 0x1E01F268, 0x6906C2FE,
    0xF762575D, 0x806567CB, 0x196C3671, 0x6E6B06E7,
    0xFED41B76, 0x89D32BE0, 0x10DA7A5A, 0x67DD4ACC,
    0xF9B9DF6F, 0x8EBEEFF9, 0x17B7BE43, 0x60B08ED5,
    0xD6D6A3E8, 0xA1D1937E, 0x38D8C2C4, 0x4FDFF252,
    0xD1BB67F1, 0xA6BC5767, 0x3FB506DD, 0x48B2364B,
    0xD80D2BDA, 0xAF0A1B4C, 0x36034AF6, 0x41047A60,
    0xDF60EFC3, 0xA867DF55, 0x316E8EEF, 0x4669BE79,
    0xCB61B38C, 0xBC66831A, 0x256FD2A0, 0x5268E236,
    0xCC0C7795, 0xBB0B4703, 0x220216B9, 0x5505262F,
    0xC5BA3BBE, 0xB2BD0B28, 0x2BB45A92, 0x5CB36A04,
    0xC2D7FFA7, 0xB5D0CF31, 0x2CD99E8B, 0x5BDEAE1D,
    0x9B64C2B0, 0xEC63F226, 0x756AA39C, 0x026D930A,
    0x9C0906A9, 0xEB0E363F, 0x72076785, 0x05005713,
    0x95BF4A82, 0xE2B87A14, 0x7BB12BAE, 0x0CB61B38,
    0x92D28E9B, 0xE5D5BE0D, 0x7CDCEFB7, 0x0BDBDF21,
    0x86D3D2D4, 0xF1D4E242, 0x68DDB3F8, 0x1FDA836E,
    0x81BE16CD, 0xF6B9265B, 0x6FB077E1, 0x18B74777,
    0x88085AE6, 0xFF0F6A70, 0x66063BCA, 0x11010B5C,
    0x8F659EFF, 0xF862AE69, 0x616BFFD3, 0x166CCF45,
    0xA00AE278, 0xD70DD2EE, 0x4E048354, 0x3903B3C2,
    0xA7672661, 0xD06016F7, 0x4969474D, 0x3E6E77DB,
    0xAED16A4A, 0xD9D65ADC, 0x40DF0B66, 0x37D83BF0,
    0xA9BCAE53, 0xDEBB9EC5, 0x47B2CF7F, 0x30B5FFE9,
    0xBDBDF21C, 0xCABAC28A, 0x53B39330, 0x24B4A3A6,
    0xBAD03605, 0xCDD70693, 0x54DE5729, 0x23D967BF,
    0xB3667A2E, 0xC4614AB8, 0x5D681B02, 0x2A6F2B94,
    0xB40BBE37, 0xC30C8EA1, 0x5A05DF1B, 0x2D02EF8D
};

static uint32_t crc32_update(uint32_t crc, const uint8_t *buf, uint32_t len)
{
    uint32_t c = ~crc;
    for (uint32_t i = 0; i < len; i++)
    {
        c = crc32_table[(c ^ buf[i]) & 0xFF] ^ (c >> 8);
    }
    return ~c;
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

/* Transcribed verbatim from cannonball/src/main/roms.cpp,
 * Roms::load_revb_roms(). Note the sprite ROMs interleave in board-position
 * order (9, 10, 11, 12, 13, 14, 15, 16), which is NOT numeric ROM order - and
 * that the PCM region is banked, with the six samples 64 KB apart in a 384 KB
 * space rather than packed end to end.
 *
 * The entries of each region are contiguous here, and the builder relies on it:
 * it decodes tiles the moment the last tiles file has been read, which is what
 * frees the source buffer for the road files. */
static const struct
{
    int region;
    const char *file;
    uint32_t offset;
    uint32_t length;
    uint32_t crc;
    uint8_t interleave;
} load_table[OUTRUN_PACK_FILE_COUNT] = {
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
 * d3 d2 d1 d0, packed big-endian into one word.
 *
 * SAFE IN PLACE, and the builder relies on it to save a megabyte: iteration i
 * reads exactly s[4i..4i+3] and writes out[i], which occupies those same four
 * bytes. The loop is strictly forward, so no later read touches a byte an
 * earlier write changed. `s` is a character type, so it may alias `out` and the
 * compiler cannot assume otherwise. */
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
/* Image layout                                                                */
/* ------------------------------------------------------------------------- */

/* Regions in port/outrun_data.h enum order. */
static const uint32_t out_size[OUTRUN_REGION_COUNT] = {
    [OUTRUN_REGION_ROM0] = 0x40000,
    [OUTRUN_REGION_ROM1] = 0x40000,
    [OUTRUN_REGION_TILES] = TILES_LENGTH * 4,
    [OUTRUN_REGION_SPRITES] = SPRITES_LENGTH * 4,
    [OUTRUN_REGION_ROAD] = ROADS_LENGTH,
    [OUTRUN_REGION_Z80] = 0x10000,
    [OUTRUN_REGION_PCM] = 0x60000,
};

/* The source regions the load table scatters into, before decoding. */
static const uint32_t src_size[SRC_COUNT] = {
    [SRC_ROM0] = 0x40000,  [SRC_ROM1] = 0x40000, [SRC_TILES] = 0x30000, [SRC_ROAD] = 0x10000,
    [SRC_SPRITES] = 0x100000, [SRC_Z80] = 0x10000, [SRC_PCM] = 0x60000,
};

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static uint32_t layout(uint32_t *offset)
{
    uint32_t total = OUTRUN_HEADER_SIZE;
    for (int i = 0; i < OUTRUN_REGION_COUNT; i++)
    {
        total = (total + 3u) & ~3u; /* every region 4-byte aligned */
        offset[i] = total;
        total += out_size[i];
    }
    return total;
}

uint32_t outrun_pack_image_size(void)
{
    uint32_t offset[OUTRUN_REGION_COUNT];
    return layout(offset);
}

int outrun_pack_file_count(void)
{
    return OUTRUN_PACK_FILE_COUNT;
}

const char *outrun_pack_filename(int idx)
{
    if (idx < 0 || idx >= OUTRUN_PACK_FILE_COUNT)
    {
        return NULL;
    }
    return load_table[idx].file;
}

/* ------------------------------------------------------------------------- */
/* The builder                                                                 */
/* ------------------------------------------------------------------------- */

/* Where a source region's bytes are scattered. ROM0, ROM1, Z80 and PCM are
 * verbatim copies, so they go straight into the image; SPRITES goes there too
 * and is decoded in place afterwards. Only tiles and road need a staging buffer,
 * and they take turns using the same one. */
static uint8_t *scatter_dest(int region, uint8_t *image, const uint32_t *offset, uint8_t *src)
{
    switch (region)
    {
    case SRC_ROM0:
        return image + offset[OUTRUN_REGION_ROM0];
    case SRC_ROM1:
        return image + offset[OUTRUN_REGION_ROM1];
    case SRC_SPRITES:
        return image + offset[OUTRUN_REGION_SPRITES];
    case SRC_Z80:
        return image + offset[OUTRUN_REGION_Z80];
    case SRC_PCM:
        return image + offset[OUTRUN_REGION_PCM];
    case SRC_TILES:
    case SRC_ROAD:
        return src;
    default:
        return NULL;
    }
}

/* Read one ROM file, CRC what was read, and scatter it into `dst`.
 *
 * Cannonball's RomLoader::load_rom() does the same three things; the scatter is
 * its `rom[(i * interleave) + offset] = buffer[i]`, done a chunk at a time so
 * the firmware never needs the whole file in memory at once. */
static outrun_file_state_t load_one(const outrun_pack_io_t *io, int idx, uint8_t *dst,
                                    uint8_t *scratch, uint32_t *got_out, uint32_t *crc_out)
{
    const uint32_t want = load_table[idx].length;
    const uint32_t il = load_table[idx].interleave;
    const uint32_t off = load_table[idx].offset;

    *got_out = 0;
    *crc_out = 0;

    void *h = io->open(io->ctx, load_table[idx].file);
    if (!h)
    {
        return OUTRUN_FILE_MISSING;
    }

    uint32_t got = 0;
    uint32_t crc = 0;
    bool ioerr = false;

    while (got < want)
    {
        uint32_t chunk = want - got;
        if (chunk > OUTRUN_PACK_CHUNK)
        {
            chunk = OUTRUN_PACK_CHUNK;
        }

        long n = io->read(io->ctx, h, scratch, chunk);
        if (n < 0)
        {
            ioerr = true;
            break;
        }
        if (n == 0)
        {
            break; /* short file */
        }

        crc = crc32_update(crc, scratch, (uint32_t)n);
        if (il == 1)
        {
            memcpy(dst + off + got, scratch, (size_t)n);
        }
        else
        {
            for (uint32_t k = 0; k < (uint32_t)n; k++)
            {
                dst[((got + k) * il) + off] = scratch[k];
            }
        }
        got += (uint32_t)n;
    }

    io->close(io->ctx, h);

    *got_out = got;
    *crc_out = crc;

    if (ioerr || got != want)
    {
        return OUTRUN_FILE_SHORT;
    }
    if (crc != load_table[idx].crc)
    {
        return OUTRUN_FILE_BAD_CRC;
    }
    return OUTRUN_FILE_OK;
}

static void phase(const outrun_pack_io_t *io, const char *name)
{
    if (io->on_phase)
    {
        io->on_phase(io->ctx, name);
    }
}

outrun_pack_result_t outrun_pack_build(const outrun_pack_io_t *io, uint8_t *image,
                                       uint32_t image_size, uint8_t *scratch,
                                       uint32_t scratch_size, uint8_t *src, uint32_t src_size_in,
                                       outrun_pack_status_t *status)
{
    uint32_t offset[OUTRUN_REGION_COUNT];
    const uint32_t total = layout(offset);

    outrun_pack_status_t st;
    memset(&st, 0, sizeof(st));
    st.image_size = total;

    if (!io || !io->open || !io->read || !io->close || !image || !scratch || !src ||
        image_size < total || scratch_size < OUTRUN_PACK_CHUNK ||
        src_size_in < OUTRUN_PACK_SRC_SIZE)
    {
        st.result = OUTRUN_PACK_ERR_ARGS;
        if (status)
        {
            *status = st;
        }
        return st.result;
    }

    /* The Z80 region's upper half and the gaps between the banked PCM samples
     * are never written, and are defined to be zero. */
    memset(image, 0, total);

    put32(image + 0, OUTRUN_DATA_MAGIC);
    put32(image + 4, OUTRUN_DATA_VERSION);
    put32(image + 8, total);
    /* image + 12 is the CRC, filled in once the payload is in place */
    for (int i = 0; i < OUTRUN_REGION_COUNT; i++)
    {
        put32(image + 16 + i * 8, offset[i]);
        put32(image + 16 + i * 8 + 4, out_size[i]);
    }

    for (int i = 0; i < OUTRUN_PACK_FILE_COUNT; i++)
    {
        const int region = load_table[i].region;
        const bool first_of_region = (i == 0) || (load_table[i - 1].region != region);
        const bool last_of_region =
            (i == OUTRUN_PACK_FILE_COUNT - 1) || (load_table[i + 1].region != region);

        /* Only the staging buffer needs clearing, and only so that a failed load
         * decodes to something deterministic rather than to the previous
         * region's leftovers. On the success path every byte is overwritten. */
        if (first_of_region && (region == SRC_TILES || region == SRC_ROAD))
        {
            memset(src, 0, src_size[region]);
        }

        uint32_t got = 0;
        uint32_t crc = 0;
        outrun_file_state_t state =
            load_one(io, i, scatter_dest(region, image, offset, src), scratch, &got, &crc);

        switch (state)
        {
        case OUTRUN_FILE_OK:
            break;
        case OUTRUN_FILE_MISSING:
            st.missing++;
            break;
        case OUTRUN_FILE_SHORT:
            st.short_read++;
            break;
        case OUTRUN_FILE_BAD_CRC:
            st.bad_crc++;
            break;
        }
        if (state != OUTRUN_FILE_OK && !st.first_bad)
        {
            st.first_bad = load_table[i].file;
        }

        if (io->on_file)
        {
            io->on_file(io->ctx, i + 1, OUTRUN_PACK_FILE_COUNT, load_table[i].file, got,
                        load_table[i].crc, crc, state);
        }

        /* Decode as soon as a staging region is complete: the tiles have to be
         * out of `src` before the road files can be read into it. */
        if (last_of_region && region == SRC_TILES)
        {
            phase(io, "read");
            decode_tiles(src, (uint32_t *)(void *)(image + offset[OUTRUN_REGION_TILES]));
            phase(io, "tiles");
        }
        else if (last_of_region && region == SRC_ROAD)
        {
            phase(io, "read");
            decode_road(src, image + offset[OUTRUN_REGION_ROAD]);
            phase(io, "road");
        }
    }

    phase(io, "read");

    /* In place over the region the sprite files were just scattered into. */
    decode_sprites(image + offset[OUTRUN_REGION_SPRITES],
                   (uint32_t *)(void *)(image + offset[OUTRUN_REGION_SPRITES]));
    phase(io, "sprites");

    put32(image + 12, crc32_update(0, image + 16, total - 16));
    phase(io, "crc");

    if (st.missing)
    {
        st.result = OUTRUN_PACK_ERR_MISSING;
    }
    else if (st.short_read)
    {
        st.result = OUTRUN_PACK_ERR_SHORT;
    }
    else if (st.bad_crc)
    {
        st.result = OUTRUN_PACK_ERR_BAD_CRC;
    }
    else
    {
        st.result = OUTRUN_PACK_OK;
    }

    if (status)
    {
        *status = st;
    }
    return st.result;
}
