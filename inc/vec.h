#pragma once

#include <cmath>
#include <cstdint>

#include <compare>

#include "macros.h"

SUPPRESS_NONSTANDARD_EXTENSION_WARNING();

template <typename T>
struct Vec2T
{
    union
    {
        struct
        {
            T x;
            T y;
        };
        T xy[2];
    };

    constexpr Vec2T(): x{}, y{} { }
    constexpr Vec2T(T x, T y):
        x{ x }, y{ y } { }
    constexpr Vec2T(T xy): Vec2T{ xy, xy } { }

    constexpr T mag2() const
    {
        return x * x + y * y;
    }

    friend constexpr Vec2T operator+(const Vec2T& a, const Vec2T& b)
    {
        return { a.x + b.x,
                 a.y + b.y };
    }

    friend constexpr Vec2T operator-(const Vec2T& a, const Vec2T& b)
    {
        return { a.x - b.x,
                 a.y - b.y };
    }

    friend constexpr Vec2T operator*(const Vec2T& a, const Vec2T& b)
    {
        return { a.x * b.x,
                 a.y * b.y };
    }

    friend constexpr Vec2T operator/(const Vec2T& a, const Vec2T& b)
    {
        return { a.x / b.x,
                 a.y / b.y };
    }

    constexpr bool operator==(const Vec2T& other) const
    {
        return x == other.x
            and y == other.y;
    }
};

template <typename T>
struct Vec4T
{
    union
    {
        struct
        {
            T x;
            T y;
            T z;
            T a;
        };
        struct
        {
            T p0[2];
            T p1[2];
        };
        T xyza[4];
    };

    constexpr Vec4T(): x{}, y{}, z{}, a{} { }
    constexpr Vec4T(T x, T y, T z, T a):
        x{ x }, y{ y }, z{ z }, a{ a } { }
    constexpr Vec4T(T xyza): Vec4T{ xyza, xyza, xyza, xyza } { }

    friend constexpr Vec4T operator+(const Vec4T& a, const Vec4T& b)
    {
        return { a.x + b.x,
                 a.y + b.y,
                 a.z + b.z,
                 a.a + b.a };
    }

    friend constexpr Vec4T operator-(const Vec4T& a, const Vec4T& b)
    {
        return { a.x - b.x,
                 a.y - b.y,
                 a.z - b.z,
                 a.a - b.a };
    }

    friend constexpr Vec4T operator*(const Vec4T& a, const Vec4T& b)
    {
        return { a.x * b.x,
                 a.y * b.y,
                 a.z * b.z,
                 a.a * b.a };
    }

    constexpr bool operator==(const Vec4T& other) const
    {
        return x == other.x
                and y == other.y
                and z == other.z
                and a == other.a;
    }
};

ENABLE_NONSTANDARD_EXTENSION_WARNING();

using Vec2f = Vec2T<float>;
using Vec4f = Vec4T<float>;

using Vec2d = Vec2T<double>;
using Vec4d = Vec4T<double>;

using Vec2i = Vec2T<int>;
using Vec4i = Vec4T<int>;

constexpr Vec4f hex_to_vec4f(uint32_t color)
{
    uint32_t r = (color >> 24) & 0xFF;
    uint32_t g = (color >> 16) & 0xFF;
    uint32_t b = (color >> 8)  & 0xFF;
    uint32_t a = (color >> 0)  & 0xFF;
    return { r/255.0f,
             g/255.0f,
             b/255.0f,
             a/255.0f };
}

constexpr uint32_t vec4f_to_hex(const Vec4f& color)
{
    uint32_t r = static_cast<uint32_t>(color.x * 255.f);
    uint32_t g = static_cast<uint32_t>(color.y * 255.f);
    uint32_t b = static_cast<uint32_t>(color.z * 255.f);
    uint32_t a = static_cast<uint32_t>(color.a * 255.f);

    uint32_t result = (r << 24)
                    | (g << 16)
                    | (b << 8)
                    | (a << 0);
    return result;
}

template <typename T>
Vec2T<T> abs(const Vec2T<T>& v)
{
    return { std::abs(v.x), std::abs(v.y) };
}

constexpr Vec4f invert_color(const Vec4f& color)
{
    auto inv = 1.f - color;
    inv.a = color.a;
    return inv;
}

constexpr uint32_t color_rgb(const Vec4f& color)
{
    auto hex = vec4f_to_hex(color);
    // Chop the alpha.
    hex >>= 8;
    return hex;
}

template <typename T>
constexpr Vec2T<T> apply_anim(Vec2T<T> value, float rate)
{
    value = value - value * static_cast<T>(rate);

    if (std::abs(value.x) < T(0.005))
    {
        value.x = 0;
    }

    if (std::abs(value.y) < T(0.005))
    {
        value.y = 0;
    }
    return value;
}

constexpr Vec2i apply_anim(Vec2i value, float rate)
{
    // Convert to a float.
    Vec2f converted{ static_cast<float>(value.x), static_cast<float>(value.y) };
    converted = apply_anim(converted, rate);
    value.x = static_cast<int>(converted.x);
    value.y = static_cast<int>(converted.y);
    return value;
}

template <typename T, typename U>
constexpr Vec2T<T> ease_expon(Vec2T<T> value, U delta_time)
{
    const T ease_weight = T(1.) - std::pow(T(2.), (T(-40.) * delta_time));
    value = value - value * ease_weight;

    if (std::abs(value.x) < T(0.005))
    {
        value.x = 0;
    }

    if (std::abs(value.y) < T(0.005))
    {
        value.y = 0;
    }
    return value;
}

template <typename U>
constexpr Vec2i ease_expon(Vec2i value, U delta_time)
{
    // Convert to a float.
    Vec2f converted{ static_cast<float>(value.x), static_cast<float>(value.y) };
    converted = ease_expon(converted, delta_time);
    value.x = static_cast<int>(converted.x);
    value.y = static_cast<int>(converted.y);
    return value;
}

template <typename T, typename U>
constexpr Vec2T<T> ease_expon_val(Vec2T<T> value, U delta_time, T speed)
{
    const T ease_weight = T(1.) - std::pow(T(2.), (-speed * delta_time));
    value = value - value * ease_weight;

    if (std::abs(value.x) < T(0.005))
    {
        value.x = 0;
    }

    if (std::abs(value.y) < T(0.005))
    {
        value.y = 0;
    }
    return value;
}

namespace CmdBuffer
{
    // Since these structures depend on definitions of the vector structure and is
    // commonly used across the UI, we will place them here.
    struct ColorPalette
    {
        Vec4f fill;
        Vec4f background;
        Vec4f border;
        Vec4f highlight;
        Vec4f outline_selection;
        Vec4f text;
    };
} // namespace CmdBuffer

namespace Render
{
    // Similar to the above, this is commonly used across the system.
    template <typename T>
    struct CameraT
    {
        Vec2T<T> pos;
        Vec2T<T> scale = 3.;
        Vec2T<T> scale_velocity;
        Vec2T<T> velocity;

        bool operator==(const CameraT&) const = default;
    };

    using Camera = CameraT<float>;

    using WorldCamera = CameraT<double>;
} // namespace Render
