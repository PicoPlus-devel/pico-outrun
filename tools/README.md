# tools/

## `mkoutrundata` (not yet implemented — Milestone 2/3)

Converts an OutRun revision B ROM set into `outrun-data.uf2`, the flash-resident game data
image. **The ROM set is supplied by the user and is never committed to this repository or
attached to a release.**

Intended usage:

```sh
tools/mkoutrundata.sh <romset-dir-or-zip> [-b <build-dir>] [-o outrun-data.uf2]
tools/mkoutrundata --check <romset-dir-or-zip>     # verify without writing
```

The wrapper reads `OUTRUN_DATA_ADDR` from `<build-dir>/outrun_flashmap.env`, which
`cmake/OutRunPartition.cmake` generates, and passes it to:

```sh
picotool uf2 convert outrun-data.bin -t bin outrun-data.uf2 -o $OUTRUN_DATA_ADDR --family data
```

so the flash address can never disagree with the address compiled into the application.

### Output format

Defined by `port/outrun_data.h`, which is the authoritative specification: a header
(magic `ORUN`, version, total size, CRC32, and an offset/size pair per region) followed by
the regions in enum order.

Regions `TILES`, `SPRITES` and `ROAD` are stored **already decoded**. Upstream Cannonball
performs those conversions at startup into roughly 1.5 MB of RAM (`hwtiles::tiles` plus
`tiles_backup`, `hwsprites::sprites`, `hwroad::roads`), which does not fit on an RP2350.
Doing them here puts the result in flash instead, and makes `tiles_backup` unnecessary
because the flash copy is itself the unmodified original.

`TILES` is stored **unpatched**. `hwtiles::patch_tiles()` is not a bug fix applied globally:
upstream calls it only on the music selection screen and only in widescreen
(`omusic.cpp`, guarded by `config.s16_x_off > 0`), reverting it via `restore_tiles()` on
exit. picoOutRun renders 320×224, where `video.cpp` leaves `s16_x_off` at 0, so the patch
never runs and `res/tilepatch.bin` is unused.

### Verifying the decoders

The decode loops here are transcribed from Cannonball, and a transcription can drift from
its source. `hosttest/verify_decode` guards against that: it links Cannonball's **real,
unmodified** `hwtiles.cpp`, `hwsprites.cpp` and `hwroad.cpp`, runs their `init()` on the same
ROM set, and byte-compares the result against the packed image.

```sh
./hosttest/build.sh check          # pack, then verify; OUTRUN_ROMS overrides the romset path
tools/mkoutrundata --check <roms>  # verify an existing image still matches its romset
```

`hosttest/stubs/` supplies minimal stand-ins for `frontend/config.hpp` and `video.hpp`, whose
real versions pull in Boost and SDL. Neither affects decoding.

### Expected ROM set

26 files, approximately 2.2 MB, the MAME `outrun` (revision B) parent set. The
authoritative list, with CRCs, is `roms.cpp` in the Cannonball source.

| Region | Files |
| --- | --- |
| `ROM0` (master 68000) | `epr-10380b.133`, `epr-10382b.118`, `epr-10381b.132`, `epr-10383b.117` |
| `ROM1` (slave 68000) | `epr-10327a.76`, `epr-10329a.58`, `epr-10328a.75`, `epr-10330a.57` |
| `TILES` | `opr-10268.99`, `opr-10232.102`, `opr-10267.100`, `opr-10231.103`, `opr-10266.101`, `opr-10230.104` |
| `ROAD` | `opr-10185.11`, `opr-10186.47` |
| `SPRITES` | `mpr-10371.9` … `mpr-10378.16` |
| `Z80` | `epr-10187.88` |
| `PCM` | `opr-10193.66` … `opr-10188.71` |

The tool must name any file that is missing or whose CRC does not match, rather than
producing a subtly incorrect image.
