#include "tooltips.h"

#include <cassert>

#include "basic-textbox.h"

namespace UI::Widgets
{
    CmdBuffer::DrawList* begin_tooltip(UIState* state, CmdBuffer::DrawList* core_lst, const CmdBuffer::ClipRect& content_clip)
    {
        if (state->tooltip.lst == nullptr)
        {
            state->tooltip.lst = CmdBuffer::alloc_draw_list();
        }
        CmdBuffer::DrawList* lst = state->tooltip.lst;
        // Start the frame for the tooltip frame.
        CmdBuffer::new_frame(lst,
                                CmdBuffer::screen(*core_lst),
                                { .dt = CmdBuffer::delta_time(*core_lst),
                                    .app_time = CmdBuffer::app_time(*core_lst) });
        // Default texture (atlas by default).
        CmdBuffer::push_texture(lst, CmdBuffer::current_texture(*core_lst));
        // Default palette.
        CmdBuffer::push_color_palette(lst, *CmdBuffer::current_palette(*core_lst));

        // Compute the expansion of the given clip.
        CmdBuffer::ClipRect clip = expand_clip_center(content_clip, 1.f - state->tooltip.expand_offset);
        CmdBuffer::push_clip(lst, clip);

        CmdBuffer::standard_window_blur(lst, clip);

        // Border.
        CmdBuffer::start_shapes(lst, Render::VertShader::OneOneTransform);
        {
            Vec2f left{ 0.f, 0.f };
            Vec2f size{ rep(clip.width) + 0.f, rep(clip.height) + 0.f };
            // First lets clear the rect.
            const auto* palette = CmdBuffer::current_palette(*lst);
            auto bg_color = palette->background;
            bg_color.a = 0.8f;
            CmdBuffer::solid_rect(lst, Render::FragShader::BasicColor, left, size, bg_color);
            // Now strike it with the color we want.
            CmdBuffer::strike_rect(lst, Render::FragShader::BasicColor, left, size, 2.f, palette->border);
        }

        state->tooltip.enabled = true;
        return state->tooltip.lst;
    }

    void end_tooltip(UIState* state)
    {
        assert(state->tooltip.lst != nullptr);
        CmdBuffer::DrawList* lst = state->tooltip.lst;
        CmdBuffer::pop_clip(lst);
        CmdBuffer::pop_texture(lst);
        CmdBuffer::pop_color_palette(lst);
    }

    void basic_text_tooltip(UIState* state, CmdBuffer::DrawList* core_lst, Glyph::RenderFontContext* font_ctx, const TextTooltipInput& in)
    {
        Vec2f size{};
        size.x = font_ctx->measure_text(in.text).x + in.padding.x * 2;
        size.y = font_ctx->current_font_line_height() + in.padding.y * 2;
        CmdBuffer::ClipRect tip_clip{};
        tip_clip.width = Width{ static_cast<int>(size.x) };
        tip_clip.height = Height{ static_cast<int>(size.y) };
        move_clip_to_absolute(&tip_clip, state->mouse.ui_mouse);
        clamp_clip_to(&tip_clip, CmdBuffer::screen(*core_lst));

        CmdBuffer::DrawList* tip_lst = begin_tooltip(state, core_lst, tip_clip);

        ClipAlignedTextboxInput txt_in{
            .text = in.text,
            .border_width = {},
            .align = TextAlign::Center
        };
        build_clip_aligned_textbox(tip_lst, font_ctx, txt_in);

        end_tooltip(state);
    }

    void basic_multiline_text_tooltip(UIState* state, CmdBuffer::DrawList* core_lst, Glyph::RenderFontContext* font_ctx, const MultilineTextTooltipInput& in)
    {
        Vec2f size{};
        for EachNode(n, in.lines.first)
        {
            size.x = std::max(font_ctx->measure_text(n->string).x + in.padding.x * 2, size.x);
        }
        size.y = font_ctx->current_font_line_height() * in.lines.node_count + in.padding.y * 2;
        CmdBuffer::ClipRect tip_clip{};
        tip_clip.width = Width{ static_cast<int>(size.x) };
        tip_clip.height = Height{ static_cast<int>(size.y) };
        move_clip_to_absolute(&tip_clip, state->mouse.ui_mouse);
        clamp_clip_to(&tip_clip, CmdBuffer::screen(*core_lst));

        CmdBuffer::DrawList* tip_lst = begin_tooltip(state, core_lst, tip_clip);

        const auto* palette = CmdBuffer::current_palette(*tip_lst);

        Vec2f pos;
        const int line_h = font_ctx->current_font_line_height();
        pos.y = rep(tip_clip.height) + in.padding.y;
        pos.x = in.padding.x;

        CmdBuffer::start_glyph_run(tip_lst, Render::VertShader::OneOneTransform);
        for EachNode(n, in.lines.first)
        {
            pos.y -= line_h;
            font_ctx->render_text(tip_lst, n->string, pos, palette->text);
        }

        end_tooltip(state);
    }
} // namespace UI::Widgets