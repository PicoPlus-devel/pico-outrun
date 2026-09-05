#include "outrun_alloc.hpp"

#include "FrensHelpers.h"

#include <cstdlib>

// Routes to PSRAM when the board has it (Frens::f_malloc -> PicoPlusPsram),
// otherwise to plain malloc. See outrun_alloc.hpp for why this is necessary.
extern "C" void *outrun_psram_alloc(size_t bytes)
{
    return Frens::f_malloc(bytes);
}

extern "C" void outrun_psram_free(void *p)
{
    if (p)
    {
        Frens::f_free(p);
    }
}

extern "C" void *outrun_sram_alloc(size_t bytes)
{
    return malloc(bytes);
}

extern "C" void outrun_sram_free(void *p)
{
    free(p);
}
