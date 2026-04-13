#include "basic-checkbox.h"

#include "config.h"

namespace UI::Widgets
{
    namespace
    {
        constexpr int checkbox_padding = 2;
    } // namespace [anon]

    Vec2f measure_checkbox(Glyph::RenderFontContext* font_ctx, const CheckboxInput& in)
    {
        const int height = font_ctx->current_font_size();
        const float width = height + checkbox_padding + font_ctx->measure_text(in.label).x;
        return { width, height + 0.f };
    }

    Vec2f build_checkbox_no_label(CmdBuffer::DrawList* lst, Glyph::RenderFontContext* font_ctx, Vec2f pos, Checked checked)
    {
        const auto& colors = Config::widget_colors();

        // Render the checkbox itself.
        CmdBuffer::start_shapes(lst, Render::VertShader::OneOneTransform);
        const int height = font_ctx->current_font_size();

        Vec2f box_height = static_cast<float>(height);
        CmdBuffer::strike_rect(lst, Render::FragShader::BasicColor, pos, box_height, checkbox_padding, colors.window_border);

        CmdBuffer::start_glyph_run(lst, Render::VertShader::OneOneTransform);
        // Render the check state.
        if (is_yes(checked))
        {
            auto glyph_size = font_ctx->icon_glyph_size(Glyph::SpecialGlyph::Checkmark);
            auto check_pos = pos;
            check_pos.x += (box_height.x - glyph_size.x) / 2.f;
            check_pos.y += (box_height.y + glyph_size.y) / 2.f;
            font_ctx->render_icon_glyph_no_offsets(lst, Glyph::SpecialGlyph::Checkmark, check_pos, colors.window_title_font_color);
        }

        const int line_height = font_ctx->current_font_size();
        Vec2f label_pos = pos;
        label_pos.x += box_height.x + checkbox_padding;
        label_pos.y += line_height / 5.f;
        return label_pos;
    }

    Vec2f build_checkbox(CmdBuffer::DrawList* lst, Glyph::RenderFontContext* font_ctx, Vec2f pos, const CheckboxInput& in)
    {
        const auto& colors = Config::widget_colors();

        // Render the checkbox itself.
        CmdBuffer::start_shapes(lst, Render::VertShader::OneOneTransform);
        const int height = font_ctx->current_font_size();

        Vec2f box_height = static_cast<float>(height);
        CmdBuffer::strike_rect(lst, Render::FragShader::BasicColor, pos, box_height, checkbox_padding, colors.window_border);

        CmdBuffer::start_glyph_run(lst, Render::VertShader::OneOneTransform);
        // Render the check state.
        if (is_yes(in.checked))
        {
            auto glyph_size = font_ctx->icon_glyph_size(Glyph::SpecialGlyph::Checkmark);
            auto check_pos = pos;
            check_pos.x += (box_height.x - glyph_size.x) / 2.f;
            check_pos.y += (box_height.y + glyph_size.y) / 2.f;
            font_ctx->render_icon_glyph_no_offsets(lst, Glyph::SpecialGlyph::Checkmark, check_pos, colors.window_title_font_color);
        }

        const int line_height = font_ctx->current_font_size();
        Vec2f label_pos = pos;
        label_pos.x += box_height.x + checkbox_padding;
        label_pos.y += line_height / 5.f;
        font_ctx->render_text(lst, in.label, label_pos, colors.window_title_font_color);
        return label_pos;
    }
} // namespace UI::Widgets