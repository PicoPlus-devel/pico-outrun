/*
 * Access to the OutRun game data.
 *
 * The data is NOT part of this application image. There are two ways it can get
 * onto a board, and the firmware tries them in this order:
 *
 *   1. FLASH. tools/mkoutrundata packs the user's OutRun revision B romset into
 *      outrun-data.bin, and picotool converts that to an RP2350 DATA-family .uf2
 *      flashed at OUTRUN_DATA_ADDR. Read directly out of XIP; nothing is copied.
 *      This is the fast path: no boot delay at all.
 *
 *   2. SD CARD. When no image has been flashed, the same picture is built at
 *      boot in PSRAM from the raw romset in /roms/ORUN, using the very same
 *      packer (port/outrun_pack.c). Costs a few seconds and about 2.7 MB of
 *      PSRAM, and asks nothing of the user but their unmodified ROM files.
 *
 * Either way the engine sees one thing: outrun_data_region(). It has no idea
 * which of the two it is reading, and does not need to.
 *
 * OUTRUN_DATA_ADDR comes from cmake/OutRunPartition.cmake so that the compile
 * definition, the picotool -o and the application size cap cannot drift apart.
 * Only outrun_data.c uses it - this header is deliberately free of it, so that
 * the host packer can include it for the container format alone.
 */

#ifndef OUTRUN_DATA_H
#define OUTRUN_DATA_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/* Region order is fixed by the packed format; append only. */
typedef enum
{
    OUTRUN_REGION_ROM0 = 0, /* master 68k, byte-interleaved   0x40000 */
    OUTRUN_REGION_ROM1,     /* slave 68k, byte-interleaved    0x40000 */
    OUTRUN_REGION_TILES,    /* decoded, pre-patched, uint32   0x40000 */
    OUTRUN_REGION_SPRITES,  /* decoded, uint32              0x100000 */
    OUTRUN_REGION_ROAD,     /* decoded                       0x40200 */
    OUTRUN_REGION_Z80,      /* sound data tables               0x8000 */
    OUTRUN_REGION_PCM,      /* samples                        0x30000 */
    OUTRUN_REGION_COUNT
} outrun_region_t;

#define OUTRUN_DATA_MAGIC 0x4E55524Fu /* 'ORUN' little-endian */
#define OUTRUN_DATA_VERSION 1u

typedef struct
{
    uint32_t magic;
    uint32_t version;
    uint32_t total_size;                    /* header + all regions            */
    uint32_t crc32;                         /* over everything after this field */
    struct
    {
        uint32_t offset;                    /* from the start of the header    */
        uint32_t size;
    } region[OUTRUN_REGION_COUNT];
} outrun_data_header_t;

/* ------------------------------------------------------------------------- */
/* Where the data came from, and why it did not.                               */
/* ------------------------------------------------------------------------- */

typedef enum
{
    OUTRUN_DATA_SOURCE_NONE = 0,
    OUTRUN_DATA_SOURCE_FLASH, /* the DATA .uf2 at OUTRUN_DATA_ADDR       */
    OUTRUN_DATA_SOURCE_PSRAM  /* built at boot from the romset on the SD */
} outrun_data_source_t;

typedef enum
{
    OUTRUN_DATA_ERR_NONE = 0,
    OUTRUN_DATA_ERR_NO_PSRAM,  /* board has no PSRAM - cannot run at all      */
    OUTRUN_DATA_ERR_NO_SD,     /* no card, or it would not mount              */
    OUTRUN_DATA_ERR_NO_ROMS,   /* directory absent, or none of the 30 present */
    OUTRUN_DATA_ERR_MISSING,   /* some of the 30 absent                       */
    OUTRUN_DATA_ERR_BAD_CRC,   /* present, but the wrong set or corrupt       */
    OUTRUN_DATA_ERR_READ,      /* truncated file or an SD read error          */
    OUTRUN_DATA_ERR_NO_MEMORY, /* not enough free PSRAM to build the image    */
    OUTRUN_DATA_ERR_BAD_IMAGE  /* built image failed validation - a bug here  */
} outrun_data_error_t;

/* Called as the image is built, so the caller can show progress. `step` runs
 * 0..steps, and `what` is a short label - a ROM file name, or a decode stage. */
typedef void (*outrun_data_progress_fn)(int step, int steps, const char *what);

/*
 * Locate the game data: the flash image first, then the romset on the SD card.
 * Call ONCE, from main(), after Frens::initAll() (both PSRAM and the SD card
 * have to be up) and after the settings have been loaded (so `romdir` is final).
 *
 * `romdir` is normally settings.currentDir, i.e. /roms/ORUN; `<romdir>/outrun`
 * is also tried, since that is where extracting a MAME set commonly puts them.
 *
 * Returns true when a usable image is in place. On false, outrun_data_error()
 * and the accessors below describe the failure well enough to put on screen.
 */
bool outrun_data_init(const char *romdir, bool sd_mounted, outrun_data_progress_fn progress);

outrun_data_source_t outrun_data_source(void);
outrun_data_error_t outrun_data_error(void);

/* An FRESULT for OUTRUN_DATA_ERR_READ, otherwise a count whose meaning depends
 * on the error (files missing, files with a bad CRC, KB of PSRAM short). */
int outrun_data_error_detail(void);

/* The offending ROM files, for the error screen. The count is the true total,
 * which may exceed the number of names actually retained. */
#define OUTRUN_DATA_MAX_BAD_FILES 4
int outrun_data_bad_file_count(void);
const char *outrun_data_bad_file(int idx); /* NULL past the end */

/* The directory the romset was actually looked for in. */
const char *outrun_data_romdir(void);

/* ------------------------------------------------------------------------- */
/* The image itself                                                            */
/* ------------------------------------------------------------------------- */

/* Base of whichever image is in use, or NULL before a successful init. */
const uint8_t *outrun_data_base(void);

/* Where the flashed image would be, for diagnostics only. */
const uint8_t *outrun_data_flash_base(void);

/* True once outrun_data_init() has found a usable image. */
bool outrun_data_valid(void);

/* NULL when there is no image, or the region is empty. */
const uint8_t *outrun_data_region(outrun_region_t region, uint32_t *size_out);

#ifdef __cplusplus
}
#endif

#endif /* OUTRUN_DATA_H */
