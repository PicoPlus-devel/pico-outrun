/*
 * picoOutRun's replacement for cannonball/src/main/directx/ffeedback.hpp.
 *
 * Upstream uses DirectInput force feedback to drive a deluxe cabinet's steering
 * motor. There is no motor and no DirectX here, so every entry point is a
 * no-op and set() reports failure, which is what ooutputs.cpp checks.
 */

#pragma once

namespace forcefeedback
{
    inline bool is_supported() { return false; }
    inline int init(int /*max_force*/, int /*min_force*/, int /*force_duration*/) { return 1; }
    inline void close() {}
    inline int set(int /*xdirection*/, int /*force*/) { return 1; }
}
