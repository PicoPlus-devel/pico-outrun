# cannonball/

A vendored subset of [Cannonball](https://github.com/djyt/cannonball) by Chris White — the
OutRun engine, in which the original 68000 and Z80 assembler was rewritten in C++. See
`UPSTREAM.txt` for the exact commit and `docs/license.txt` for the licence, which also
governs this repository.

## What was left out, and why

| Upstream path | Status |
| --- | --- |
| `src/main/engine/` | vendored |
| `src/main/hwvideo/` | vendored |
| `src/main/hwaudio/` | vendored |
| `src/main/{video,roms,romloader,trackloader,utils}.*` | vendored |
| `src/main/sdl2/` | **dropped** — SDL2 rendering, audio, input and timing |
| `src/main/frontend/` | **dropped** — in-game menu and `config.xml` (needs Boost) |
| `src/main/directx/` | **dropped** — Windows force feedback |
| `src/main/main.cpp` | **dropped** — replaced by `main.cpp` in the repository root |
| `res/config.xml`, `gamecontrollerdb.txt`, icons | **dropped** |
| `res/tilemap.bin`, `res/tilepatch.bin` | vendored |

The port has no frontend: it boots straight into attract mode, and settings come from the
`pico_shared` menu instead. Everything the dropped directories provided is replaced by
`../port/`.

## Modifying these files

Keep changes minimal and obvious, so the subset stays diffable against upstream.

Made so far:

- **`src/main/stdint.hpp`** — `BOOST_STATIC_ASSERT_MSG` is now a one-line macro over C++11
  `static_assert`. Upstream used Boost here solely for compile-time size checks and its own
  comment invites the change; this removes Boost from the port entirely.

Expected as the port progresses:

- **Large arrays move to flash.** `hwtiles::tiles`, `hwtiles::tiles_backup`,
  `hwsprites::sprites` and `hwroad::roads` are about 1.5 MB of RAM upstream. In this port
  they are decoded at build time by `../tools/mkoutrundata` and read from flash;
  `tiles_backup` disappears entirely, because the flash copy is itself the unmodified
  original.
- **`Video::pixels` disappears.** The renderers write through a palette lookup straight into
  the framebuffer that `pico_shared` already owns, instead of into a separate 143 KB buffer
  of palette indices.

The decode loops in `hwtiles::init`, `hwsprites::init` and `HWRoad::decode_road` are the
authoritative definition of the on-flash format. `../tools/mkoutrundata.c` reproduces them
and cites them by file and function. If you change one, change both, then run
`./hosttest/build.sh check` — it compiles these three files unmodified and byte-compares
their output against the packed image, so drift fails loudly instead of becoming a subtle
graphics bug on hardware.
