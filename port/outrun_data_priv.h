/*
 * Internal seam between outrun_data.c (the container: validate, hold the base
 * pointer, hand out regions) and outrun_sdload.cpp (how we got it: FatFs, PSRAM,
 * the progress screen and the error state).
 *
 * The split keeps outrun_data.c pure C with no framework dependency, which is
 * what lets the host packer include outrun_data.h for the format alone.
 */

#ifndef OUTRUN_DATA_PRIV_H
#define OUTRUN_DATA_PRIV_H

#include "outrun_data.h"

#ifdef __cplusplus
extern "C"
{
#endif

/* Probe OUTRUN_DATA_ADDR and adopt the image there if it is well formed. Logs
 * what it found either way. */
bool outrun_data_try_flash(void);

/* Adopt an image built in RAM. `cap` is the size of the allocation, which is the
 * bound the header is checked against. Logs and returns false if it does not
 * validate - which would be a bug in the packer, not a user error. */
bool outrun_data_adopt_psram(const uint8_t *base, uint32_t cap);

#ifdef __cplusplus
}
#endif

#endif /* OUTRUN_DATA_PRIV_H */
