/*
 * outrun_pack - turn an OutRun revision B ROM set into picoOutRun's data image.
 *
 * This is the ONE implementation of the load table and the three decoders. It is
 * compiled into both:
 *
 *   - tools/mkoutrundata, which writes outrun-data.bin on the host, and
 *   - the firmware, which builds the same image in PSRAM at boot when no data
 *     .uf2 has been flashed (port/outrun_sdload.cpp).
 *
 * Keeping it in one place is not tidiness. The decoders are transcribed from
 * Cannonball, a transcription can drift from its source, and the failure mode -
 * subtly wrong graphics on hardware, hours from a debugger - is expensive.
 * hosttest/verify_decode links Cannonball's REAL hwtiles/hwsprites/hwroad and
 * byte-compares against what this produces, so a single shared source means that
 * test certifies the firmware's decoders too. A second copy in port/ would be a
 * third thing to keep in sync, and only one of the three would be tested.
 *
 * The container format is port/outrun_data.h, which this file fills in.
 *
 * No libc file I/O: the caller supplies open/read/close, because the host uses
 * stdio and the firmware uses FatFs. No allocation either - the caller supplies
 * every buffer, because the host has malloc and the firmware has
 * Frens::f_malloc, which panics rather than returning NULL.
 */

#ifndef OUTRUN_PACK_H
#define OUTRUN_PACK_H

#include <stdbool.h>
#include <stdint.h>

#include "outrun_data.h"

#ifdef __cplusplus
extern "C"
{
#endif

/* The MAME 'outrun' revision B parent set: 4 master 68k + 4 slave 68k + 6 tile
 * + 2 road + 8 sprite + 1 Z80 + 6 PCM. */
#define OUTRUN_PACK_FILE_COUNT 31

/* Buffer sizes the caller must provide. See outrun_pack_build().
 *
 * CHUNK is the file read granularity, and therefore how often the firmware gets
 * to service USB while loading. SRC_SIZE is the tiles source region, the largest
 * that cannot be decoded in place; the road source reuses its first 64 KB, since
 * the tiles are fully decoded before the road files are read. */
#define OUTRUN_PACK_CHUNK 0x10000u    /*  64 KB */
#define OUTRUN_PACK_SRC_SIZE 0x30000u /* 192 KB */

/* Peak transient cost on top of the image itself: 256 KB. */
#define OUTRUN_PACK_OVERHEAD (OUTRUN_PACK_CHUNK + OUTRUN_PACK_SRC_SIZE)

typedef enum
{
    OUTRUN_FILE_OK = 0,
    OUTRUN_FILE_MISSING, /* could not be opened                       */
    OUTRUN_FILE_SHORT,   /* opened, but ended early or read failed    */
    OUTRUN_FILE_BAD_CRC  /* right length, wrong contents              */
} outrun_file_state_t;

typedef struct
{
    /* Open `name` for reading. Return NULL when it does not exist. */
    void *(*open)(void *ctx, const char *name);

    /* Read up to `len` bytes. Return the number read (0 at end of file), or a
     * negative value on an I/O error. The firmware pumps USB and blinks the LED
     * here: it is called once per OUTRUN_PACK_CHUNK, which is the only place in
     * this file that runs often enough to matter. */
    long (*read)(void *ctx, void *handle, void *buf, uint32_t len);

    void (*close)(void *ctx, void *handle);

    /* Called once per ROM file, after it has been read and checked, with
     * everything a log line or a progress bar needs. Optional. */
    void (*on_file)(void *ctx, int done, int total, const char *name, uint32_t got,
                    uint32_t want_crc, uint32_t got_crc, outrun_file_state_t state);

    /* Called as each phase COMPLETES: "read", "tiles", "road", "sprites", "crc".
     * The caller times the intervals itself, so this file needs no clock - which
     * is what lets it compile unchanged for the host and the firmware. Optional. */
    void (*on_phase)(void *ctx, const char *phase);

    void *ctx;
} outrun_pack_io_t;

typedef enum
{
    OUTRUN_PACK_OK = 0,
    OUTRUN_PACK_ERR_ARGS,    /* a buffer was missing or too small */
    OUTRUN_PACK_ERR_MISSING, /* one or more ROM files absent      */
    OUTRUN_PACK_ERR_SHORT,   /* one or more truncated or unreadable */
    OUTRUN_PACK_ERR_BAD_CRC  /* one or more with the wrong contents */
} outrun_pack_result_t;

typedef struct
{
    outrun_pack_result_t result;
    int missing;
    int short_read;
    int bad_crc;
    const char *first_bad; /* points into the load table; NULL when none */
    uint32_t image_size;
} outrun_pack_status_t;

/* Exact size of the packed image: header plus every region, each 4-byte aligned.
 * Computed from the same layout the builder uses, so there is no constant here
 * that could disagree with the image. */
uint32_t outrun_pack_image_size(void);

/* The expected ROM file names, for probing a directory before committing to it. */
int outrun_pack_file_count(void);
const char *outrun_pack_filename(int idx);

/*
 * Build the image.
 *
 *   image    outrun_pack_image_size() bytes. Zeroed here - the Z80 region's
 *            upper half and the gaps between the banked PCM samples are defined
 *            to be zero, and nothing writes them.
 *   scratch  OUTRUN_PACK_CHUNK bytes, the file read buffer.
 *   src      OUTRUN_PACK_SRC_SIZE bytes, the tiles and road source regions.
 *
 * Every ROM file is attempted even after one fails, so the caller can report all
 * of them rather than only the first. On failure the image is left incomplete
 * and must be discarded; `status` says what went wrong.
 */
outrun_pack_result_t outrun_pack_build(const outrun_pack_io_t *io, uint8_t *image,
                                       uint32_t image_size, uint8_t *scratch,
                                       uint32_t scratch_size, uint8_t *src, uint32_t src_size,
                                       outrun_pack_status_t *status);

#ifdef __cplusplus
}
#endif

#endif /* OUTRUN_PACK_H */
