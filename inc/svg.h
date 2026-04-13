#pragma once

#include <string_view>

#include "assets.h"
#include "renderer.h"
#include "types.h"

namespace Feed
{
    class MessageFeed;
} // namespace Feed

namespace SVG
{
    struct LoadSVGResult
    {
        Render::BasicTexture tex;
        ScreenDimensions size;
    };

    // Basic interface.
    LoadSVGResult load_svg_asset(Assets::AssetID id, Feed::MessageFeed* feed);
    LoadSVGResult load_svg_asset(Assets::AssetID id, Feed::MessageFeed* feed, ScreenDimensions dim);
} // namespace SVG