/*
 * picoOutRun's replacement for cannonball/src/main/romloader.cpp.
 *
 * Upstream's RomLoader reads a file off disk, CRC-checks it with Boost, and
 * scatters it into a buffer. picoOutRun has none of that: every ROM region was
 * decoded and packed at build time by tools/mkoutrundata and is read straight
 * out of flash, and the CRC checking happened there, on the host, where a
 * mismatch can actually be reported to whoever supplied the ROM set.
 *
 * So what survives is the part the translated 68000 code actually uses - `rom`,
 * `length`, and the read8/16/32 accessors in the header - plus set_flash() to
 * point an instance at a region without copying it.
 */

#include "romloader.hpp"

#include <cstdlib>
#include <cstring>

RomLoader::RomLoader()
{
    rom = NULL;
    length = 0;
    loaded = false;
    owned = false;
    load = &RomLoader::load_rom;
}

RomLoader::~RomLoader()
{
    unload();
}

void RomLoader::init(uint32_t length)
{
    unload();
    this->length = length;
    rom = new uint8_t[length];
    memset(rom, 0, length);
    owned = true;
    loaded = false;
}

// Point at a region of the flash data image. Nothing is copied, and nothing
// writes through `rom` - the const is cast away only because the translated 68k
// accessors in romloader.hpp take a non-const pointer.
void RomLoader::set_flash(const uint8_t *data, uint32_t len)
{
    unload();
    rom = const_cast<uint8_t *>(data);
    length = len;
    owned = false;
    loaded = (data != NULL);
}

void RomLoader::unload(void)
{
    if (owned && rom)
    {
        delete[] rom;
    }
    rom = NULL;
    length = 0;
    owned = false;
    loaded = false;
}

/* No filesystem path reaches the engine. Every caller is either satisfied from
 * flash before it gets here, or is a feature this port does not carry:
 *   - omusic's tilemap.bin: pointed at port/tilemap_bin.h via set_flash()
 *   - omusic's tilepatch.bin: never used at 320x224 (s16_x_off is 0)
 *   - trackloader's LayOut files: custom tracks are not supported
 * Returning 1 (failure) leaves `loaded` false, which every caller checks. */
int RomLoader::load_binary(const char * /*filename*/)
{
    loaded = false;
    return 1;
}

int RomLoader::load_rom(const char * /*filename*/, const int /*offset*/, const int /*length*/,
                        const int /*expected_crc*/, const uint8_t /*mode*/, const bool /*verbose*/)
{
    return 1;
}

int RomLoader::load_crc32(const char * /*debug*/, const int /*offset*/, const int /*length*/,
                          const int /*expected_crc*/, const uint8_t /*mode*/, const bool /*verbose*/)
{
    return 1;
}
