#pragma once

#include <string_view>

#include "cmd-buffer-api.h"
#include "gap-strings.h"
#include "glyph-cache.h"
#include "types.h"
#include "ui-common.h"

namespace UI::Widgets
{
    CmdBuffer::DrawList* begin_tooltip(UIState* state, CmdBuffer::DrawList* core_lst, const CmdBuffer::ClipRect& clip);
    void end_tooltip(UIState* state);

    struct TextTooltipInput
    {
        std::string_view text;
        Vec2f padding{};
        Vec2i screen_pos{}; // Since we render 'up', this will be the bottom-left of the rect.
    };

    // Will call begin/end tooltip.
    void basic_text_tooltip(UIState* state, CmdBuffer::DrawList* core_lst, Glyph::RenderFontContext* font_ctx, const TextTooltipInput& in);

    struct MultilineTextTooltipInput
    {
        String8List lines;
        Vec2f padding{};
        Vec2i screen_pos{};
    };

    // Will call begin/end tooltip.
    void basic_multiline_text_tooltip(UIState* state, CmdBuffer::DrawList* core_lst, Glyph::RenderFontContext* font_ctx, const MultilineTextTooltipInput& in);
} // namespace UI::Widgets