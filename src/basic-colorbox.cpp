#include "basic-colorbox.h"

#include "config.h"

namespace UI::Widgets
{
    namespace
    {
        constexpr int colorbox_padding = 2;
    } // namespace [anon]

    Vec2f measure_colorbox(Glyph::RenderFontContext* font_ctx, const ColorboxData& data)
    {
        const int height = font_ctx->current_font_size();
        const float width = height + colorbox_padding + font_ctx->measure_text(data.label).x;
        return { width, height + 0.f };
    }

    Vec2f build_colorbox(CmdBuffer::DrawList* lst, Glyph::RenderFontContext* font_ctx, Vec2f pos, const ColorboxData& data)
    {
        auto label_pos = build_colorbox_no_label(lst, font_ctx, pos, data.color);
        auto* palette = CmdBuffer::current_palette(*lst);
        CmdBuffer::start_glyph_run(lst, Render::VertShader::OneOneTransform);
        pos = font_ctx->render_text(lst, data.label, label_pos, palette->text);
        return pos;
    }

    Vec2f build_colorbox_no_label(CmdBuffer::DrawList* lst, Glyph::RenderFontContext* font_ctx, Vec2f pos, const Vec4f& color)
    {
        // Render the colorbox itself.
        CmdBuffer::start_shapes(lst, Render::VertShader::OneOneTransform);
        const int height = font_ctx->current_font_size();

        Vec2f box_height = static_cast<float>(height);
        // Draw the color.
        CmdBuffer::solid_rect(lst, Render::FragShader::BasicColor, pos, box_height, color);

        const int line_height = font_ctx->current_font_size();
        Vec2f label_pos = pos;
        label_pos.x += box_height.x + colorbox_padding;
        label_pos.y += line_height / 5.f;
        return label_pos;
    }
} // namespace UI::Widgets