#pragma once

#include "types.h"

constexpr uint64_t up_pow2(uint64_t x)
{
    if (x == 0)
        return 1;
    x -= 1;
    x |= x >> 1;
    x |= x >> 2;
    x |= x >> 4;
    x |= x >> 8;
    x |= x >> 16;
    x |= x >> 32;
    return x + 1;
}

constexpr bool pow_2(uint64_t x)
{
    return (x != 0) and ((x & (x - 1)) == 0);
}