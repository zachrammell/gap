#include "basic-colorpicker.h"

#include <algorithm>

#include "basic-textbox.h"
#include "basic-window.h"
#include "config.h"
#include "gap-strings.h"
#include "util.h"

namespace UI::Widgets
{
    namespace
    {
        enum class DragTarget
        {
            None,
            Hue,
            SV,
            Alpha,
        };
    } // namespace [anon]

    struct ColorPicker::Data
    {
        BasicWindow window;
        ID hue_id = ID::Zero;
        ID sv_id = ID::Zero;
        ID alpha_id = ID::Zero;
        ID initial_color_id = ID::Zero;
        Glyph::Atlas* atlas;
        Glyph::FontSize font_size = Glyph::FontSize{ 12 };
        Vec4f picker_color_hsv;
        Vec4f initial_color_rgb;

        static constexpr int padding = 2;
        static constexpr int slider_widths = 20;
        static constexpr int selected_color_size = 10;
    };

    namespace
    {
        Render::RenderViewport initial_window_viewport(const ScreenDimensions& screen)
        {
            auto viewport = Render::RenderViewport::basic(screen);
            viewport.width = Width{ rep(viewport.width) / 4 };
            viewport.height = Height{ rep(viewport.height) / 3 };
            viewport.offset_x = Render::ViewportOffsetX{ (rep(screen.width) - rep(viewport.width)) / 2 };
            viewport.offset_y = Render::ViewportOffsetY{ (rep(screen.height) - rep(viewport.height)) / 2 };
            return viewport;
        }

        struct ColorValuesViewports
        {
            Render::RenderViewport hex;
            Render::RenderViewport alpha;
            Render::RenderViewport r;
            Render::RenderViewport g;
            Render::RenderViewport b;
            Render::RenderViewport h;
            Render::RenderViewport s;
            Render::RenderViewport v;
        };

        struct ColorPickerWindowContentViewports
        {
            Render::RenderViewport hue_vp;
            Render::RenderViewport alpha_vp;
            Render::RenderViewport sv_vp;
            Render::RenderViewport selected_vp;
            Render::RenderViewport initial_color_vp;
            ColorValuesViewports color_values_vp;
        };

