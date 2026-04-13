#pragma once

#include <string_view>

#include "glyph-cache.h"
#include "renderer.h"

namespace UI::Widgets
{
    struct ColorboxData
    {
        std::string_view label;
        Vec4f color;
    };

    Vec2f measure_colorbox(Glyph::RenderFontContext* font_ctx, const ColorboxData& data);
    Vec2f build_colorbox(CmdBuffer::DrawList* lst, Glyph::RenderFontContext* font_ctx, Vec2f pos, const ColorboxData& data);
    // Returns the label position.
    Vec2f build_colorbox_no_label(CmdBuffer::DrawList* lst, Glyph::RenderFontContext* font_ctx, Vec2f pos, const Vec4f& color);
} // namespace UI::Widgets