/*
 * Minimal stand-in for cannonball/src/main/video.hpp, used ONLY by
 * hosttest/verify_decode. The real Video owns the render pipeline and a 143 KB
 * pixel buffer; hwsprites.cpp only touches video.pixels, and the decoders under
 * test do not touch it at all.
 *
 * There is deliberately no config.hpp stub: the test uses the real
 * port/frontend/config.hpp, so the firmware and the tests cannot disagree about
 * s16_width, s16_x_off or any other setting that reaches a decoder.
 */
#pragma once
#include "stdint.hpp"

class Video
{
public:
    uint16_t *pixels = nullptr;
};

extern Video video;
