#pragma once

#include "os.h"
#include "types.h"

namespace Feed
{
    class MessageFeed;
} // namespace Feed

namespace Theme
{
    void apply_border_color(OS::OSWindow window, Feed::MessageFeed* feed);
} // namespace Theme