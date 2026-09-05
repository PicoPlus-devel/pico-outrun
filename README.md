# pico-outrun

A port of the arcade game OutRun to RP2350 boards with DVI/HDMI output, built on the
`pico_shared` framework used by the Frens emulator family.

The game engine is [Cannonball](https://github.com/djyt/cannonball) by Chris White, in which
the original 68000 and Z80 assembler has been rewritten in C++. This is therefore a native
port rather than an arcade emulator.

## Status

Early development. The current build is a skeleton: it initialises the framework, the
display and the input devices on every supported board, and draws a test pattern. The game
engine is not yet integrated. See `CHANGELOG.md` for what each release contains.

## Game data

**The OutRun revision B ROM set is not included and must be supplied by the user.** It is
copyright SEGA and cannot be distributed with this project or with its releases. The
released `.uf2` files contain no ROM data.

The data is not read from the SD card at runtime. Instead, `tools/mkoutrundata` converts a
ROM set into `outrun-data.uf2`, a data image that is flashed once to a fixed address and
read directly from flash by the running application. The conversion performs, at build time,
the tile, sprite and road decoding that Cannonball would otherwise do into RAM at startup,
which is what allows the port to fit on an RP2350.

If the data image has not been flashed, the application reports that the game data is
missing rather than failing silently.

## Supported hardware

RP2350 (Pico 2 and compatible) only. The RP2350 has 520 KB of SRAM; the RP2040's 264 KB is
not sufficient for the engine's working set, so the RP2040-only board configurations
(`HW_CONFIG` 3 and 4) are not supported.

| `HW_CONFIG` | Board | Video |
| --- | --- | --- |
| 1 | Pimoroni Pico DV Demo Base | PicoDVI |
| 2 | Adafruit DVI + microSD breakouts / PicoNES PCB | HSTX |
| 5 | Adafruit Metro RP2350 | HSTX |
| 6 | RP2350-Zero with custom PCB | PicoDVI |
| 7 | Waveshare RP2350-PiZero | PicoDVI |
| 8 | Adafruit Fruit Jam | HSTX |
| 9 | Waveshare RP2350-USB-A | PicoDVI |
| 10 | Spotpear HDMI | PicoDVI |
| 12 | Murmulator M1 | PicoDVI |
| 13 | Murmulator M2 | HSTX |
| 14 | Adafruit Feather RP2350 with TLV320DAC3100 | HSTX |

Boards without HSTX pins use the PicoDVI driver. The selection is automatic and is derived
from the pin definitions in `pico_shared/BoardConfigs.cmake`; no configuration is required.
`./bld.sh -D` forces the PicoDVI path on an HSTX-capable board.

## Building

Requires the Pico SDK (`PICO_SDK_PATH`), `picotool`, and `PICO_PIO_USB_PATH` for the
configurations that use PIO USB (7, 8, 9 and 14).

```sh
./bld.sh -c 8 -2            # one configuration; result in releases/
./bld.sh -h                 # all options
./buildAll.sh               # every supported configuration; what CI runs
```

Every configuration must be built with `-2` (RP2350). To iterate without a full
reconfiguration, run `./bld.sh -m -c 8 -2` once and then `make -j$(nproc) -C build`;
`bld.sh` deletes and recreates `build/` on each run.

## Flashing

Two images are required, both flashed over BOOTSEL or with `picotool`:

1. `releases/picoOutRun_<board>.uf2` — the application.
2. `outrun-data.uf2` — the game data, generated locally from your own ROM set.

The data image only needs to be flashed again when it changes.

## Licence

The port is distributed under Cannonball's licence; see `LICENSE`. It is not an open-source
licence: redistributions may not be sold or used commercially, and modified redistributions
must include complete source code.

OutRun is a trademark of SEGA Corporation. This project is not affiliated with SEGA.
