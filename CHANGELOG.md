# pico-outRun

pico-outrun is a port of the arcade game OutRun to RP2350 boards with PSRAM, based on the
Cannonball engine

# Changelog

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
| Pimoroni Pico Plus 2 with Adafruit DVI and microSD breakouts, or on the PicoNES PCB (v2.6 or later) | `picoOutRun_AdafruitDVISD_pico2_arm.uf2` |
| Murmulator M2 | `picoOutRun_MurmulatorM2_arm.uf2` |
| Adafruit Feather RP2350 with HSTX port and TLV320DAC3100 | `picoOutRun_AdafruitFeatherRP2350_TLV320DAC3100_arm_piousb.uf2` |

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
