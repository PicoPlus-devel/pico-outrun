/*
 * The SD-card failsafe: build the game data image in PSRAM from a raw OutRun
 * romset when no data .uf2 has been flashed.
 *
 * This is where FatFs, PSRAM and the progress reporting live, so that
 * outrun_data.c stays pure C with no framework dependency and outrun_pack.c
 * stays free of both (the host tool compiles it too).
 *
 * The user copies their unmodified MAME 'outrun' revision B ROM files into
 * /roms/ORUN and that is the whole procedure - no conversion, no host tools.
 * The board does what tools/mkoutrundata would have done, using the very same
 * packer, and produces a byte-identical image.
 *
 * It is a failsafe, not the recommended route: it costs a few seconds at every
 * boot and about 2.7 MB of PSRAM at its peak. Flashing outrun-data.uf2 costs
 * neither.
 */

#include "outrun_data.h"
#include "outrun_data_priv.h"
#include "outrun_pack.h"

#include <cstdio>
#include <cstring>

#include "ff.h"
#include "tusb.h"

#include "FrensHelpers.h"

/* Headroom left free after the image and the packer's scratch, so that the
 * engine, the settings menu and lwmem's own per-block overhead still fit. The
 * engine's own PSRAM use is about 214 KB (Video::pixels, hwtiles, hwsprites).
 * 512 KB is the same figure Frens::flashromtoPsram reserves. */
#define OUTRUN_SD_MARGIN (512u * 1024u)

/* 31 ROM files, then tiles, road, sprites and the closing checksum. */
#define OUTRUN_SD_STEPS (OUTRUN_PACK_FILE_COUNT + 4)

// ---------------------------------------------------------------------------
// Failure state, reported by the accessors in outrun_data.h.
// ---------------------------------------------------------------------------

static outrun_data_error_t s_err;
static int s_detail;
static const char *s_bad[OUTRUN_DATA_MAX_BAD_FILES];
static int s_bad_count;
static char s_romdir[96];

static void fail(outrun_data_error_t err, int detail)
{
    s_err = err;
    s_detail = detail;
}

outrun_data_error_t outrun_data_error(void)
{
    return s_err;
}

int outrun_data_error_detail(void)
{
    return s_detail;
}

int outrun_data_bad_file_count(void)
{
    return s_bad_count;
}

const char *outrun_data_bad_file(int idx)
{
    if (idx < 0 || idx >= s_bad_count || idx >= OUTRUN_DATA_MAX_BAD_FILES)
    {
        return NULL;
    }
    return s_bad[idx];
}

const char *outrun_data_romdir(void)
{
    return s_romdir;
}

// ---------------------------------------------------------------------------
// FatFs side of outrun_pack_io_t
// ---------------------------------------------------------------------------

namespace
{
    struct LoadCtx
    {
        const char *dir;
        FIL *fil;
        FRESULT fres;
        outrun_data_progress_fn progress;
        /* Counted separately, because files and decode phases interleave: the
         * tiles are decoded the moment the last tiles file has been read, not
         * after all 31. Summing two monotonic counters keeps the bar monotonic. */
        int files_done;
        int phases_done;
        uint32_t mark_us; /* start of the phase currently being timed */
        uint32_t read_us; /* accumulated across the three read stretches */
    };
} // namespace

static void *io_open(void *ctx, const char *name)
{
    LoadCtx *c = (LoadCtx *)ctx;
    char path[128];

    snprintf(path, sizeof(path), "%s/%s", c->dir, name);
    FRESULT fr = f_open(c->fil, path, FA_READ);
    if (fr != FR_OK)
    {
        return nullptr;
    }
    return c->fil;
}

static long io_read(void *ctx, void *handle, void *buf, uint32_t len)
{
    LoadCtx *c = (LoadCtx *)ctx;
    UINT br = 0;

    FRESULT fr = f_read((FIL *)handle, buf, len, &br);

    /* Called once per 64 KB, which makes it the only place in the whole load
     * that runs often enough to keep USB alive and the LED ticking. The decoders
     * deliberately do not do this: they are verbatim transcriptions that
     * hosttest/verify_decode certifies, and sprinkling callbacks through them
     * would defeat that. A ~200 ms gap before enumeration completes is fine. */
    static bool led;
    led = !led;
    Frens::blinkLed(led);
    tuh_task();

    if (fr != FR_OK)
    {
        c->fres = fr;
        return -1;
    }
    return (long)br;
}

static void io_close(void *ctx, void *handle)
{
    (void)ctx;
    f_close((FIL *)handle);
}

