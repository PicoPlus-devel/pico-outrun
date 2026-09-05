#include "outrun_data.h"

#include <stdio.h>

/*
 * Validation is deliberately cheap and done once: magic, version, and that
 * every region lies inside the declared total, which itself has to fit the room
 * the flash map left for it. The CRC in the header is checked by
 * tools/mkoutrundata --check on the host, not here - a 2.25 MB CRC over XIP at
 * every boot would cost more than it is worth, and a half-written blob fails
 * the bounds checks in practice.
 *
 * Returning false is a normal state, not an error: it is what a board looks
 * like when only the application .uf2 has been flashed.
 */

static bool s_checked;
static bool s_valid;

static bool validate(const outrun_data_header_t *h)
{
    if (h->magic != OUTRUN_DATA_MAGIC)
    {
        printf("outrun_data: no blob at %p (magic %08lx)\n", (const void *)h,
               (unsigned long)h->magic);
        return false;
    }
    if (h->version != OUTRUN_DATA_VERSION)
    {
        printf("outrun_data: version %lu, expected %u - regenerate outrun-data.uf2\n",
               (unsigned long)h->version, OUTRUN_DATA_VERSION);
        return false;
    }
    if (h->total_size < sizeof(*h) || h->total_size > OUTRUN_DATA_MAX_SIZE)
    {
        printf("outrun_data: total_size %lu does not fit %u bytes of flash\n",
               (unsigned long)h->total_size, (unsigned)OUTRUN_DATA_MAX_SIZE);
        return false;
    }
    for (int i = 0; i < OUTRUN_REGION_COUNT; i++)
    {
        uint32_t off = h->region[i].offset;
        uint32_t sz = h->region[i].size;
        if (off < sizeof(*h) || sz > h->total_size || off > h->total_size - sz)
        {
            printf("outrun_data: region %d (offset %lu size %lu) out of bounds\n", i,
                   (unsigned long)off, (unsigned long)sz);
            return false;
        }
    }
    return true;
}

bool outrun_data_valid(void)
{
    if (!s_checked)
    {
        s_checked = true;
        s_valid = validate((const outrun_data_header_t *)outrun_data_base());
    }
    return s_valid;
}

const uint8_t *outrun_data_region(outrun_region_t region, uint32_t *size_out)
{
    if (size_out)
    {
        *size_out = 0;
    }
    if (region < 0 || region >= OUTRUN_REGION_COUNT || !outrun_data_valid())
    {
        return NULL;
    }

    const outrun_data_header_t *h = (const outrun_data_header_t *)outrun_data_base();
    if (h->region[region].size == 0)
    {
        return NULL;
    }
    if (size_out)
    {
        *size_out = h->region[region].size;
    }
    return outrun_data_base() + h->region[region].offset;
}
