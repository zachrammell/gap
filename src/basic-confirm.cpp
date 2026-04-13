#include "basic-confirm.h"

#include "basic-button.h"
#include "basic-window.h"
#include "config.h"

namespace UI::Widgets
{
    struct ConfirmDlg::Data
    {
        BasicWindow window;
        bool spawn = false;

        static constexpr float padding = 2.f;
    };

    namespace
    {
        Render::RenderViewport ideal_window_viewport(const CmdBuffer::ClipRect& clip, Glyph::RenderFontContext* font_ctx, Height window_title_height, float horiz_window_padding, const BuildConfirmDlgInput& in)
        {
            Vec2f size{};
            size.y = rep(window_title_height) + 0.f;
            // Measure size of description.
            size.y += font_ctx->current_font_line_height();
            size.x = font_ctx->measure_text(in.description).x;
            // Measure buttons.
            // Since they're all on the same row, we can simply set the y component at the end.
            Vec2f btn_size{};
            BuildButtonInput btn_in{
                .padding = { ConfirmDlg::Data::padding, 0.f },
                .thickness = ConfirmDlg::Data::padding
            };
            for (auto first = in.first; first != in.last; ++first)
            {
                btn_in.label = first->btn_label;
                auto btn_measure = measure_button(font_ctx, btn_in);
                // Accumulate 'x'.  Set 'y'.
                btn_size.x = std::max(btn_size.x, btn_measure.x);
                btn_size.y = btn_measure.y;
            }
            int count = static_cast<int>(in.last - in.first);
            float total_button_x = (count - 1) * ConfirmDlg::Data::padding + count * btn_size.x;
            size.x = std::max(size.x, total_button_x);
            size.y += btn_size.y + font_ctx->current_font_line_height() / 2.f;
            size.x += horiz_window_padding * 2;
            auto half_size = size / 2.f;
            // Convert to viewport.
            auto center = center_clip(clip);
            auto wnd_clip = vec_as_clip(Vec4f{
                center.x - half_size.x,
                center.y - half_size.y,
                center.x + half_size.x,
                center.y + half_size.y
            });
            return convert(wnd_clip);
        }
    } // namespace [anon]

    ConfirmDlg::ConfirmDlg():
        data{ new Data{
                    .window = { UI::Widgets::ID::ConfirmDlg },
        } }
    { }

    ConfirmDlg::~ConfirmDlg() = default;

    // Interaction.
    void ConfirmDlg::sync_config(Glyph::Atlas* atlas)
    {
        data->window.sync_config(atlas);
    }

    void ConfirmDlg::start(std::string_view title)
    {
        data->spawn = true;
        data->window.title(title);
        data->window.background_alpha(0.8f);
    }

    BuildConfirmDlgResponse ConfirmDlg::build(CmdBuffer::DrawList* lst, UI::UIState* state, Glyph::Atlas* atlas, const BuildConfirmDlgInput& in)
    {
        BuildConfirmDlgResponse resp{};
        // Process input.
        {
            if (down(*state, OS::Key::Right))
            {
                // Try to find the current focus widget.
                auto selected = in.last;
                for (auto first = in.first; first != in.last; ++first)
                {
                    if (state->focus_keyboard == first->btn_id)
                    {
                        selected = first;
                        break;
                    }
                }
                // Select the first.
                if (selected == in.last)
                {
                    selected = in.first;
                }
                else
                {
                    ++selected;
                    // Wrap around.
                    if (selected == in.last)
                    {
                        selected = in.first;
                    }
                }
                state->focus_keyboard = selected->btn_id;
            }

            if (down(*state, OS::Key::Left))
            {
                // Try to find the current focus widget.
                auto selected = in.last;
                for (auto first = in.first; first != in.last; ++first)
                {
                    if (state->focus_keyboard == first->btn_id)
                    {
                        selected = first;
                        break;
                    }
                }
                // Select the first.
                if (selected == in.last)
                {
                    selected = in.first;
                }
                else
                {
                    // Wrap around.
                    if (selected == in.first)
                    {
                        selected = in.last;
                    }
                    --selected;
                }
                state->focus_keyboard = selected->btn_id;
            }
        }
        const auto font_size = Glyph::FontSize{ Config::widget_state().window_title_font_size };
        auto font_ctx = atlas->render_font_context(font_size);
        // Render widgets.
        {
            if (data->spawn)
            {
                auto clip = CmdBuffer::current_clip(*lst);
                // Compute the window size given the button and description inputs.
                auto window_vp = ideal_window_viewport(clip, &font_ctx, data->window.min_viewable_window_content_height(), data->window.horiz_padding(), in);
                ShowWindowData show_data{
                    .initial_viewport = window_vp,
                    .expand_point = { 0.5f, 0.5f }
                };
                data->window.show(show_data);
                data->spawn = false;
                // Focus the first button.
                state->focus_keyboard = in.first->btn_id;
            }
            auto window_resp = data->window.build(lst, atlas, state);
            resp.close = window_resp.close;
        }
        const auto* palette = CmdBuffer::current_palette(*lst);
        // Description.
        auto content_vp = data->window.content_viewport(data->window.window_viewport());
        CmdBuffer::push_clip(lst, convert(content_vp));
        CmdBuffer::start_glyph_run(lst, Render::VertShader::OneOneTransform);
        Vec2f pos{};
        pos.y = static_cast<float>(rep(content_vp.height) - font_ctx.current_font_line_height());
        font_ctx.render_text(lst, in.description, pos, palette->text);
        pos.y -= font_ctx.current_font_line_height() + font_ctx.current_font_line_height() / 2.f;
        // Render buttons.
        BuildButtonInput btn_in{
            .padding = { ConfirmDlg::Data::padding, 0.f },
            .thickness = ConfirmDlg::Data::padding
        };
        // Find the longest length button.
        float largest_btn_x = 0.f;
        for (auto first = in.first; first != in.last; ++first)
        {
            largest_btn_x = std::max(largest_btn_x, font_ctx.measure_text(first->btn_label).x);
        }
        largest_btn_x += btn_in.thickness * 2;
        btn_in.forced_size = Vec2f{ largest_btn_x, standard_font_padding(Glyph::FontSize{ font_ctx.current_font_size() }) } + btn_in.padding * 2;

        for (auto first = in.first; first != in.last; ++first)
        {
            btn_in.id = first->btn_id;
            btn_in.label = first->btn_label;
            btn_in.pos = pos;
            auto btn_resp = basic_button(lst, state, &font_ctx, btn_in, BuildButtonFlags::Strike);
            if (btn_resp.clicked)
            {
                resp.clicked = btn_in.id;
            }
            pos.x += btn_resp.btn_size.x + Data::padding;
        }
        CmdBuffer::pop_clip(lst);

        // This dialog is modal, so if there are any events, we want to eat them all.
        try_set_focus_widget(state, UI::Widgets::ID::ConfirmDlg);

        data->window.end(state);
        return resp;
    }
} // namespace UI::Widgets