static void io_on_file(void *ctx, int done, int total, const char *name, uint32_t got,
                       uint32_t want_crc, uint32_t got_crc, outrun_file_state_t state)
{
    LoadCtx *c = (LoadCtx *)ctx;

    switch (state)
    {
    case OUTRUN_FILE_OK:
        printf("[data] pack: %2d/%d %-16s %6lu bytes crc %08lx ok\n", done, total, name,
               (unsigned long)got, (unsigned long)got_crc);
        break;
    case OUTRUN_FILE_MISSING:
        printf("[data] pack: %2d/%d %-16s MISSING\n", done, total, name);
        break;
    case OUTRUN_FILE_SHORT:
        printf("[data] pack: %2d/%d %-16s SHORT read %lu bytes (FatFs %d)\n", done, total, name,
               (unsigned long)got, (int)c->fres);
        break;
    case OUTRUN_FILE_BAD_CRC:
        printf("[data] pack: %2d/%d %-16s BAD CRC expected %08lx found %08lx\n", done, total, name,
               (unsigned long)want_crc, (unsigned long)got_crc);
        break;
    }

    if (state != OUTRUN_FILE_OK && s_bad_count < OUTRUN_DATA_MAX_BAD_FILES)
    {
        s_bad[s_bad_count] = name;
    }
    if (state != OUTRUN_FILE_OK)
    {
        s_bad_count++;
    }

    c->files_done = done;
    if (c->progress)
    {
        /* The label carries the count, so the caller never has to work out
         * which of the interleaved counters a given step belongs to. */
        static char label[32];
        snprintf(label, sizeof(label), "%s (%d/%d)", name, done, total);
        c->progress(c->files_done + c->phases_done, OUTRUN_SD_STEPS, label);
    }
}

static void io_on_phase(void *ctx, const char *phase)
{
    LoadCtx *c = (LoadCtx *)ctx;

    const uint32_t now = (uint32_t)Frens::time_us();
    const uint32_t elapsed = now - c->mark_us;
    c->mark_us = now;

    /* "read" closes a stretch of file reading; the other phases are one decode
     * each. Accumulate the read time so it can be reported once at the end. */
    if (strcmp(phase, "read") == 0)
    {
        c->read_us += elapsed;
        return;
    }

    printf("[data] pack: %s decoded in %lu ms\n", phase, (unsigned long)(elapsed / 1000u));

    c->phases_done++;
    if (c->progress)
    {
        static char label[32];
        snprintf(label, sizeof(label), "decoding %s", phase);
        c->progress(c->files_done + c->phases_done, OUTRUN_SD_STEPS, label);
    }
}

// ---------------------------------------------------------------------------
// Finding the romset
// ---------------------------------------------------------------------------

/* One file from each region family, so a partial set still identifies the
 * directory rather than being reported as "no ROM files here". */
static const int probe_idx[] = {0, 8, 16, 24, 25};

static int count_probe_hits(const char *dir)
{
    FILINFO *fno = (FILINFO *)Frens::f_malloc(sizeof(FILINFO));
    if (!fno)
    {
        return 0;
    }

    int hits = 0;
    for (unsigned i = 0; i < sizeof(probe_idx) / sizeof(probe_idx[0]); i++)
    {
        char path[128];
        snprintf(path, sizeof(path), "%s/%s", dir, outrun_pack_filename(probe_idx[i]));
        if (f_stat(path, fno) == FR_OK)
        {
            hits++;
        }
    }

    Frens::f_free(fno);
    return hits;
}

/* /roms/ORUN first, then /roms/ORUN/outrun - extracting a MAME set commonly
 * produces the latter. Whichever looks more like a romset wins. */
static bool choose_romdir(const char *romdir)
{
    char nested[96];
    snprintf(nested, sizeof(nested), "%s/outrun", romdir);

    const int hits_flat = count_probe_hits(romdir);
    const int hits_nested = count_probe_hits(nested);

    printf("[data] sd: probing %s (%d/%u) and %s (%d/%u)\n", romdir, hits_flat,
           (unsigned)(sizeof(probe_idx) / sizeof(probe_idx[0])), nested, hits_nested,
           (unsigned)(sizeof(probe_idx) / sizeof(probe_idx[0])));

    if (hits_flat == 0 && hits_nested == 0)
    {
        snprintf(s_romdir, sizeof(s_romdir), "%s", romdir);
        return false;
    }

    snprintf(s_romdir, sizeof(s_romdir), "%s", hits_nested > hits_flat ? nested : romdir);
    printf("[data] sd: using %s\n", s_romdir);
    return true;
}

// ---------------------------------------------------------------------------

