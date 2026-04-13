#pragma once

#include <string_view>

#include "cmd-buffer-api.h"
#include "glyph-cache.h"
#include "types.h"
#include "ui-common.h"

namespace UI::Widgets
{
    enum class BuildButtonFlags : uint32_t
    {
        None     = 0,
        Strike   = 1U << 0,
        Fill     = 1U << 1,
        HideIcon = 1U << 2,
    };

    struct BuildButtonInput
    {
        ID id = ID::Zero;
        std::string_view label;
        Vec2f pos{}; // Bottom-left.
        Vec2f padding{};
        Vec2f forced_size{}; // If non-zero, this will be the forced button size.
        float thickness{};
    };

    struct BuildIconicButtonInput
    {
        ID id = ID::Zero;
        Glyph::SpecialGlyph icon;
        Vec2f pos{}; // Bottom-left.
        Vec2f padding{};
        Vec2f forced_size{};
        float thickness{};
    };

    struct BuildIconicTextButtonInput
    {
        BuildButtonInput btn_in;
        Glyph::SpecialGlyph icon;
        Vec4f icon_color{};
    };

    struct BuildButtonResponse
    {
        bool clicked = false;
        Vec2f btn_size;
    };

    Vec2f measure_button(Glyph::RenderFontContext* font_ctx, const BuildButtonInput& in);
    BuildButtonResponse basic_button(CmdBuffer::DrawList* lst, UIState* state, Glyph::RenderFontContext* font_ctx, const BuildButtonInput& in, BuildButtonFlags flags);

    Vec2f measure_iconic_button(Glyph::RenderFontContext* font_ctx, const BuildIconicButtonInput& in);
    BuildButtonResponse basic_iconic_button(CmdBuffer::DrawList* lst, UIState* state, Glyph::RenderFontContext* font_ctx, const BuildIconicButtonInput& in, BuildButtonFlags flags);

    Vec2f measure_left_iconic_text_button(Glyph::RenderFontContext* font_ctx, const BuildIconicTextButtonInput& in);
    BuildButtonResponse basic_left_iconic_text_button(CmdBuffer::DrawList* lst, UIState* state, Glyph::RenderFontContext* font_ctx, const BuildIconicTextButtonInput& in, BuildButtonFlags flags);
} // namespace UI::Widgets