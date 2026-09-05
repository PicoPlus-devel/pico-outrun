/*
 * Input for picoOutRun.
 *
 * Upstream's Input is an SDL event sink. Here it is a plain latch: main.cpp
 * merges the pad sources pico_shared exposes - USB HID, NES/SNES pads over GPIO,
 * and the Wii Classic over I2C - and calls set_button() / set_analog() once per
 * frame. The engine then reads it through the same is_pressed / has_pressed /
 * a_wheel interface it always did.
 */

#include "sdl2/input.hpp"

#include <cstring>

Input input;

void Input::init()
{
    memset(keys, 0, sizeof(keys));
    memset(keys_old, 0, sizeof(keys_old));
    wheel = a_wheel = CENTRE;
    a_accel = 0;
    a_brake = 0;
    a_motor = 0;

    // No cabinet motor: tell the engine the wheel is permanently centred, which
    // is what ooutputs checks before trying to drive one.
    motor_limits[SW_LEFT] = false;
    motor_limits[SW_CENTRE] = true;
    motor_limits[SW_RIGHT] = false;
}

void Input::set_button(presses p, bool down)
{
    keys[p] = down;
}

// Upstream calls this at the end of every ticked frame; has_pressed() compares
// against it to turn a held button into a single edge.
void Input::frame_done()
{
    memcpy(keys_old, keys, sizeof(keys));
}

bool Input::is_pressed(presses p)
{
    return keys[p];
}

// Read-and-clear, for one-shot actions.
bool Input::is_pressed_clear(presses p)
{
    if (!keys[p])
    {
        return false;
    }
    keys[p] = false;
    return true;
}

// True only on the frame the button went down.
bool Input::has_pressed(presses p)
{
    return keys[p] && !keys_old[p];
}
