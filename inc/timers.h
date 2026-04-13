#pragma once

#include "os.h"

namespace Timers
{
    class Stopwatch
    {
    public:
        void start()
        {
            start_ = OS::get_ticks32();
        }

        void stop()
        {
            stop_  = OS::get_ticks32();
        }

        PrimitiveType<OS::Ticks32> ticks() const
        {
            return rep(stop_) - rep(start_);
        }

        // helpers
        PrimitiveType<OS::Ticks32> to_ms() const
        {
            return ticks();
        }

    private:
        OS::Ticks32 start_ = { };
        OS::Ticks32 stop_ = { };
    };
} // namespace Timers