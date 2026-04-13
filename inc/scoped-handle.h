#pragma once

#include <concepts>
#include <utility>

// Explicitly specialize this template for custom values.
template <std::equality_comparable Handle>
constexpr Handle null_handle_value = { };

template <std::equality_comparable Handle, std::invocable<Handle> Cleanup>
class ScopedHandle
{
public:
    constexpr ScopedHandle() = default;
    constexpr explicit ScopedHandle(Handle handle): h{ handle } { }
    constexpr ScopedHandle(const ScopedHandle&) = delete;
    constexpr ScopedHandle(ScopedHandle&& other): h{ std::exchange(other.h, null_handle_value<Handle>) } { }

    constexpr ScopedHandle& operator=(const ScopedHandle&) = delete;
    constexpr ScopedHandle& operator=(ScopedHandle&& rhs)
    {
        if (this == &rhs)
            return *this;
        std::swap(h, rhs.h);
        return *this;
    }

    ~ScopedHandle()
    {
        if (valid())
        {
            cleanup(handle());
        }
    }

    constexpr bool valid() const
    {
        return handle() != null_handle_value<Handle>;
    }

    explicit constexpr operator bool() const
    {
        return valid();
    }

    constexpr Handle handle() const
    {
        return h;
    }

    constexpr void release()
    {
        h = null_handle_value<Handle>;
    }

private:
    Handle h = null_handle_value<Handle>;
    Cleanup cleanup;
};