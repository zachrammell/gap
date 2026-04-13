#pragma once

#include <string_view>

#include "glyph-cache.h"
#include "renderer.h"

namespace UI::Widgets
{
    enum class Checked : bool { No, Yes };

    struct CheckboxInput
    {
        std::string_view label;
        Checked checked;
    };

    Vec2f measure_checkbox(Glyph::RenderFontContext* font_ctx, const CheckboxInput& in);
    // Returns the position of the label.
    Vec2f build_checkbox_no_label(CmdBuffer::DrawList* lst, Glyph::RenderFontContext* font_ctx, Vec2f pos, Checked checked);
    Vec2f build_checkbox(CmdBuffer::DrawList* lst, Glyph::RenderFontContext* font_ctx, Vec2f pos, const CheckboxInput& in);
} // namespace UI::Widgets