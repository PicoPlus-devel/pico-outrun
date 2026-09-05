# pico-outrun

> [!NOTE]
> There is no release yet. No binaries are published, so the only way to run this is to build it yourself — see [Building from source](#building-from-source). The port is under active development.

**pico-outrun** is a port of the arcade game **OutRun** to RP2350-based microcontroller boards with PSRAM, with video and audio over HDMI. The game engine is [Cannonball](https://github.com/djyt/cannonball) by Chris White, in which the original 68000 and Z80 assembler has been rewritten in C++. This is therefore a native port rather than an arcade emulator: the code runs directly on the RP2350 and only the artwork, sound samples and level data come from the original ROMs.

It uses the same menu, display, audio and controller framework as this family of emulators:

- NES: [pico-infonesPlus](https://github.com/PicoPlus-devel/pico-infonesPlus)
- Super Nintendo: [pico-snesPlus](https://github.com/PicoPlus-devel/pico-snesPlus)
- Sega Master System / Game Gear: [pico-smsplus](https://github.com/PicoPlus-devel/pico-smsplus)
- Game Boy / Game Boy Color: [pico-peanutGB](https://github.com/PicoPlus-devel/pico-peanutGB)
- Sega Mega Drive / Genesis: [pico-genesisPlus](https://github.com/PicoPlus-devel/pico-genesisPlus)

It runs on four hardware configurations — see [Supported hardware](#supported-hardware):

- [Adafruit Fruit Jam](https://www.adafruit.com/product/6200) — the primary development and test board
- [Pimoroni Pico Plus 2](https://shop.pimoroni.com/products/pimoroni-pico-plus-2?variant=42092668289107) with an [Adafruit DVI breakout](https://www.adafruit.com/product/4984) and a microSD breakout, on a breadboard or on the [PicoNES PCB](#picones-pcb)
- [Murmulator M2](https://murmulator.ru)
- [Adafruit Feather RP2350 with HSTX Port](https://www.adafruit.com/product/6130) with a TLV320DAC3100 I2S DAC and a microSD breakout

All four are RP2350 boards with 8 MB of PSRAM, which this port requires: a plain Raspberry Pi Pico 2 has none and cannot be used.

**The OutRun ROM set is not included and must be supplied by the user.** It is copyright SEGA and is not distributed with this project. See [Game data](#game-data).

***

## Status and limitations

> [!NOTE]
> This is work in progress. The game runs from attract mode through to a full race with music and sound effects, but it is not finished, and how well it plays is still changing from commit to commit.

Worth knowing before you start:

- **Full speed is not held yet.** Driving a 68000-era arcade game on a microcontroller is demanding, and the frame rate and with it the game speed still vary with what is on screen. Tuning the balance between the engine tick and the renderer is where the current work is; expect the speed to change, in both directions, while that continues.
- **There is no ROM browser.** The board boots straight into OutRun. There is only one game, so there is nothing to choose from and no file menu.
- **No save states, and high scores are not kept.** A high score survives until the board is reset or powered off.
- **A USB controller is required to play.** A pad on the GPIO NES/SNES controller port can steer and press Start and Coin, but accelerate, brake and gear are not mapped to it yet, and the Wii Classic port is not wired to the engine at all. See [Controllers](#controllers).
- **Not launchable from [pico-bootLoader](https://github.com/PicoPlus-devel/pico-bootLoader) yet.** The build can produce a bootloader-format image, but it has not been verified, and *Return to emulator selection* is hidden in the settings menu.
- Development and testing take place primarily on the Adafruit Fruit Jam. The other supported boards need to be more thoroughly tested.

***

## Game data

OutRun's ROM data is copyright SEGA and is not distributed with this project. You have to supply it yourself, from a set you legally own. Required is the MAME **`outrun` revision B parent set**: 31 files, roughly 2.2 MB, extracted — zip archives are not read. [tools/README.md](tools/README.md) lists every file and what it contains.

The tile, sprite and road ROMs are stored in a packed arcade-specific layout and have to be decoded before they can be drawn. Cannonball does that at startup into approximately 1.5 MB of RAM, which an RP2350 does not have, so pico-outrun does it ahead of time and stores the result as a single data image. There are two ways to provide that image, and the application tries them in this order.

**1. Flashed (recommended).** [tools/mkoutrundata.sh](tools/mkoutrundata.sh) converts a ROM set into `outrun-data.uf2`, a data image that is flashed once, to a fixed address just after the application, and read directly from flash while the game runs. This is the fastest route: the game starts immediately, and no SD card is needed to play.

```sh
./bld.sh -m -c 8 -2                    # once, to establish the flash address
tools/mkoutrundata.sh ~/roms/outrun    # writes outrun-data.uf2
```

Flash `outrun-data.uf2` the same way as the application, over BOOTSEL or with `picotool`. It only has to be flashed again when it changes, so reflashing the application later leaves the game data in place.

**2. From the SD card.** If no data image has been flashed, the application looks for the ROM set itself in `/roms/ORUN` on the SD card and builds the same image in PSRAM at startup, showing a progress bar while it does. This requires no host tools at all — the ROM files are copied across unchanged — but it takes a few seconds on every boot and needs roughly 2.7 MB of free PSRAM. `/roms/ORUN/outrun` is searched as well, since extracting a MAME set commonly produces that layout.

Each file is checked against its expected CRC on both routes, so an incomplete set, a corrupt file or a different revision is reported by name rather than producing a subtly incorrect image.

If neither route has been taken, the application displays a screen describing what is missing and what to do about it — no PSRAM, no SD card, no ROMs in the directory it looked in, files missing from the set, or a CRC mismatch. The settings menu remains reachable with **Select + Start** from that screen, so [USB drive mode](#usb-drive-mode) can be used to put the ROM set on the card, and BOOTSEL mode can be entered without unplugging the board.

***

## Supported hardware

An RP2350 board with 8 MB of PSRAM is required, and video must run over HSTX. Only the four hardware configurations below are supported.

| HW_CONFIG | Hardware | Binary |
| --- | --- | --- |
| 2 | [Pimoroni Pico Plus 2](https://shop.pimoroni.com/products/pimoroni-pico-plus-2?variant=42092668289107) with [Adafruit DVI Breakout](https://www.adafruit.com/product/4984) and a microSD breakout, on a breadboard or on the [PicoNES PCB](#picones-pcb) | `picoOutRun_AdafruitDVISD_pico2_arm.uf2` |
| 8 | [Adafruit Fruit Jam](https://www.adafruit.com/product/6200) (primary development and test board) | `picoOutRun_AdafruitFruitJam_arm_piousb.uf2` |
| 13 | [Murmulator M2](https://murmulator.ru) | `picoOutRun_MurmulatorM2_arm.uf2` |
| 14 | [Adafruit Feather RP2350 with HSTX Port](https://www.adafruit.com/product/6130) with TLV320DAC3100 I2S DAC and microSD breakout | `picoOutRun_AdafruitFeatherRP2350_TLV320DAC3100_arm_piousb.uf2` |

Notes per configuration:

- **HW_CONFIG 2**: a plain Raspberry Pi Pico 2 does not work — it has no PSRAM. The Pimoroni Pico Plus 2 (with onboard PSRAM) is required. The [PicoNES PCB](#picones-pcb) is the tidy version of this configuration; it needs design v2.6 or later, which is the first that can host a Pimoroni Pico Plus 2. The two builds take different microSD breakouts: on a breadboard the [Adafruit Micro-SD breakout board+](https://www.adafruit.com/product/254), on the PCB the smaller [Adafruit Micro SD SPI or SDIO breakout](https://www.adafruit.com/product/4682), which is the footprint the board is laid out for.
- **HW_CONFIG 8**: no additional hardware is required apart from a USB game controller. Audio is output through the monitor and the built-in speaker or headphone jack.
- **HW_CONFIG 14**: the Feather RP2350 is sold in two variants: [with 8 MB PSRAM onboard](https://www.adafruit.com/product/6130) and [without PSRAM](https://www.adafruit.com/product/6000). On the variant without PSRAM, a PSRAM chip must be soldered onto the board separately.

> [!IMPORTANT]
> Unlike the sister projects, the build does not refuse a configuration without PSRAM: the other board configurations known from those projects still compile and flash. They will not run. A board without PSRAM reports *"This board has no PSRAM. picoOutRun cannot run on it."* at startup, and the configurations that fall back to the bit-banged PicoDVI driver are too slow for the engine — see [Overclocking](#overclocking).

For wiring and assembly instructions, see the setup sections of the [pico-infonesPlus README](https://github.com/PicoPlus-devel/pico-infonesPlus#setup); for the PCB version of HW_CONFIG 2, see [PicoNES PCB](#picones-pcb) below. Flashing works the same for every board: hold BOOTSEL while connecting the board over USB, then copy the `.uf2` file onto the USB drive that appears.

***

## Overclocking

All four supported configurations run at **378 MHz at 1.50 V**. Cannonball runs single-core: core1 is owned by the display driver and the sound chain, so all of the engine and rendering headroom has to come out of the core clock. 378 MHz is what makes the port viable at all, and there is no user-selectable overclock — the settings menu entry that the sister projects offer is a ROM-browser feature, and this port has no ROM browser.

That clock is also why the board configurations driven by the bit-banged PicoDVI driver are not supported. There the system clock is tied to the pixel clock and cannot exceed 324 MHz, and without the HSTX driver's background task the sound chain moves back onto core0, so audio quality follows the frame rate. `./bld.sh -D` still forces that path on an HSTX-capable board, and the remaining board configurations in `pico_shared` still build, but neither is a supported way to run this port. Most of those boards have no PSRAM either.

Use this software at your own risk. I am not responsible in any way for damage to your board and/or connected peripherals caused by using this software or by incorrect wiring or voltages.

***

## Custom PCB

A community PCB design turns the HW_CONFIG 2 breadboard build into a finished little console: it carries a Pico-format board, the DVI and microSD breakouts and a controller port on a single board, with an optional 3D-printed case. Nothing changes in the firmware — it is simply a neater way to build hardware this port already supports, so you flash the same HW_CONFIG 2 binary and you are done.

| Design | Board it carries | Build | Gerber archive | Designed by |
| --- | --- | --- | --- | --- |
| [PicoNES](#picones-pcb) | Pimoroni Pico Plus 2 on male headers — design **v2.6** or later only | `-c2` | `pico_nesPCB_v2.6.zip` | John Edgar Park |

The archive lives in [`pico_shared/PCB`](https://github.com/PicoPlus-devel/pico_shared/tree/main/PCB). Upload the zip as-is to a PCB manufacturer of your choice; [PCBWay](https://www.pcbway.com/) and JLCPCB are both good options.

The other two designs from [pico-infonesPlus](https://github.com/PicoPlus-devel/pico-infonesPlus), the **PicoNES Mini** and **PicoNES Micro**, are **not applicable** to this project: they are built around Waveshare boards without PSRAM. Older PicoNES designs (v2.1 and earlier) are not applicable either, for the reason given below.

> [!NOTE]
> Sellers on AliExpress have copied the PicoNES design and sell ready-made boards. For questions about those, contact the seller.

### PicoNES PCB

The design, by [@johnedgarpark](https://twitter.com/johnedgarpark), comes from [pico-infonesPlus](https://github.com/PicoPlus-devel/pico-infonesPlus) and kept its NES-flavoured name, but there is nothing NES-specific about it — it is DVI, microSD and controller wiring, and this port runs on it just as well. The current design is **v2.6**.

> [!IMPORTANT]
> For this project the PCB only works with a [Pimoroni Pico Plus 2](https://shop.pimoroni.com/products/pimoroni-pico-plus-2?variant=42092668289107), and that board needs design **v2.6 or later** plus male headers. A Raspberry Pi Pico 2 or Pico 2 W has no PSRAM and cannot run pico-outrun at all, so an older PicoNES board built around one of those cannot be reused here — see [Mounting the board](#mounting-the-board).

<img width="480" alt="Populated PCB with a Pico plugged into the through-holes" src="https://github.com/user-attachments/assets/2bbc846d-56b1-4528-9899-01bc9b32ce11" />

#### Mounting the board

Design v2.6 added through-holes, and that is what makes a Pimoroni Pico Plus 2 — and with it the PSRAM this port needs — an option at all:

| PCB design | Takes a Pimoroni Pico Plus 2? |
| --- | --- |
| v2.6 or later (through-holes) | Yes — with male headers soldered on, plugged into the through-holes |
| v2.1 and older | No — the board has to lie flat against the PCB, which the SP/CE connector on its back prevents |

> [!NOTE]
> Soldering skills are required. Solder every connection from the board to the PCB, including the ones on the short right-hand side — those are ground.

#### What you need

- A [Pimoroni Pico Plus 2](https://shop.pimoroni.com/products/pimoroni-pico-plus-2?variant=42092668289107) with **male headers** soldered on ([these](https://a.co/d/dSNPuyo) fit), plugged into the through-holes of a v2.6 or later board.
- [Adafruit DVI Breakout Board — For HDMI Source Devices](https://www.adafruit.com/product/4984)
- [Adafruit Micro SD SPI or SDIO Card Breakout Board — 3V ONLY!](https://www.adafruit.com/product/4682) — note this is **not** the Micro-SD breakout board+ used in the breadboard build; the PCB is laid out for this smaller one. The card is only needed for the SD-card route to the [game data](#game-data); with the data image flashed, the game runs without a card.
- [Micro USB to OTG Y-cable](https://a.co/d/b9t11rl) — a USB controller is required to play, and the Y-cable powers the board and connects the controller at the same time.
- Micro USB power supply.
- Optional: an on/off switch, such as [this one](https://www.kiwi-electronics.com/en/spdt-slide-switch-410?search=KW-2467).
- Optional: [one or two NES controller ports](https://www.zedlabz.com/products/controller-connector-port-for-nintendo-nes-console-7-pin-90-degree-replacement-2-pack-black-zedlabz) — usable for steering and Start/Coin only at the moment, see [Controllers](#controllers).

Audio on this configuration is carried over HDMI.

<img width="480" alt="Assembled PicoNES PCB with controllers connected" src="https://github.com/user-attachments/assets/d40ed98f-4632-4161-986a-732d35290fac" />

#### Which binary to flash

`picoOutRun_AdafruitDVISD_pico2_arm.uf2` — the same file as the breadboard build, since the PCB is the same hardware configuration.

#### 3D printed case

Gavin Knight ([DynaMight1124](https://github.com/DynaMight1124)) designed an NES-like enclosure for this PCB: [thingiverse.com/thing:6689537](https://www.thingiverse.com/thing:6689537). The v2.0 design has a base, a power-switch part and a choice of two top covers — one with a button that reaches the BOOTSEL button so firmware can be updated without opening the case, one without. Print the files that match the PCB version you own; Gavin's Thingiverse page has the details.

> [!IMPORTANT]
> Download the **latest** top cover. The Pimoroni Pico Plus 2 is always mounted on headers here, and headers raise the board — only the newest cover leaves room for the USB cable, the older ones assume a Pico soldered flat onto the PCB.

<img width="480" alt="Top cover with a button for BOOTSEL" src="https://github.com/user-attachments/assets/3c8f8990-51b9-4873-9054-64bb2cd6c300" />

For the full photo gallery and assembly detail, see the [PCB section of the pico-infonesPlus documentation](https://github.com/PicoPlus-devel/pico-infonesPlus#pcb-with-raspberry-pi-pico-or-pico-2-and-pimoroni-pico-plus-2).

***

## SD card setup

An SD card is optional. With the game data flashed, the game runs without one, and a missing card is not an error.

A card is used for two things:

1. **The ROM set**, if you would rather not build the data image on a PC. Format the card as FAT32 (recommended) or exFAT and copy the extracted OutRun ROM files into `/roms/ORUN`. The folder is created automatically on first boot. See [Game data](#game-data).
2. **Settings.** Screen mode, scanlines, audio and the other options from the settings menu are stored in `/settings_ORUN.dat` in the root of the card and are remembered across restarts. Without a card the settings revert to their defaults on every boot.

There are no save files: the game has no save states, and high scores are not written to the card.

Files can be added or removed later without taking the card out of the board; see [USB drive mode](#usb-drive-mode).

***

## USB drive mode

The SD card can be handed to a computer as an ordinary USB mass storage device, so the ROM set can be copied onto it without moving the card to a card reader.

Press **Select + Start** to open the settings menu, select **USB drive mode**, and connect the board's USB port to a computer. The card is unmounted and appears on the computer as a removable drive. Copy or delete files, then eject the drive on the computer: the card is remounted and the game resumes.

**B** also leaves the screen. If no computer has claimed the drive within twenty seconds, it closes by itself.

Points to note:

- The entry is also available on the screen that reports missing game data, which is how a ROM set gets onto the card on a board that has never run the game.
- Eject the drive on the computer before leaving the screen, as with any removable drive.
- Gamepads on the GPIO controller ports, and USB controllers on boards that have a second USB port for them, keep working while the card is mounted.
- A board with only one USB port needs that port for the computer, and is powered through it, so a USB controller cannot be attached at the same time and the cables cannot be exchanged while the game is running. Connect the board to the computer first and open the screen with a gamepad on a GPIO controller port.
- The card must be readable when the screen is opened. A missing or unreadable card is reported instead.

***

## Controllers

> [!IMPORTANT]
> A USB controller is required to play. The other controller types are wired up only partly, and the car cannot be driven with them yet.

| Controller | What works |
| --- | --- |
| USB gamepad (XInput, DualShock 4 / DualSense, generic HID, 8BitDo) | Everything — steering, accelerate, brake, gear, Start and Coin, and the settings menu. Buttons are read by position, so the right-hand face button accelerates, the bottom one brakes and the top one changes gear: **B / A / Y** on an Xbox pad, **Circle / Cross / Triangle** on a PlayStation pad. If the pad has an analog stick, the left stick steers proportionally; the d-pad steers as well. |
| USB keyboard | Playable: **X** accelerates, **Z** brakes, **C** changes gear, **S** is Start, **A** is Coin, and the arrow keys steer. |
| NES or SNES controller on the GPIO port | Steering (Left/Right), **Start** and **Select** (insert coin), and the Select + Start combination that opens the settings menu. **Accelerate, brake and gear are not mapped**, so the car cannot be driven with it. |
| Wii Classic / SNES-Classic-mini pad (I2C port) | Not mapped in game. |

See the [pico-infonesPlus README](https://github.com/PicoPlus-devel/pico-infonesPlus#gamecontroller-support) for general controller notes and troubleshooting.

***

## In-game controls

| Action | Button | Xbox pad | PlayStation pad | Keyboard |
| --- | --- | --- | --- | --- |
| Steer | D-pad Left/Right, or the left analog stick | Stick / d-pad | Stick / d-pad | Arrow keys |
| Accelerate | A (right-hand face button) | B | Circle | X |
| Brake | B (bottom face button) | A | Cross | Z |
| Gear (high/low) | X (top face button) | Y | Triangle | C |
| Start | Start | Menu | Options | S |
| Insert coin | Select | View | Share | A |
| Settings menu | Select + Start | View + Menu | Share + Options | A + S |

The machine is set to free play, so Start alone begins a game; the coin button is there for completeness.

**Select + Start** opens the settings menu while the game keeps its state; leaving the menu returns to where you were. From there you can reset the game or change settings: screen mode (8:7 or 1:1, with or without scanlines), the frame rate display, audio on/off, display mode, external audio, board-specific options such as the speaker volume and the NeoPixel VU meter on the Fruit Jam, the controller test screen, [USB drive mode](#usb-drive-mode), and BOOTSEL mode. Settings are remembered across restarts when an SD card is present.

Entries that the sister projects offer are absent here because they have nothing to act on: there is no *Quit game* (there is no ROM browser to return to), no save states, no frame skip setting and no *Return to emulator selection*.

***

## Building from source

Build on Linux (a Raspberry Pi also works) or on Windows under WSL, with the [Pico SDK](https://github.com/raspberrypi/pico-sdk) version 2.x or later installed and `PICO_SDK_PATH` set. Two additional requirements:

- The TinyUSB submodule of the Pico SDK must be on the latest master branch (`cd $PICO_SDK_PATH/lib/tinyusb && git checkout master && git pull`).
- Configurations 8 and 14 use PIO USB for a second USB port and need [Pico-PIO-USB](https://github.com/sekigon-gonnoc/Pico-PIO-USB), with `PICO_PIO_USB_PATH` pointing to the cloned repository.

`picotool` must be on the `PATH`; it is used to convert the game data image and by `buildAll.sh`.

Then:

```bash
git clone https://github.com/PicoPlus-devel/pico-outrun.git
cd pico-outrun
git submodule update --init --recursive
./bld.sh -c2 -2     # HW_CONFIG 2:  Pimoroni Pico Plus 2 breadboard or PicoNES PCB
./bld.sh -c8 -2     # HW_CONFIG 8:  Adafruit Fruit Jam
./bld.sh -c13 -2    # HW_CONFIG 13: Murmulator M2
./bld.sh -c14 -2    # HW_CONFIG 14: Adafruit Feather RP2350
./buildAll.sh       # all four; what CI runs
```

Every configuration must be built with `-2`: this is an RP2350 project, and the build refuses RP2040. Run `./bld.sh -h` for all options. The resulting `.uf2` file is placed in the `releases/` folder; flash it by holding BOOTSEL while connecting the board and copying the file onto the USB drive that appears.

`bld.sh` deletes and recreates `build/` on every run. To iterate, configure once with `./bld.sh -m -c 8 -2` and then build with `make -j$(nproc) -C build`.

### Generating the game data image

```sh
./bld.sh -m -c 8 -2                    # once, to establish the flash address
tools/mkoutrundata.sh ~/roms/outrun    # writes outrun-data.uf2
```

The packer is built automatically on first use. It compiles [`port/outrun_pack.c`](port/outrun_pack.c) — the same ROM load table, CRC check and tile/sprite/road decoders the firmware uses — so the image produced on a PC and the one the board builds from an SD-card ROM set are byte for byte identical. [tools/README.md](tools/README.md) documents the tool, its options and the ROM set it expects.

### Host-side test harness

[`hosttest/build.sh`](hosttest/build.sh) builds the ROM tooling natively on Linux, without the Pico SDK: `packer` builds the data packer, `verify` links Cannonball's own unmodified decoders and byte-compares their output against the packed image, `lutgen` regenerates the YM2151 tables, and `check` runs all of it against a ROM set. Decoder and data-format changes can be verified on a desktop machine without flashing a board.

## Acknowledgements

- [Cannonball](https://github.com/djyt/cannonball), the OutRun engine this port is built on, by **Chris White** and the Cannonball team.
- The menu, HDMI driver, PSRAM allocator, SD card and controller code in [pico_shared](https://github.com/PicoPlus-devel/pico_shared) are shared with the sister projects listed at the top of this README.
- HSTX video driver and I2S audio: [@fliperama86](https://github.com/fliperama86) and [@frenskefrens](https://github.com/fhoedemakers).
- (S)NES and Wii controller support: [@PaintYourDragon](https://github.com/PaintYourDragon) and [Adafruit](https://www.adafruit.com).
- The [PicoNES PCB](#picones-pcb) was designed by **John Edgar Park** ([@johnedgarpark](https://twitter.com/johnedgarpark)); the 3D-printed case for it by [DynaMight1124](https://github.com/DynaMight1124).

## Use of AI

The port of the Cannonball engine to the RP2350, the ROM data packer and the performance and memory work were developed with the help of [Anthropic Claude](https://www.anthropic.com/claude) (Opus 5).

## License

pico-outrun is a port of Cannonball and is distributed under Cannonball's licence; see [LICENSE](LICENSE). Note in particular that this is **not** an open-source licence in the OSI sense: redistributions may not be sold or used commercially, and modified redistributions must include complete source code.

OutRun is a trademark of SEGA Corporation. This project is not affiliated with SEGA in any way, and no SEGA ROM data is distributed with it.
