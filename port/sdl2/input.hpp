/*
 * picoOutRun's replacement for cannonball/src/main/sdl2/input.hpp.
 *
 * Upstream's header says it outright: "If porting to a non-SDL platform, you
 * would need to replace this class." This is that replacement - the same public
 * surface the engine uses, with every SDL event handler removed.
 *
 * The engine only ever touches:
 *   is_pressed / is_pressed_clear / has_pressed, the `presses` and `limits`
 *   enums, motor_limits[], gamepad, analog, and the analog values
 *   a_wheel / a_accel / a_brake / a_motor.
 *
 * Filling those in is port/input.cpp's job: it merges USB HID, NES/SNES and Wii
 * pads the way the rest of the Frens family does. Analog steering comes from
 * io::GamePadState::axis[], which is why `analog` can be switched on at all.
 *
 * Found as "sdl2/input.hpp" because port/ is ahead of the vendored engine on the
 * include path.
 */

#pragma once

#include "stdint.hpp"

class Input
{
public:
    enum presses
    {
        LEFT = 0,
        RIGHT = 1,
        UP = 2,
        DOWN = 3,
        ACCEL = 4,
        BRAKE = 5,
        GEAR1 = 6,
        GEAR2 = 7,

        START = 8,
        COIN = 9,
        VIEWPOINT = 10,

        PAUSE = 11,
        STEP = 12,
        TIMER = 13,
        MENU = 14,
    };

    bool keys[15] = {};
    bool keys_old[15] = {};

    enum limits
    {
        SW_LEFT = 0,
        SW_CENTRE = 1,
        SW_RIGHT = 2,
    };
    // Deluxe cabinet motor end-stops. No motor here, so the engine is told the
    // wheel is always centred.
    bool motor_limits[3] = {false, true, false};

    bool gamepad = false;      // a pad has been seen
    int rumble_supported = 0;  // no rumble path on this hardware
    int analog = 0;            // set by port/input.cpp when an analog stick appears

    // Analog controls. CENTRE is the neutral value the engine expects.
    int wheel = CENTRE, a_wheel = CENTRE;
    int a_accel = 0;
    int a_brake = 0;
    int a_motor = 0;

    Input() = default;
    ~Input() = default;

    void init();
    void frame_done();
    bool is_pressed(presses p);
    bool is_pressed_clear(presses p);
    bool has_pressed(presses p);

    // Called once per frame by port/input.cpp with the merged pad state.
    void set_button(presses p, bool down);

    static const int CENTRE = 0x80;
};

extern Input input;