        ColorPickerWindowContentViewports colorpicker_window_content_viewports(ColorPicker::Data* data, const Render::RenderViewport& viewport)
        {
            auto hue_vp = viewport;
            hue_vp.height = Height{ ColorPicker::Data::slider_widths };
            hue_vp.offset_y = Render::ViewportOffsetY{ rep(viewport.offset_y) + rep(viewport.height) - rep(hue_vp.height) };
            hue_vp.width = Width{ rep(viewport.width) - ColorPicker::Data::padding - ColorPicker::Data::slider_widths };

            // Hex Alpha
            // R  H
            // G  S
            // B  V
            const int desc_boxes_height = static_cast<int>(UI::standard_font_padding(data->font_size)) * 4 + ColorPicker::Data::padding * 4;

            auto sv_vp = hue_vp;
            auto sv_vp_height = std::max(0, rep(viewport.height) - rep(hue_vp.height) - ColorPicker::Data::padding - desc_boxes_height);
            sv_vp.offset_y = Render::ViewportOffsetY{ rep(sv_vp.offset_y) - sv_vp_height - ColorPicker::Data::padding };
            sv_vp.height = Height{ sv_vp_height };
            // Adjust the width to fit the alpha and then the selected color viewports.
            auto sv_vp_width = (rep(viewport.width) - (ColorPicker::Data::slider_widths + ColorPicker::Data::padding * 4)) / 2;
            sv_vp.width = Width{ sv_vp_width };

            auto alpha_vp = sv_vp;
            alpha_vp.width = Width{ ColorPicker::Data::slider_widths };
            alpha_vp.offset_x = Render::ViewportOffsetX{ rep(sv_vp.offset_x) + sv_vp_width + ColorPicker::Data::padding * 2 };

            auto selected_vp = sv_vp;
            selected_vp.offset_x = Render::ViewportOffsetX{ rep(alpha_vp.offset_x) + rep(alpha_vp.width) + ColorPicker::Data::padding * 2 };

            auto initial_color_vp = hue_vp;
            initial_color_vp.width = Width{ ColorPicker::Data::slider_widths };
            initial_color_vp.offset_x = Render::ViewportOffsetX{ rep(hue_vp.offset_x) + rep(hue_vp.width) + ColorPicker::Data::padding };

            // Compute picker values viewports.
            ColorValuesViewports values{};
            values.hex.height = Height{ static_cast<int>(UI::standard_font_padding(data->font_size)) };
            values.hex.width = Width{ rep(viewport.width) / 2 - ColorPicker::Data::padding };
            values.hex.offset_x = viewport.offset_x;
            values.hex.offset_y = offset_from(sv_vp.offset_y, -(rep(values.hex.height) + ColorPicker::Data::padding));

            values.r = values.hex;
            values.r.offset_y = offset_from(values.hex.offset_y, -(rep(values.r.height) + ColorPicker::Data::padding));

            values.g = values.hex;
            values.g.offset_y = offset_from(values.r.offset_y, -(rep(values.g.height) + ColorPicker::Data::padding));

            values.b = values.hex;
            values.b.offset_y = offset_from(values.g.offset_y, -(rep(values.b.height) + ColorPicker::Data::padding));

            values.alpha = values.hex;
            values.alpha.offset_x = offset_from(values.hex.offset_x, rep(values.hex.width) + ColorPicker::Data::padding);

            values.h = values.alpha;
            values.h.offset_y = offset_from(values.alpha.offset_y, -(rep(values.h.height) + ColorPicker::Data::padding));

            values.s = values.alpha;
            values.s.offset_y = offset_from(values.h.offset_y, -(rep(values.s.height) + ColorPicker::Data::padding));

            values.v = values.alpha;
            values.v.offset_y = offset_from(values.s.offset_y, -(rep(values.v.height) + ColorPicker::Data::padding));

            return {
                .hue_vp = hue_vp,
                .alpha_vp = alpha_vp,
                .sv_vp = sv_vp,
                .selected_vp = selected_vp,
                .initial_color_vp = initial_color_vp,
                .color_values_vp = values
            };
        }

        void setup_ui_data(ColorPicker::Data* data, const Vec4f& color)
        {
            data->window.background_alpha(0.8f);
            data->picker_color_hsv = rgb_to_hsv(color);
            data->initial_color_rgb = color;
        }

        void populate_hue_for_mouse_pos(ColorPicker::Data* data, const Vec2i& mouse_pos, const Render::RenderViewport& viewport)
        {
            auto adjusted_mouse = adjusted_mouse_for_viewport(mouse_pos, viewport);
            float x = std::clamp(static_cast<float>(adjusted_mouse.x), 0.f, rep(viewport.width) + 0.f);
            float hue = x / rep(viewport.width);
            data->picker_color_hsv.x = hue;
        }

        void populate_sv_for_mouse_pos(ColorPicker::Data* data, const Vec2i& mouse_pos, const Render::RenderViewport& viewport)
        {
            auto adjusted_mouse = adjusted_mouse_for_viewport(mouse_pos, viewport);
            float x = std::clamp(static_cast<float>(adjusted_mouse.x), 0.f, rep(viewport.width) + 0.f);
            float y = std::clamp(static_cast<float>(adjusted_mouse.y), 0.f, rep(viewport.height) + 0.f);
            float sat = x / rep(viewport.width);
            float value = y / rep(viewport.height);
            data->picker_color_hsv.y = sat;
            data->picker_color_hsv.z = value;
        }