static bool build_from_sd(outrun_data_progress_fn progress)
{
    const uint32_t image_size = outrun_pack_image_size();
    const uint32_t need = image_size + OUTRUN_PACK_OVERHEAD + OUTRUN_SD_MARGIN;
    const uint32_t avail = Frens::GetAvailableMemory();

    printf("[data] sd: need %lu image + %u scratch + %u margin = %lu, PSRAM free %lu\n",
           (unsigned long)image_size, (unsigned)OUTRUN_PACK_OVERHEAD, (unsigned)OUTRUN_SD_MARGIN,
           (unsigned long)need, (unsigned long)avail);

    if (avail < need)
    {
        printf("[data] sd: %lu bytes short\n", (unsigned long)(need - avail));
        fail(OUTRUN_DATA_ERR_NO_MEMORY, (int)((need - avail) / 1024u));
        return false;
    }

    /* Checked above, because Frens::f_malloc panics rather than returning NULL. */
    uint8_t *image = (uint8_t *)Frens::f_malloc(image_size);
    uint8_t *scratch = (uint8_t *)Frens::f_malloc(OUTRUN_PACK_CHUNK);
    uint8_t *src = (uint8_t *)Frens::f_malloc(OUTRUN_PACK_SRC_SIZE);
    FIL *fil = (FIL *)Frens::f_malloc(sizeof(FIL));

    printf("[data] sd: image at %p, scratch at %p, src at %p\n", (void *)image, (void *)scratch,
           (void *)src);

    /* The engine reads TILES and SPRITES as uint32 straight out of this buffer.
     * lwmem aligns to 4, so this holds - but say so rather than assume it. */
    if (((uintptr_t)image & 3u) != 0)
    {
        printf("[data] sd: image is not 4-byte aligned - cannot use it\n");
        Frens::f_free(image);
        Frens::f_free(scratch);
        Frens::f_free(src);
        Frens::f_free(fil);
        fail(OUTRUN_DATA_ERR_BAD_IMAGE, 0);
        return false;
    }

    LoadCtx c;
    memset(&c, 0, sizeof(c));
    c.dir = s_romdir;
    c.fil = fil;
    c.fres = FR_OK;
    c.progress = progress;
    c.mark_us = (uint32_t)Frens::time_us();
    const uint32_t start_us = c.mark_us;

    outrun_pack_io_t io;
    memset(&io, 0, sizeof(io));
    io.open = io_open;
    io.read = io_read;
    io.close = io_close;
    io.on_file = io_on_file;
    io.on_phase = io_on_phase;
    io.ctx = &c;

    outrun_pack_status_t st;
    outrun_pack_result_t rc = outrun_pack_build(&io, image, image_size, scratch,
                                                OUTRUN_PACK_CHUNK, src, OUTRUN_PACK_SRC_SIZE, &st);

    printf("[data] pack: %d files read in %lu ms\n", OUTRUN_PACK_FILE_COUNT,
           (unsigned long)(c.read_us / 1000u));

    Frens::f_free(scratch);
    Frens::f_free(src);
    Frens::f_free(fil);

    if (rc != OUTRUN_PACK_OK)
    {
        printf("[data] pack: FAILED - %d missing, %d short, %d bad CRC, of %d files\n", st.missing,
               st.short_read, st.bad_crc, OUTRUN_PACK_FILE_COUNT);
        Frens::f_free(image);

        switch (rc)
        {
        case OUTRUN_PACK_ERR_MISSING:
            fail(OUTRUN_DATA_ERR_MISSING, st.missing);
            break;
        case OUTRUN_PACK_ERR_SHORT:
            fail(OUTRUN_DATA_ERR_READ, (int)c.fres);
            break;
        case OUTRUN_PACK_ERR_BAD_CRC:
            fail(OUTRUN_DATA_ERR_BAD_CRC, st.bad_crc);
            break;
        default:
            fail(OUTRUN_DATA_ERR_BAD_IMAGE, (int)rc);
            break;
        }
        return false;
    }

    printf("[data] pack: image %lu bytes, total %lu ms\n", (unsigned long)st.image_size,
           (unsigned long)(((uint32_t)Frens::time_us() - start_us) / 1000u));

    if (!outrun_data_adopt_psram(image, image_size))
    {
        Frens::f_free(image);
        fail(OUTRUN_DATA_ERR_BAD_IMAGE, 0);
        return false;
    }

    return true;
}

bool outrun_data_init(const char *romdir, bool sd_mounted, outrun_data_progress_fn progress)
{
    s_err = OUTRUN_DATA_ERR_NONE;
    s_detail = 0;
    s_bad_count = 0;
    s_romdir[0] = 0;

    if (outrun_data_try_flash())
    {
        printf("[data] using FLASH image at %p\n", (const void *)outrun_data_base());
        return true;
    }

    printf("[data] falling back to the SD card\n");

    if (!Frens::isPsramEnabled())
    {
        printf("[data] sd: this board has no PSRAM - there is nowhere to build the image\n");
        fail(OUTRUN_DATA_ERR_NO_PSRAM, 0);
        return false;
    }
    if (!sd_mounted)
    {
        printf("[data] sd: no SD card\n");
        fail(OUTRUN_DATA_ERR_NO_SD, 0);
        return false;
    }
    if (!romdir || !romdir[0])
    {
        fail(OUTRUN_DATA_ERR_NO_ROMS, 0);
        return false;
    }

    if (!choose_romdir(romdir))
    {
        printf("[data] sd: no OutRun ROM files in %s\n", s_romdir);
        fail(OUTRUN_DATA_ERR_NO_ROMS, 0);
        return false;
    }

    Frens::dumpHeapStats("before sd pack");
    const bool ok = build_from_sd(progress);
    Frens::dumpHeapStats("after sd pack");

    if (ok)
    {
        printf("[data] using PSRAM image at %p, built from %s\n", (const void *)outrun_data_base(),
               s_romdir);
    }
    return ok;
}
