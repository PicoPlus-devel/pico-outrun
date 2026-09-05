/*
 * mkoutrundata - pack an OutRun revision B ROM set into picoOutRun's data image.
 *
 *   mkoutrundata <romset-dir> [-o outrun-data.bin] [--check] [-q] [-v]
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
 * The load table and the decoders are NOT here. They live in port/outrun_pack.c,
 * which this tool and the firmware both compile, so that the image this writes
 * and the image the firmware builds at boot from an SD card romset cannot
 * possibly differ. hosttest/verify_decode checks that shared source against
 * Cannonball's own decoders, and therefore checks both users of it at once.
 *
 * All this file supplies is stdio, argument parsing and --check.
 */

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "outrun_pack.h"

static bool quiet;
static bool verbose;

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

/* ------------------------------------------------------------------------- */
/* stdio side of outrun_pack_io_t. ctx is the romset directory.                */
/* ------------------------------------------------------------------------- */

static void *io_open(void *ctx, const char *name)
{
    char path[4096];
    snprintf(path, sizeof(path), "%s/%s", (const char *)ctx, name);
    return fopen(path, "rb");
}

static long io_read(void *ctx, void *handle, void *buf, uint32_t len)
{
    (void)ctx;
    FILE *f = (FILE *)handle;
    size_t n = fread(buf, 1, len, f);
    if (n == 0 && ferror(f))
    {
        return -1;
    }
    return (long)n;
}

static void io_close(void *ctx, void *handle)
{
    (void)ctx;
    fclose((FILE *)handle);
}

static void io_on_file(void *ctx, int done, int total, const char *name, uint32_t got,
                       uint32_t want_crc, uint32_t got_crc, outrun_file_state_t state)
{
    (void)ctx;
    switch (state)
    {
    case OUTRUN_FILE_OK:
        if (verbose)
        {
            info("  %2d/%d  %-16s %6u bytes  crc %08x\n", done, total, name, got, got_crc);
        }
        break;
    case OUTRUN_FILE_MISSING:
        fprintf(stderr, "  MISSING  %s\n", name);
        break;
    case OUTRUN_FILE_SHORT:
        fprintf(stderr, "  SHORT    %-16s read only %u bytes\n", name, got);
        break;
    case OUTRUN_FILE_BAD_CRC:
        fprintf(stderr, "  BAD CRC  %-16s expected %08x, found %08x\n", name, want_crc, got_crc);
        break;
    }
}

/* ------------------------------------------------------------------------- */

static void usage(void)
{
    fprintf(stderr,
            "usage: mkoutrundata <romset-dir> [-o out.bin] [--check] [-q] [-v]\n"
            "\n"
            "  Packs an OutRun revision B ROM set into picoOutRun's data image.\n"
            "  --check verifies the ROM set and, if the output file already exists,\n"
            "          byte-compares against it without writing.\n"
            "  -v      lists every ROM file as it is read.\n");
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
        else if (!strcmp(argv[i], "-v"))
        {
            verbose = true;
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

    const uint32_t total = outrun_pack_image_size();

    uint8_t *image = malloc(total);
    uint8_t *scratch = malloc(OUTRUN_PACK_CHUNK);
    uint8_t *src = malloc(OUTRUN_PACK_SRC_SIZE);
    if (!image || !scratch || !src)
    {
        fprintf(stderr, "out of memory\n");
        return 1;
    }

    outrun_pack_io_t io = {
        .open = io_open,
        .read = io_read,
        .close = io_close,
        .on_file = io_on_file,
        .on_phase = NULL,
        .ctx = (void *)romdir,
    };

    info("Reading OutRun revision B ROM set from %s\n", romdir);
    fflush(stdout); /* so the per-file complaints on stderr stay in order */

    outrun_pack_status_t st;
    outrun_pack_result_t rc = outrun_pack_build(&io, image, total, scratch, OUTRUN_PACK_CHUNK, src,
                                                OUTRUN_PACK_SRC_SIZE, &st);
    free(scratch);
    free(src);

    if (rc == OUTRUN_PACK_ERR_ARGS)
    {
        fprintf(stderr, "internal error: bad buffers passed to outrun_pack_build\n");
        return 1;
    }
    if (rc != OUTRUN_PACK_OK)
    {
        int bad = st.missing + st.short_read + st.bad_crc;
        fprintf(stderr,
                "\n%d of %d ROM files missing or corrupt - cannot build the data image.\n"
                "Expected the MAME 'outrun' (revision B) parent set.\n",
                bad, outrun_pack_file_count());
        return 1;
    }

    info("  %d ROM files loaded, all CRCs match\n", outrun_pack_file_count());
    info("  tiles, sprites and road decoded\n");
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
        if (!have)
        {
            fprintf(stderr, "out of memory\n");
            fclose(f);
            return 1;
        }
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
