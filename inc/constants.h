#pragma once

#include "enum-utils.h"
#include "types.h"

namespace Constants
{
    constexpr ScreenDimensions screen{ Width{ 1366 }, Height{ 768 } };

    constexpr FPS target_fps{ 240 };

    constexpr float max_camera_zoom = 3.f;

    // Note: This is a compensation factor used to properly scale images and objects drawn to the screen.  Since we draw
    // images according to the dimensions provided by callers to draw() but the OpenGL coordinate system is in entire
    // screen dimensions (-1, 1) which is really (resolution * 2) to get to '1', we need to rescale by that same factor
    // in the vertex shader to get an expected image/shape size on screen when going through the transform shader.  Note
    // that the no-transform shader does not use this.
    constexpr float shader_scale_factor = 2.f;

    constexpr float aa_fringe_scale = 1.f;

    constexpr uint32_t sign32     = 0x80000000;
    constexpr uint32_t exponent32 = 0x7F800000;
    constexpr uint32_t mantissa32 = 0x007FFFFF;

    constexpr float   big_golden32 = 1.61803398875f;
    constexpr float small_golden32 = 0.61803398875f;

    constexpr float pi32 = 3.1415926535897f;

    constexpr double machine_epsilon64 = 4.94065645841247e-324;

    constexpr uint64_t max_U64 = 0xffffffffffffffffull;
    constexpr uint32_t max_U32 = 0xffffffff;
    constexpr uint64_t max_U16 = 0xffff;
    constexpr uint8_t  max_U8  = 0xff;

    constexpr int64_t max_S64 = static_cast<int64_t>(0x7fffffffffffffffull);
    constexpr int32_t max_S32 = static_cast<int32_t>(0x7fffffff);
    constexpr int16_t max_S16 = static_cast<int16_t>(0x7fff);
    constexpr int8_t  max_S8  = static_cast<int8_t>(0x7f);

    constexpr int64_t min_S64 = static_cast<int64_t>(0xffffffffffffffffull);
    constexpr int32_t min_S32 = static_cast<int32_t>(0xffffffff);
    constexpr int16_t min_S16 = static_cast<int16_t>(0xffff);
    constexpr int8_t  min_S8  = static_cast<int8_t>(0xff);

    constexpr uint32_t bit1  = (1U<<0);
    constexpr uint32_t bit2  = (1U<<1);
    constexpr uint32_t bit3  = (1U<<2);
    constexpr uint32_t bit4  = (1U<<3);
    constexpr uint32_t bit5  = (1U<<4);
    constexpr uint32_t bit6  = (1U<<5);
    constexpr uint32_t bit7  = (1U<<6);
    constexpr uint32_t bit8  = (1U<<7);
    constexpr uint32_t bit9  = (1U<<8);
    constexpr uint32_t bit10 = (1U<<9);
    constexpr uint32_t bit11 = (1U<<10);
    constexpr uint32_t bit12 = (1U<<11);
    constexpr uint32_t bit13 = (1U<<12);
    constexpr uint32_t bit14 = (1U<<13);
    constexpr uint32_t bit15 = (1U<<14);
    constexpr uint32_t bit16 = (1U<<15);
    constexpr uint32_t bit17 = (1U<<16);
    constexpr uint32_t bit18 = (1U<<17);
    constexpr uint32_t bit19 = (1U<<18);
    constexpr uint32_t bit20 = (1U<<19);
    constexpr uint32_t bit21 = (1U<<20);
    constexpr uint32_t bit22 = (1U<<21);
    constexpr uint32_t bit23 = (1U<<22);
    constexpr uint32_t bit24 = (1U<<23);
    constexpr uint32_t bit25 = (1U<<24);
    constexpr uint32_t bit26 = (1U<<25);
    constexpr uint32_t bit27 = (1U<<26);
    constexpr uint32_t bit28 = (1U<<27);
    constexpr uint32_t bit29 = (1U<<28);
    constexpr uint32_t bit30 = (1U<<29);
    constexpr uint32_t bit31 = (1U<<30);
    constexpr uint32_t bit32 = (1U<<31);

    constexpr uint32_t bitmask1  = 0x00000001;
    constexpr uint32_t bitmask2  = 0x00000003;
    constexpr uint32_t bitmask3  = 0x00000007;
    constexpr uint32_t bitmask4  = 0x0000000f;
    constexpr uint32_t bitmask5  = 0x0000001f;
    constexpr uint32_t bitmask6  = 0x0000003f;
    constexpr uint32_t bitmask7  = 0x0000007f;
    constexpr uint32_t bitmask8  = 0x000000ff;
    constexpr uint32_t bitmask9  = 0x000001ff;
    constexpr uint32_t bitmask10 = 0x000003ff;
    constexpr uint32_t bitmask11 = 0x000007ff;
    constexpr uint32_t bitmask12 = 0x00000fff;
    constexpr uint32_t bitmask13 = 0x00001fff;
    constexpr uint32_t bitmask14 = 0x00003fff;
    constexpr uint32_t bitmask15 = 0x00007fff;
    constexpr uint32_t bitmask16 = 0x0000ffff;
    constexpr uint32_t bitmask17 = 0x0001ffff;
    constexpr uint32_t bitmask18 = 0x0003ffff;
    constexpr uint32_t bitmask19 = 0x0007ffff;
    constexpr uint32_t bitmask20 = 0x000fffff;
    constexpr uint32_t bitmask21 = 0x001fffff;
    constexpr uint32_t bitmask22 = 0x003fffff;
    constexpr uint32_t bitmask23 = 0x007fffff;
    constexpr uint32_t bitmask24 = 0x00ffffff;
    constexpr uint32_t bitmask25 = 0x01ffffff;
    constexpr uint32_t bitmask26 = 0x03ffffff;
    constexpr uint32_t bitmask27 = 0x07ffffff;
    constexpr uint32_t bitmask28 = 0x0fffffff;
    constexpr uint32_t bitmask29 = 0x1fffffff;
    constexpr uint32_t bitmask30 = 0x3fffffff;
    constexpr uint32_t bitmask31 = 0x7fffffff;
    constexpr uint32_t bitmask32 = 0xffffffff;
} // namespace Constants