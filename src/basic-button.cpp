#include "basic-button.h"

#include "basic-box.h"

namespace UI::Widgets
{
    Vec2f measure_button(Glyph::RenderFontContext* font_ctx, const BuildButtonInput& in)
    {
        Vec2f btn_size{};
        btn_size.x = font_ctx->measure_text(in.label).x + in.thickness * 2 + in.padding.x * 2;
        btn_size.y = UI::standard_font_padding(Glyph::FontSize{ font_ctx->current_font_size() }) + in.padding.y * 2;
        return btn_size;
    }

    BuildButtonResponse basic_button(CmdBuffer::DrawList* lst, UIState* state, Glyph::RenderFontContext* font_ctx, const BuildButtonInput& in, BuildButtonFlags flags)
    {
        BuildButtonResponse resp{};
        if (in.forced_size != 0.f)
        {
            resp.btn_size = in.forced_size;
        }
        else
        {
            resp.btn_size = measure_button(font_ctx, in);
        }
        BuildBoxInput box_in{
            .id = in.id,
            .pos = in.pos,
            .size = resp.btn_size,
            .thickness = in.thickness
        };
        using Flgs = BuildBoxFlags;
        Flgs box_flags = Flgs::Clickable;
        if (implies(flags, BuildButtonFlags::Strike))
        {
            box_flags |= Flgs::Strike;
        }
        basic_box(lst, state, box_in, box_flags);
        bool fill_box = implies(flags, BuildButtonFlags::Fill);
        if (empty_focus_widget(*state) and hot_widget_set(*state, in.id))
        {
            change_cursor(state, CursorStyle::Select);
            fill_box = true;
        }

        if (fill_box)
        {
            basic_box(lst, state, box_in, Flgs::Fill);
        }

        if (empty_focus_widget(*state) and state->focus_keyboard == in.id)
        {
            auto palette = *CmdBuffer::current_palette(*lst);
            palette.border = palette.outline_selection;
            CmdBuffer::push_color_palette(lst, palette);
            basic_box(lst, state, box_in, Flgs::Strike);
            CmdBuffer::pop_color_palette(lst);
        }
        CmdBuffer::start_glyph_run(lst, Render::VertShader::OneOneTransform);
        auto text_pos = in.pos;
        text_pos.x += (resp.btn_size.x - font_ctx->measure_text(in.label).x) / 2.f;
        text_pos.y += (resp.btn_size.y + font_ctx->current_font_line_height() + in.padding.y * 2) / 5.f;
        const auto* palette = CmdBuffer::current_palette(*lst);
        font_ctx->render_text(lst, in.label, text_pos, palette->text);
        resp.clicked = std_click_trigger(*state, in.id);
        if (down(*state, OS::Key::Return)
            and state->focus_keyboard == in.id
            and empty_focus_widget(*state))
        {
            resp.clicked = true;
        }
        return resp;
    }

    Vec2f measure_iconic_button(Glyph::RenderFontContext* font_ctx, const BuildIconicButtonInput& in)
    {
        Vec2f btn_size{};
        Vec2f ico_size = font_ctx->icon_glyph_size(in.icon);
        btn_size.x = ico_size.x + in.thickness * 2 + in.padding.x * 2;
        btn_size.y = ico_size.y + in.padding.y * 2;
        return btn_size;
    }

    BuildButtonResponse basic_iconic_button(CmdBuffer::DrawList* lst, UIState* state, Glyph::RenderFontContext* font_ctx, const BuildIconicButtonInput& in, BuildButtonFlags flags)
    {
        BuildButtonResponse resp{};
        if (in.forced_size != 0.f)
        {
            resp.btn_size = in.forced_size;
        }
        else
        {
            resp.btn_size = measure_iconic_button(font_ctx, in);
        }
        BuildBoxInput box_in = {
            .id = in.id,
            .pos = in.pos,
            .size = resp.btn_size,
            .thickness = in.thickness
        };
        using Flgs = BuildBoxFlags;
        Flgs box_flags = Flgs::Clickable;
        if (implies(flags, BuildButtonFlags::Strike))
        {
            box_flags |= Flgs::Strike;
        }
        if (implies(flags, BuildButtonFlags::Fill))
        {
            box_flags |= Flgs::Fill;
        }

        basic_box(lst, state, box_in, box_flags);
        if (empty_focus_widget(*state) and hot_widget_set(*state, in.id))
        {
            change_cursor(state, CursorStyle::Select);
            if (not implies(box_flags, Flgs::Fill))
            {
                basic_box(lst, state, box_in, Flgs::Fill);
            }
        }
        CmdBuffer::start_glyph_run(lst, Render::VertShader::OneOneTransform);
        auto text_pos = in.pos;
        Vec2f ico_size = font_ctx->icon_glyph_size(in.icon);
        text_pos.x += (resp.btn_size.x - ico_size.x) / 2.f;
        text_pos.y += (resp.btn_size.y + ico_size.y) / 2.f;
        const auto* palette = CmdBuffer::current_palette(*lst);
        font_ctx->render_icon_glyph_no_offsets(lst, in.icon, text_pos, palette->text);
        resp.clicked = std_click_trigger(*state, in.id);
        return resp;
    }

