#pragma once

#include <memory>
#include <string_view>

#include "gap-strings.h"
#include "glyph-cache.h"
#include "vec.h"

namespace UI::Widgets
{
    class MultilineTextbox
    {
    public:
        struct Data;

        MultilineTextbox();
        ~MultilineTextbox();

        // Interaction.
        void text(std::string_view text);
        void text(String8List strings);
        void offset(const Vec2f& offset);
        void font_size(Glyph::FontSize size);

        // Queries.
        Vec2f content_size(Glyph::Atlas* atlas) const;
        std::string_view text() const;

        // Building.
        void build(CmdBuffer::DrawList* lst, Glyph::Atlas* atlas);
    private:
        std::unique_ptr<Data> data;
    };

    enum class TextAlign
    {
        Left,
        Center,
        Right,
    };

    struct ClipAlignedTextboxInput
    {
        std::string_view text;
        Width border_width;
        TextAlign align;
    };

    struct TextboxInput
    {
        std::string_view text;
        Vec2f pos{}; // Bottom-left.
        Vec2f padding{};
        float thickness{};
    };

    enum class BuildTextboxFlags : uint32_t
    {
        None   = 0,
        Fill   = 1 << 0,
        Strike = 1 << 1,
    };

    struct MultiLineTextboxInput
    {
        std::string_view text;
        Vec2f pos{}; // Top-left.
        Vec2f padding{};
        TextAlign align = TextAlign::Left;
    };

    void build_clip_aligned_textbox(CmdBuffer::DrawList* lst, Glyph::RenderFontContext* font_ctx, const ClipAlignedTextboxInput& in);
    Vec2f measure_textbox(Glyph::RenderFontContext* font_ctx, const TextboxInput& in);
    void build_textbox(CmdBuffer::DrawList* lst, UIState* state, Glyph::RenderFontContext* font_ctx, const TextboxInput& in, BuildTextboxFlags flags);
    Vec2f measure_multiline_textbox(Glyph::RenderFontContext* font_ctx, const MultiLineTextboxInput& in);
    void build_multiline_textbox(CmdBuffer::DrawList* lst, Glyph::RenderFontContext* font_ctx, const MultiLineTextboxInput& in);
} // namespace UI::Widgets