        void populate_alpha_for_mouse_pos(ColorPicker::Data* data, const Vec2i& mouse_pos, const Render::RenderViewport& viewport)
        {
            auto adjusted_mouse = adjusted_mouse_for_viewport(mouse_pos, viewport);
            float y = std::clamp(static_cast<float>(adjusted_mouse.y), 0.f, rep(viewport.height) + 0.f);
            float alpha = y / rep(viewport.height);
            data->picker_color_hsv.a = alpha;
        }

        struct LabelViewport
        {
            std::string_view label;
            const Render::RenderViewport* viewport;
        };

        void build_color_values(ColorPicker::Data* data, CmdBuffer::DrawList* lst, const ColorValuesViewports& values_vp, const Vec4f& current_rgb)
        {
            // For each column, try to align each of the labels.
            const LabelViewport lhs_labels[] =
            {
                { "Hex: ", &values_vp.hex },
                { "R: ", &values_vp.r },
                { "G: ", &values_vp.g },
                { "B: ", &values_vp.b },
            };

            const LabelViewport rhs_labels[] =
            {
                { "Alpha: ", &values_vp.alpha },
                { "H: ", &values_vp.h },
                { "S: ", &values_vp.s },
                { "V: ", &values_vp.v },
            };

            // This will also hold our float values of 0.00. + 1 for null byte.
            char buf[] = "0x000000000";

            const auto& colors = Config::widget_colors();
            auto font_ctx = data->atlas->render_font_context(data->font_size);
            // Let's just heuristically measure the length for now.
            float char_width = font_ctx.measure_text("0").x;
            // Render columns.
            {
                CmdBuffer::start_glyph_run(lst, Render::VertShader::OneOneTransform);
                int longest_len_str = static_cast<int>(lhs_labels[0].label.length());
                float longest_len = longest_len_str * char_width;
                Vec2f pos;
                // Labels LHS.
                for (const LabelViewport& label : lhs_labels)
                {
                    CmdBuffer::push_clip(lst, UI::convert(*label.viewport));
                    pos.x = longest_len - char_width * label.label.length();
                    pos.y = (rep(label.viewport->height) + font_ctx.current_font_size()) / 5.f;
                    font_ctx.render_text(lst, label.label, pos, colors.window_title_font_color);
                    CmdBuffer::pop_clip(lst);
                }

                ClipAlignedTextboxInput in{
                    .text = { },
                    .border_width = Width{ ColorPicker::Data::padding },
                    .align = TextAlign::Center
                };

                // Hex.
                {
                    auto hex_value = vec4f_to_hex(current_rgb);
                    in.text = fmt_string_sv(buf, "%#08x", hex_value);
                    auto value_vp = values_vp.hex;
                    size_offset_viewport(&value_vp, { -static_cast<int>(longest_len), 0 });
                    offset_viewport(&value_vp, { static_cast<int>(longest_len), 0 });
                    CmdBuffer::push_clip(lst, UI::convert(value_vp));
                    build_clip_aligned_textbox(lst, &font_ctx, in);
                    CmdBuffer::pop_clip(lst);
                }

                auto render_color = [&](float c, const Render::RenderViewport& viewport)
                {
                    // Note: Workaround for MSVC warning in std::size.
                    in.text = fmt_string_sv(buf, "%0.2f", c);
                    auto value_vp = viewport;
                    size_offset_viewport(&value_vp, { -static_cast<int>(longest_len), 0 });
                    offset_viewport(&value_vp, { static_cast<int>(longest_len), 0 });
                    CmdBuffer::push_clip(lst, UI::convert(value_vp));
                    build_clip_aligned_textbox(lst, &font_ctx, in);
                    CmdBuffer::pop_clip(lst);
                };

                // R.
                {
                    render_color(current_rgb.x, values_vp.r);
                }

                // G.
                {
                    render_color(current_rgb.y, values_vp.g);
                }

                // B.
                {
                    render_color(current_rgb.z, values_vp.b);
                }

                longest_len_str = static_cast<int>(rhs_labels[0].label.length());
                longest_len = longest_len_str * char_width;
                CmdBuffer::start_glyph_run(lst, Render::VertShader::OneOneTransform);
                // Labels RHS.
                for (const LabelViewport& label : rhs_labels)
                {
                    CmdBuffer::push_clip(lst, UI::convert(*label.viewport));
                    pos.x = longest_len - char_width * label.label.length();
                    pos.y = (rep(label.viewport->height) + font_ctx.current_font_size()) / 5.f;
                    font_ctx.render_text(lst, label.label, pos, colors.window_title_font_color);
                    CmdBuffer::pop_clip(lst);
                }

                // Alpha.
                {
                    render_color(current_rgb.a, values_vp.alpha);
                }

                // H.
                {
                    render_color(data->picker_color_hsv.x, values_vp.h);
                }

                // S.
                {
                    render_color(data->picker_color_hsv.y, values_vp.s);
                }

                // V.
                {
                    render_color(data->picker_color_hsv.z, values_vp.v);
                }
            }
        }
    } // namespace [anon]

