# pico-outRun

pico-outrun is a port of the arcade game OutRun to RP2350 boards with PSRAM, based on the
Cannonball engine.

# Changelog

## v0.7

- **Pico Computer 3.** The game now runs on the [Pico Computer 3](https://github.com/UKTailwind/Pico-Computer-3): HDMI, sound through HDMI or the audio jack, USB controllers, 8 MB of PSRAM, and NES or SNES controllers and a Wii Classic controller on the I/O header. The pin assignment may still change. See [Supported hardware](https://github.com/PicoPlus-devel/pico-outrun#supported-hardware). Thanks to @javavi.
- **PCB for the Waveshare RP2350-PiZero.** The game now runs on a PCB for the Waveshare RP2350-PiZero, designed by @szuping: HDMI, sound through HDMI or the audio jack, a USB controller on the PIO-USB port, NES and SNES controller ports, and a socket for an ESP32 module that connects Bluetooth controllers through [BlueRetro](https://github.com/darthcloud/blueretro). An 8 MB PSRAM chip must be soldered onto the RP2350-PiZero. The PCB design is not yet available. See [PCB with Waveshare RP2350-PiZero](https://github.com/PicoPlus-devel/pico-infonesPlus#pcb-with-waveshare-rp2350-pizero) in the pico-infonesPlus readme.
- **No more picture drop-outs or flickering lines in DVI mode.** On boards with HSTX video, with *Display Mode* set to DVI, the picture could randomly drop out, and now and then a single line was shown shifted to the right for one frame.
- **NES and SNES controllers on the controller ports are now read with the same timing as a real console**, on all boards. Thanks to @javavi.
- Under pico-bootLoader, the settings menu entry that goes back to the bootloader is now called **Return to boot loader**.

## v0.6

- **No more dots or lines in the picture.** On boards with HSTX video, some TVs and monitors showed small dots or short dotted lines. The picture is now clean, and the built-in USB port can still be used for controllers and USB drive mode. Thanks to @szuping for the hint that led to this fix.
- **The Video Clock Fix setting has been removed.** It is no longer needed. On a board where it was turned on, a USB controller on the built-in USB port works again.
- **The board now runs at 384 MHz instead of 378 MHz.** See [Overclocking](https://github.com/PicoPlus-devel/pico-outrun#overclocking) in the readme.
- **The settings menu no longer opens by itself with a controller on the Wii port.** With a NES or SNES Classic Mini controller or a Wii Classic controller, the settings menu opened a few moments after pressing START, and again every time it was closed. USB controllers and controllers on the NES/SNES ports were not affected.
- **USB controllers on the Adafruit Fruit Jam and Feather RP2350 are now found at every start.** On boards with an older RP2350 chip they were often not detected.
- **USB controllers connected through a USB hub are detected reliably** on the Pimoroni Pico Plus 2, Murmulator M2 and Olimex RP2040-PICO-PC. They were often not found, and the game could stop.
- **USB keyboard in the menus.** Enter selects and Esc goes back.

## v0.5.2

- ~~**New setting: Video Clock Fix** (Pimoroni Pico Plus 2 or PicoNES PCB, Murmulator M2 and Olimex RP2040-PICO-PC). Turn it on if your TV or monitor shows small dots or lines in the picture. A USB controller can then no longer be used; use a NES, SNES or Wii controller instead. On the Adafruit Fruit Jam and Feather RP2350 it is always on.~~ Removed in v0.6.
- **PicoNES PCB Gerber files.** The release now includes the Gerber files of the PicoNES PCB: `pico_nesPCB_v2.6.zip` with NES controller ports, and the new `pico_nesPCB_v2.8_SNES_PORTS.zip` with SNES controller ports, designed by John Edgar Park ([@johnedgarpark](https://twitter.com/johnedgarpark)). Both need a Pimoroni Pico Plus 2 mounted on male headers. See [Custom PCB](https://github.com/PicoPlus-devel/pico-outrun#custom-pcb) in the readme, and the [PCB README](https://github.com/PicoPlus-devel/pico_shared/blob/main/PCB/README.md) for parts lists and build instructions.

## v0.5.1

- The release now includes updateAll.exe, which installs the ROM set on the SD card from a Windows computer. The readme explains how to use it.

## v0.5

- **Settings are shared with the other arcade games** of this family and stored in `/settings_ARC.dat`. Settings saved by an earlier version are not carried over, so they return to their defaults once.

## v0.4

- **All settings return to their defaults once** after updating to this version.
- **Olimex RP2040-PICO-PC.** The game now runs on the [Olimex RP2040-PICO-PC](https://www.olimex.com/Products/MicroPython/PICO/RP2040-PICO-PC/) with a Raspberry Pi Pico 2 compatible board that has 8 MB of PSRAM: HDMI, sound through HDMI and the audio jack, a USB controller on the USB-A port and a NES or SNES controller on the UEXT connector. A standard Raspberry Pi Pico 2 has no PSRAM and does not work. See [Supported hardware](https://github.com/PicoPlus-devel/pico-outrun#supported-hardware). Contributed by [DnCraptor](https://github.com/DnCraptor).
- **The Controller Test screen is now closed by holding SELECT + UP** for 2 seconds. SELECT + START conflicted with some 8BitDo wireless controllers.
- **The B button of the AliExpress SNES USB controller works without pressing Y first.**

## v0.3

- **Overscan fix in menu.** A new setting for TVs that cut off the edges of the screen. It leaves the top and bottom rows of the settings menu blank, and optionally the first and last columns as well. The change is shown right away.
- **More options on one page in the settings menu.** The settings menu now shows more options at once, so less scrolling is needed.
- **Quicker saving in the settings menu.** Press SELECT on any setting to jump straight to the SAVE/CANCEL/DEFAULT row.
- **Controller test screen** shows the controller outline and the list of controllers correctly again.

## v0.2

Two fixes for controllers on the GPIO ports.

- **Clone and aftermarket NES controllers now work during the game.** Their A and B buttons
  only did something in the settings menu, so there was no way to accelerate or brake while
  driving. Original NES controllers, SNES controllers, and everything on USB or the Wii port
  were not affected.
- **A controller in the second port now plays the game.** Until now it could only be used in
  the settings menu. Both ports now steer the car, so it no longer matters which one is used,
  and the two ports may hold different types of controller.

## v0.1

Initial release.

pico-outrun is a port of the arcade game OutRun to RP2350 boards with PSRAM, based on the
Cannonball engine. The game plays from attract mode through a full race, with music and sound
effects. It is still work in progress: the game speed is not constant yet and varies with what
is on screen.

## Supported boards

| Board | File |
| --- | --- |
| Adafruit Fruit Jam | `picoOutRun_AdafruitFruitJam_arm_piousb.uf2` |
| Pimoroni Pico Plus 2 with Adafruit DVI and microSD breakouts, or on the PicoNES PCB (v2.6 or v2.8) | `picoOutRun_AdafruitDVISD_pico2_arm.uf2` |
| Murmulator M2 | `picoOutRun_MurmulatorM2_arm.uf2` |
| Adafruit Feather RP2350 with HSTX port and TLV320DAC3100 | `picoOutRun_AdafruitFeatherRP2350_TLV320DAC3100_arm_piousb.uf2` |
| Olimex RP2040-PICO-PC with a Pico 2 compatible board that has 8 MB of PSRAM | `picoOutRun_OlimexPicoPC_arm.uf2` |
| Pico Computer 3 | `picoOutRun_PicoComputer3_arm.uf2` |
| PCB for the Waveshare RP2350-PiZero, with an 8 MB PSRAM chip soldered onto the RP2350-PiZero | `picoOutRun_WaveShareRP2350PiZeroLiuPCB_arm_piousb.uf2` |

A board with 8 MB of PSRAM is required. A plain Raspberry Pi Pico 2 does not work.

## Game data

The OutRun ROMs are copyright SEGA and are not included. You need the MAME `outrun`
(revision B) ROM set, unzipped. There are two ways to use it:

- Copy the ROM files to `/roms/ORUN` on the SD card. The board then prepares the game data
  itself on every start, which takes a few seconds.
- Or convert them on a PC into `outrun-data.uf2` and flash that once, next to the
  application. The game then starts immediately and no SD card is needed.

See the README for details.

## Controls

USB gamepads and keyboards, NES and SNES controllers, and Wii Classic controllers are
supported. SELECT + START opens the settings menu, which also offers USB drive mode for
copying the ROM files onto the SD card without taking it out of the board.

## Known limitations

- The game speed still varies.
- Steering with an analog stick is not proportional; the stick works like the d-pad.
- High scores are not saved, and there are no save states.