    Vec2f measure_left_iconic_text_button(Glyph::RenderFontContext* font_ctx, const BuildIconicTextButtonInput& in)
    {
        Vec2f btn_size{};
        Vec2f content_size = font_ctx->icon_glyph_size(in.icon) + in.btn_in.padding + Vec2f{ font_ctx->measure_text(in.btn_in.label).x, 0.f };
        btn_size.x = content_size.x + in.btn_in.thickness * 2 + in.btn_in.padding.x * 2;
        btn_size.y = UI::standard_font_padding(Glyph::FontSize{ font_ctx->current_font_size() }) + in.btn_in.padding.y * 2;
        return btn_size;
    }

    BuildButtonResponse basic_left_iconic_text_button(CmdBuffer::DrawList* lst, UIState* state, Glyph::RenderFontContext* font_ctx, const BuildIconicTextButtonInput& in, BuildButtonFlags flags)
    {
        BuildButtonResponse resp{};
        resp.btn_size = measure_left_iconic_text_button(font_ctx, in);
        BuildBoxInput box_in{
            .id = in.btn_in.id,
            .pos = in.btn_in.pos,
            .size = resp.btn_size,
            .thickness = in.btn_in.thickness
        };
        using Flgs = BuildBoxFlags;
        Flgs box_flags = Flgs::Clickable;
        if (implies(flags, BuildButtonFlags::Strike))
        {
            box_flags |= Flgs::Strike;
        }
        basic_box(lst, state, box_in, box_flags);
        bool fill_box = implies(flags, BuildButtonFlags::Fill);
        if (empty_focus_widget(*state) and hot_widget_set(*state, in.btn_in.id))
        {
            change_cursor(state, CursorStyle::Select);
            fill_box = true;
        }

        if (fill_box)
        {
            basic_box(lst, state, box_in, Flgs::Fill);
        }

        if (empty_focus_widget(*state) and state->focus_keyboard == in.btn_in.id)
        {
            auto palette = *CmdBuffer::current_palette(*lst);
            palette.border = palette.outline_selection;
            CmdBuffer::push_color_palette(lst, palette);
            basic_box(lst, state, box_in, Flgs::Strike);
            CmdBuffer::pop_color_palette(lst);
        }
        CmdBuffer::start_glyph_run(lst, Render::VertShader::OneOneTransform);
        const auto* palette = CmdBuffer::current_palette(*lst);

        auto text_pos = in.btn_in.pos;
        Vec2f ico_size = font_ctx->icon_glyph_size(in.icon);
        text_pos.x += in.btn_in.padding.x * 2;
        text_pos.y += (resp.btn_size.y + ico_size.y) / 2.f;
        if (not implies(flags, BuildButtonFlags::HideIcon))
        {
            font_ctx->render_icon_glyph_no_offsets(lst, in.icon, text_pos, in.icon_color);
        }
        text_pos.x += font_ctx->icon_glyph_size(in.icon).x;

        text_pos.x += in.btn_in.padding.x;
        text_pos.y = in.btn_in.pos.y + (resp.btn_size.y + font_ctx->current_font_line_height() + in.btn_in.padding.y * 2) / 5.f;
        font_ctx->render_text(lst, in.btn_in.label, text_pos, palette->text);
        resp.clicked = std_click_trigger(*state, in.btn_in.id);
        if (down(*state, OS::Key::Return)
            and state->focus_keyboard == in.btn_in.id
            and empty_focus_widget(*state))
        {
            resp.clicked = true;
        }
        return resp;
    }
} // namespace UI::Widgets