    ColorPicker::ColorPicker(Glyph::Atlas* atlas, ID id):
        data{ new Data{
                .window = { id },
                .hue_id = { make_id_seed(id, "hue") },
                .sv_id = { make_id_seed(id, "sv") },
                .alpha_id = { make_id_seed(id, "alpha") },
                .initial_color_id = { make_id_seed(id, "initial") },
                .atlas = atlas } }
    {
        data->window.title("Color Picker");
    }

    ColorPicker::~ColorPicker() = default;

    // Interaction.
    void ColorPicker::start(const ScreenDimensions& screen, const Vec2i& mouse_pos, const Vec4f& color, UIState* state)
    {
        auto adjusted_mouse = adjusted_mouse_for_viewport(mouse_pos, Render::RenderViewport::basic(screen));
        auto show_data = spawn_window_at_point(screen, initial_window_viewport(screen), adjusted_mouse);
        show_data.options |= ShowWindowOptions::HideTitlebar;
        data->window.show(show_data);
        set_focus_window(state, data->window.id());
        setup_ui_data(data.get(), color);
    }

    void ColorPicker::sync_config()
    {
        data->font_size = Glyph::FontSize{ Config::widget_state().window_title_font_size };
        data->window.sync_config(data->atlas);
        setup_ui_data(data.get(), hsv_to_rgb(data->picker_color_hsv));
    }

    // Queries.
    ID ColorPicker::id() const
    {
        return data->window.id();
    }

