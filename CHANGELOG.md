# v0.1

First build of the OutRun port.

This release is a skeleton and does not play the game yet. It starts up on all supported
boards, brings up the display, sound hardware and controllers, and shows a test pattern.
It is published so the board support can be checked on real hardware.

- Runs on Pico 2 and compatible RP2350 boards, over HDMI or DVI.
- Supports the same controllers as the emulators: USB, NES/SNES and Wii.
- The settings menu opens with SELECT + START.

The game data is not included. OutRun's ROMs are copyright SEGA, so you have to supply
them yourself and convert them into a data file that is flashed alongside the application.
See the README for how.

You no longer have to do that conversion on a PC if you would rather not. Copy the OutRun
ROM files, unzipped, into `/roms/ORUN` on the SD card and the board builds the game data
itself when it starts. This takes a few seconds on every boot, so flashing the data file is
still the better option if you have it; the board uses it automatically when it is present.

If the game data is missing altogether, the screen now explains what to do instead of
showing a test pattern.
