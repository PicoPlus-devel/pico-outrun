/*
 * Access to the OutRun game data held in flash.
 *
 * The data is NOT part of this application image. tools/mkoutrundata packs the
 * user's OutRun revision B romset into outrun-data.bin - the decoded tile,
 * sprite and road tables plus the raw 68k, Z80 and PCM regions - and picotool
 * converts that to an RP2350 DATA-family .uf2 flashed at OUTRUN_DATA_ADDR.
 * Everything here is read directly out of XIP; nothing is copied to RAM.
 *
 * OUTRUN_DATA_ADDR comes from cmake/OutRunPartition.cmake so that the compile
 * definition, the picotool -o and the application size cap cannot drift apart.
 */

#ifndef OUTRUN_DATA_H
#define OUTRUN_DATA_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifndef OUTRUN_DATA_ADDR
#error "OUTRUN_DATA_ADDR is not defined - include cmake/OutRunPartition.cmake"
#endif

#ifdef __cplusplus
extern "C"
{
#endif

/* Region order is fixed by the on-flash format; append only. */
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

static inline const uint8_t *outrun_data_base(void)
{
    return (const uint8_t *)(OUTRUN_DATA_ADDR);
}

/* True when a well-formed, version-matching blob is present. Call before
 * touching any region; a missing blob is the normal state of a board that has
 * only had the application .uf2 flashed. */
bool outrun_data_valid(void);

/* NULL when the blob is absent or the region is empty. */
const uint8_t *outrun_data_region(outrun_region_t region, uint32_t *size_out);

#ifdef __cplusplus
}
#endif

#endif /* OUTRUN_DATA_H */
