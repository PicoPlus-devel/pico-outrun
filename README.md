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

The tile, sprite and road ROMs are decoded into a single data image before use. Cannonball
performs that decoding into approximately 1.5 MB of RAM at startup, which an RP2350 does not
have, so picoOutRun does it ahead of time instead. There are two ways to provide the result,
and the application tries them in this order.

**1. Flashed (recommended).** `tools/mkoutrundata.sh` converts a ROM set into
`outrun-data.uf2`, a data image flashed once to a fixed address and read directly from flash
while the game runs. This is the fastest route: the game starts immediately.

```sh
./bld.sh -m -c 8 -2                    # once, to establish the flash address
tools/mkoutrundata.sh ~/roms/outrun    # writes outrun-data.uf2
```

**2. From the SD card.** If no data image has been flashed, the application looks for the
ROM set itself in `/roms/ORUN` on the SD card, and builds the same image in PSRAM at
startup. This requires no host tools at all — the ROM files are copied across unchanged —
but it takes a few seconds on every boot and needs approximately 2.7 MB of free PSRAM.
`/roms/ORUN/outrun` is also searched, since extracting a MAME set commonly produces that
layout.

The ROM files must be extracted; zip archives are not read. Each file is checked against its
expected CRC, so an incomplete set, a corrupt file or a different revision is reported by
name rather than producing a subtly incorrect image.

If neither route has been taken, the application displays a message describing what is
missing and what to do about it, and the settings menu remains reachable with SELECT + START
so that BOOTSEL mode can be entered without unplugging the board.

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

The application, `releases/picoOutRun_<board>.uf2`, is flashed over BOOTSEL or with
`picotool`.

The game data may either be flashed alongside it as `outrun-data.uf2`, generated locally
from your own ROM set, or placed on the SD card as described under "Game data" above. The
data image only needs to be flashed again when it changes.

## Licence

The port is distributed under Cannonball's licence; see `LICENSE`. It is not an open-source
licence: redistributions may not be sold or used commercially, and modified redistributions
must include complete source code.

OutRun is a trademark of SEGA Corporation. This project is not affiliated with SEGA.
