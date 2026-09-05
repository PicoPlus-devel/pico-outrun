#include "outrun_data.h"
#include "outrun_data_priv.h"

#include <stdio.h>

/*
 * The container: validate a packed image and hand out its regions, wherever it
 * happens to live. outrun_sdload.cpp decides which image that is.
 *
 * Validation is deliberately cheap: magic, version, and that every region lies
 * inside the declared total, which itself has to fit the room available. The
 * CRC in the header is checked by tools/mkoutrundata --check on the host, not
 * here - a 2.44 MB CRC over XIP at every boot would cost more than it is worth,
 * and a half-written blob fails the bounds checks in practice. The SD path has
 * already CRC-checked every ROM file it read, which is a stronger guarantee.
 *
 * A missing flash image is a normal state, not an error: it is what a board
 * looks like when only the application .uf2 has been flashed.
 */

#ifndef OUTRUN_DATA_ADDR
#error "OUTRUN_DATA_ADDR is not defined - include cmake/OutRunPartition.cmake"
#endif

static const char *const region_name[OUTRUN_REGION_COUNT] = {
    "rom0", "rom1", "tiles", "sprites", "road", "z80", "pcm",
};

static const uint8_t *s_base;
static outrun_data_source_t s_source;

const uint8_t *outrun_data_flash_base(void)
{
    return (const uint8_t *)(OUTRUN_DATA_ADDR);
}

static bool validate(const outrun_data_header_t *h, uint32_t cap)
{
    if (h->magic != OUTRUN_DATA_MAGIC)
    {
        printf("[data] no image at %p (magic %08lx)\n", (const void *)h, (unsigned long)h->magic);
        return false;
    }
    if (h->version != OUTRUN_DATA_VERSION)
    {
        printf("[data] version %lu, expected %u - regenerate the data image\n",
               (unsigned long)h->version, OUTRUN_DATA_VERSION);
        return false;
    }
    if (h->total_size < sizeof(*h) || h->total_size > cap)
    {
        printf("[data] total_size %lu does not fit %lu bytes\n", (unsigned long)h->total_size,
               (unsigned long)cap);
        return false;
    }
    for (int i = 0; i < OUTRUN_REGION_COUNT; i++)
    {
        uint32_t off = h->region[i].offset;
        uint32_t sz = h->region[i].size;
        if (off < sizeof(*h) || sz > h->total_size || off > h->total_size - sz)
        {
            printf("[data] region %d %s (offset %lu size %lu) out of bounds\n", i, region_name[i],
                   (unsigned long)off, (unsigned long)sz);
            return false;
        }
    }
    return true;
}

static void dump(const outrun_data_header_t *h)
{
    printf("[data]   magic %08lx version %lu total %lu crc %08lx\n", (unsigned long)h->magic,
           (unsigned long)h->version, (unsigned long)h->total_size, (unsigned long)h->crc32);
    for (int i = 0; i < OUTRUN_REGION_COUNT; i++)
    {
        printf("[data]   region %d %-7s offset %8lu size %8lu\n", i, region_name[i],
               (unsigned long)h->region[i].offset, (unsigned long)h->region[i].size);
    }
}

bool outrun_data_try_flash(void)
{
    const uint8_t *base = outrun_data_flash_base();
    const outrun_data_header_t *h = (const outrun_data_header_t *)base;

    printf("[data] flash probe at %p (max %lu bytes)\n", (const void *)base,
           (unsigned long)OUTRUN_DATA_MAX_SIZE);

    if (!validate(h, OUTRUN_DATA_MAX_SIZE))
    {
        return false;
    }

    dump(h);
    s_base = base;
    s_source = OUTRUN_DATA_SOURCE_FLASH;
    return true;
}

bool outrun_data_adopt_psram(const uint8_t *base, uint32_t cap)
{
    const outrun_data_header_t *h = (const outrun_data_header_t *)base;

    if (!validate(h, cap))
    {
        printf("[data] the image just built does not validate - this is a bug\n");
        return false;
    }

    dump(h);
    s_base = base;
    s_source = OUTRUN_DATA_SOURCE_PSRAM;
    return true;
}

const uint8_t *outrun_data_base(void)
{
    return s_base;
}

outrun_data_source_t outrun_data_source(void)
{
    return s_source;
}

bool outrun_data_valid(void)
{
    return s_base != NULL;
}

const uint8_t *outrun_data_region(outrun_region_t region, uint32_t *size_out)
{
    if (size_out)
    {
        *size_out = 0;
    }
    if (region < 0 || region >= OUTRUN_REGION_COUNT || !s_base)
    {
        return NULL;
    }

    const outrun_data_header_t *h = (const outrun_data_header_t *)s_base;
    if (h->region[region].size == 0)
    {
        return NULL;
    }
    if (size_out)
    {
        *size_out = h->region[region].size;
    }
    return s_base + h->region[region].offset;
}