    BuildColorPickerResponse ColorPicker::build(CmdBuffer::DrawList* lst,
                                                UIState* state)
    {
        BuildColorPickerResponse resp{};
        // Process input.
        {
            auto window_vp = data->window.window_viewport();
            auto vp = colorpicker_window_content_viewports(data.get(), data->window.content_viewport(window_vp));
            if (mouse_in_viewport(state->mouse.ui_mouse, window_vp))
            {
                ID hot_widget = ID::Zero;
                if (mouse_in_viewport(state->mouse.ui_mouse, vp.hue_vp))
                {
                    hot_widget = data->hue_id;
                }
                else if (mouse_in_viewport(state->mouse.ui_mouse, vp.sv_vp))
                {
                    hot_widget = data->sv_id;
                }
                else if (mouse_in_viewport(state->mouse.ui_mouse, vp.alpha_vp))
                {
                    hot_widget = data->alpha_id;
                }
                else if (mouse_in_viewport(state->mouse.ui_mouse, vp.initial_color_vp))
                {
                    hot_widget = data->initial_color_id;
                }

                try_set_hot_widget(state, hot_widget);
                if (down(*state, MouseButton::L))
                {
                    try_set_focus_widget(state, hot_widget);
                }

                if (empty_focus_widget(*state))
                {
                    if (hot_widget_set(*state, hot_widget))
                    {
                        change_cursor(state, CursorStyle::Default);
                    }
                }
            }
            else
            {
                // If the mouse is clicked outside of the window and not dragging a color value, we want to close this window.
                if (down(*state, MouseButton::L) and empty_focus_widget(*state))
                {
                    try_set_focus_widget(state, ID::Sentinel);
                    resp.close = true;
                }
            }

            // Change colors.
            if (state->focus_widget == data->sv_id)
            {
                populate_sv_for_mouse_pos(data.get(), state->mouse.ui_mouse, vp.sv_vp);
                resp.color_change = true;
                resp.result_color = hsv_to_rgb(data->picker_color_hsv);
            }
            else if (state->focus_widget == data->hue_id)
            {
                populate_hue_for_mouse_pos(data.get(), state->mouse.ui_mouse, vp.hue_vp);
                resp.color_change = true;
                resp.result_color = hsv_to_rgb(data->picker_color_hsv);
            }
            else if (state->focus_widget == data->alpha_id)
            {
                populate_alpha_for_mouse_pos(data.get(), state->mouse.ui_mouse, vp.alpha_vp);
                resp.color_change = true;
                resp.result_color = hsv_to_rgb(data->picker_color_hsv);
            }
            // Note: This is only triggered on click, so we need the full event gambit.
            else if (state->focus_widget == data->initial_color_id
                    and state->hot_widget == data->initial_color_id
                    and not down(*state, MouseButton::L))
            {
                data->picker_color_hsv = rgb_to_hsv(data->initial_color_rgb);
                resp.color_change = true;
                resp.result_color = data->initial_color_rgb;
            }
        }
        // Render all widgets.
        {
            auto wind_resp = data->window.build(lst, data->atlas, state);
            resp.close |= wind_resp.close;
        }
        auto window_vp = data->window.window_viewport(); auto [hue_vp, alpha_vp, sv_vp, selected_vp, initial_color_vp, color_values_vp] = colorpicker_window_content_viewports(data.get(), data->window.content_viewport(window_vp));
        CmdBuffer::push_clip(lst, UI::convert(window_vp));

        Vec2f target_size{ Data::selected_color_size };
        Vec2f target_pos;
        const Vec4f picked_rgb = hsv_to_rgb(data->picker_color_hsv);
        const Vec4f hue_color_rgb = hsv_to_rgb({ data->picker_color_hsv.x, 1, 1, 1 });
        Vec2f size;
        // Picker.
        {
            CmdBuffer::push_clip(lst, UI::convert(sv_vp));
            size.x = rep(sv_vp.width) + 0.f;
            size.y = rep(sv_vp.height) + 0.f;
            CmdBuffer::start_shapes(lst, Render::VertShader::OneOneTransform);

            CmdBuffer::MultiColorInput in{};
            in.upper_left = hex_to_vec4f(0xFFFFFFFF);
            in.upper_right = hue_color_rgb;
            in.lower_left = in.upper_left;
            in.lower_right = hue_color_rgb;
            CmdBuffer::multi_color_solid_rect(lst, Render::FragShader::BasicColor, {}, size, in);
            // Black foreground.
            in.upper_left = hex_to_vec4f(0x00000000);
            in.upper_right = in.upper_left;
            in.lower_left = hex_to_vec4f(0x000000FF);
            in.lower_right = in.lower_left;
            CmdBuffer::multi_color_solid_rect(lst, Render::FragShader::BasicColor, {}, size, in);

            // Draw the target.
            target_pos.x = (data->picker_color_hsv.y * size.x) - Data::selected_color_size / 2.f;
            target_pos.y = (data->picker_color_hsv.z * size.y) - Data::selected_color_size / 2.f;
            CmdBuffer::strike_rect(lst, Render::FragShader::BasicColor, target_pos, target_size, Data::padding, hex_to_vec4f(0xFFFFFFFF));
            CmdBuffer::pop_clip(lst);
        }

        // Hue picker.
        {
            CmdBuffer::push_clip(lst, UI::convert(hue_vp));
            constexpr Vec4f col_hues[] =
            {
                hex_to_vec4f(0xFF0000FF),
                hex_to_vec4f(0xFFFF00FF),
                hex_to_vec4f(0x00FF00FF),
                hex_to_vec4f(0x00FFFFFF),
                hex_to_vec4f(0x0000FFFF),
                hex_to_vec4f(0xFF00FFFF),
                // This last entry is more of a sentinel than anything.
                hex_to_vec4f(0xFF0000FF),
            };

            size.x = rep(hue_vp.width) + 0.f;
            size.y = rep(hue_vp.height) + 0.f;
            CmdBuffer::start_shapes(lst, Render::VertShader::OneOneTransform);
            Vec2f slice_size = size;
            slice_size.x = slice_size.x / (std::size(col_hues) - 1);
            CmdBuffer::MultiColorInput in = {};
            Vec2f pos = {};
            for EachIndex(i, std::size(col_hues) - 1)
            {
                in.upper_left = in.lower_left = col_hues[i];
                in.upper_right = in.lower_right = col_hues[i + 1];
                CmdBuffer::multi_color_solid_rect(lst, Render::FragShader::BasicColor, pos, slice_size, in);
                pos.x += slice_size.x;
            }

            // Draw the target.
            target_pos.x = (data->picker_color_hsv.x * size.x) - Data::selected_color_size / 2.f;
            target_pos.y = 0;
            target_size.y = size.y;
            CmdBuffer::strike_rect(lst, Render::FragShader::BasicColor, target_pos, target_size, Data::padding, hex_to_vec4f(0xFFFFFFFF));
            CmdBuffer::pop_clip(lst);
        }

        // Alpha picker.
        {
            CmdBuffer::push_clip(lst, UI::convert(alpha_vp));
            size.x = rep(alpha_vp.width) + 0.f;
            size.y = rep(alpha_vp.height) + 0.f;
            auto non_alpha_color = picked_rgb;
            non_alpha_color.a = 1.f;
            CmdBuffer::start_shapes(lst, Render::VertShader::OneOneTransform);

            CmdBuffer::MultiColorInput in{};
            in.upper_left = non_alpha_color;
            in.upper_right = non_alpha_color;
            in.lower_left = Vec4f{ picked_rgb.x, picked_rgb.y, picked_rgb.z, 0.f };
            in.lower_right = in.lower_left;
            CmdBuffer::multi_color_solid_rect(lst, Render::FragShader::BasicColor, {}, size, in);

            // Draw the target.
            target_pos.y = (data->picker_color_hsv.a * size.y) - Data::selected_color_size / 2.f;
            target_pos.x = 0;
            target_size.x = size.x;
            target_size.y = Data::selected_color_size;
            CmdBuffer::strike_rect(lst, Render::FragShader::BasicColor, target_pos, target_size, Data::padding, hex_to_vec4f(0xFFFFFFFF));
            CmdBuffer::pop_clip(lst);
        }

        // Currently selected color.
        {
            CmdBuffer::push_clip(lst, UI::convert(selected_vp));
            size.x = rep(selected_vp.width) + 0.f;
            size.y = rep(selected_vp.height) + 0.f;
            CmdBuffer::start_shapes(lst, Render::VertShader::OneOneTransform);
            CmdBuffer::solid_rect(lst, Render::FragShader::BasicColor, {}, size, picked_rgb);
            CmdBuffer::pop_clip(lst);
        }

        // Color seed.
        {
            CmdBuffer::push_clip(lst, UI::convert(initial_color_vp));
            size.x = rep(initial_color_vp.width) + 0.f;
            size.y = rep(initial_color_vp.height) + 0.f;
            CmdBuffer::start_shapes(lst, Render::VertShader::OneOneTransform);
            CmdBuffer::solid_rect(lst, Render::FragShader::BasicColor, {}, size, data->initial_color_rgb);
            CmdBuffer::pop_clip(lst);
        }

        // Color values.
        build_color_values(data.get(), lst, color_values_vp, picked_rgb);

        data->window.end(state);

        CmdBuffer::pop_clip(lst);

        return resp;
    }
} // namespace UI::Widgets