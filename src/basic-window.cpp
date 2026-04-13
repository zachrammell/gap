#include "basic-window.h"

#include <string>

#include "cmd-buffer.h"
#include "config.h"
#include "os.h"
#include "util.h"

namespace UI::Widgets
{
    namespace
    {
        enum class Resizing
        {
            None,
            Bottom,
            Left,
            Right,
            BottomLeft,
            BottomRight,
            Count
        };

        constexpr std::string_view resizing_to_string(Resizing s)
        {
            using namespace std::literals;
            switch (s)
            {
            case Resizing::None:
                return "None"sv;
            case Resizing::Bottom:
                return "Bottom"sv;
            case Resizing::Left:
                return "Left"sv;
            case Resizing::Right:
                return "Right"sv;
            case Resizing::BottomLeft:
                return "BottomLeft"sv;
            case Resizing::BottomRight:
                return "BottomRight"sv;
            }
            return "";
        }

        CursorStyle resize_to_cursor(Resizing area)
        {
            switch (area)
            {
            case Resizing::Bottom:
                return CursorStyle::UpDownArrow;
            case Resizing::Left:
            case Resizing::Right:
                return CursorStyle::LeftRightArrow;
            case Resizing::BottomLeft:
                return CursorStyle::SouthWestArrow;
            case Resizing::BottomRight:
                return CursorStyle::SouthEastArrow;
            }
            return CursorStyle::Default;
        }
    } // namespace [anon]

    struct BasicWindow::Data
    {
        static constexpr int padding = 4;

        ID id = ID::Zero;
        ID resize_bar_id = ID::Zero;
        ID title_bar_id = ID::Zero;
        ID close_button_id = ID::Zero;
        ID resize_edge_id[count_of<Resizing>]{};
        std::string title;
        Render::RenderViewport window_vp{};
        Vec2f expand_point_bottom_left;
        Vec2f window_ease_in_offset{};
        Vec2f button_size;
        float titlebar_height = 20;
        Glyph::FontSize font_size = Glyph::FontSize{ 14 };
        float background_alpha = 1.f;
        bool hide_titlebar = false;
    };

    namespace
    {
        struct TitlebarRect
        {
            Vec2f pos;
            Vec2f size;
        };

        TitlebarRect titlebar_box(const BasicWindow::Data& data, const Render::RenderViewport& viewport)
        {
            // Note: the viewport height is added to content size height because the viewport goes from y(0) to y(content.height).
            Vec2f left{ 0.f, rep(viewport.height) - data.titlebar_height };
            Vec2f size{ rep(viewport.width) + 0.f, data.titlebar_height };
            return { .pos = left, .size = size };
        }

        struct CloseButtonRect
        {
            Vec2f pos;
            Vec2f size;
        };

        CloseButtonRect close_button_box(const BasicWindow::Data& data, const Render::RenderViewport& viewport)
        {
            Vec2f left{ rep(viewport.width) - data.button_size.x, rep(viewport.height) - data.button_size.y };
            Vec2f size = data.button_size;
            return { .pos = left, .size = size };
        }

        Resizing resizing_edge(const Vec2i& mouse_pos, const Render::RenderViewport& viewport)
        {
            auto adjusted_mouse = adjusted_mouse_for_viewport(mouse_pos, viewport);
            if (adjusted_mouse.y <= BasicWindow::Data::padding)
            {
                if (adjusted_mouse.x <= BasicWindow::Data::padding)
                    return Resizing::BottomLeft;
                if ((rep(viewport.width) - adjusted_mouse.x) <= BasicWindow::Data::padding)
                    return Resizing::BottomRight;
                return Resizing::Bottom;
            }
            if (adjusted_mouse.x <= BasicWindow::Data::padding)
                return Resizing::Left;
            if ((rep(viewport.width) - adjusted_mouse.x) <= BasicWindow::Data::padding)
                return Resizing::Right;
            return Resizing::None;
        }

        Render::RenderViewport mouse_move_resize(const UIState& state, Resizing edge, const Render::RenderViewport& old)
        {
            auto offset = state.mouse.ui_mouse - state.mouse.ui_prev_mouse;
            auto new_viewport = old;
            switch (edge)
            {
            case Resizing::None:
                break;
            case Resizing::Bottom:
                // Adjust both the viewport height and offset by the same amount.
                new_viewport.height = Height{ rep(new_viewport.height) - offset.y };
                new_viewport.offset_y = Render::ViewportOffsetY{ rep(new_viewport.offset_y) + offset.y };
                break;
            case Resizing::Left:
                // Adjust both the viewport height and offset by the same amount.
                new_viewport.width = Width{ rep(new_viewport.width) - offset.x };
                new_viewport.offset_x = Render::ViewportOffsetX{ rep(new_viewport.offset_x) + offset.x };
                break;
            case Resizing::Right:
                // Adjust both the viewport height and offset by the same amount.
                new_viewport.width = Width{ rep(new_viewport.width) + offset.x };
                break;
            case Resizing::BottomLeft:
                // Adjust both the viewport height and offset by the same amount.
                new_viewport.height = Height{ rep(new_viewport.height) - offset.y };
                new_viewport.offset_y = Render::ViewportOffsetY{ rep(new_viewport.offset_y) + offset.y };
                // Adjust both the viewport height and offset by the same amount.
                new_viewport.width = Width{ rep(new_viewport.width) - offset.x };
                new_viewport.offset_x = Render::ViewportOffsetX{ rep(new_viewport.offset_x) + offset.x };
                break;
            case Resizing::BottomRight:
                // Adjust both the viewport height and offset by the same amount.
                new_viewport.height = Height{ rep(new_viewport.height) - offset.y };
                new_viewport.offset_y = Render::ViewportOffsetY{ rep(new_viewport.offset_y) + offset.y };
                // Adjust both the viewport height and offset by the same amount.
                new_viewport.width = Width{ rep(new_viewport.width) + offset.x };
                break;
            }
            return new_viewport;
        }

        Height window_height_offset_for_content(const BasicWindow::Data& data)
        {
            // Note: we remove 2 from padding here due to rendering 'up' in screen space.  We lose one pixel from the window border
            // on the bottom and then one more due to 'padding' being where we _start_ rednering.  However, we remove half pixel from the
            // top because the top of the box does not have this issue we end having this inconsistency.
            constexpr int render_start_offset = 2;
            return Height{ BasicWindow::Data::padding * 2 + static_cast<int>(data.titlebar_height) - render_start_offset };
        }

        Render::RenderViewport window_viewport(BasicWindow::Data* data)
        {
            auto dest_vp = data->window_vp;

            int expand_x = static_cast<int>(data->window_ease_in_offset.x);
            int move_x = static_cast<int>(data->window_ease_in_offset.x * data->expand_point_bottom_left.x);
            int expand_y = static_cast<int>(data->window_ease_in_offset.y);
            int move_y = static_cast<int>(data->window_ease_in_offset.y * data->expand_point_bottom_left.y);

            dest_vp.width = Width{ rep(dest_vp.width) - expand_x };
            dest_vp.offset_x = Render::ViewportOffsetX{ rep(dest_vp.offset_x) + move_x };
            dest_vp.height = Height{ rep(dest_vp.height) - expand_y };
            dest_vp.offset_y = Render::ViewportOffsetY{ rep(dest_vp.offset_y) + move_y };

            return dest_vp;
        }
    } // namespace [anon]

    BasicWindow::BasicWindow(ID id):
        data{ new Data{ .id = id,
                        .resize_bar_id = make_id_seed(id, "resize"),
                        .title_bar_id = make_id_seed(id, "title"),
                        .close_button_id = make_id_seed(id, "close") } }
    {
        // Fill in resize edge IDs.
        for (int i = 0; i < count_of<Resizing>; ++i)
        {
            data->resize_edge_id[i] = make_id_seed(id, resizing_to_string(Resizing{ i }));
        }
    }

    BasicWindow::~BasicWindow() = default;

    // Setup.
    void BasicWindow::title(std::string_view s)
    {
        data->title = s;
    }

    void BasicWindow::background_alpha(float a)
    {
        data->background_alpha = a;
    }

    void BasicWindow::sync_config(Glyph::Atlas* atlas)
    {
        data->font_size = Glyph::FontSize{ Config::widget_state().window_title_font_size };
        auto font_ctx = atlas->render_font_context(data->font_size);

        if (not data->hide_titlebar)
        {
            data->titlebar_height = font_ctx.current_font_line_height() + Data::padding * 2.f;
        }
    }

    void BasicWindow::show(const ShowWindowData& in)
    {
        data->window_vp = in.initial_viewport;
        data->window_ease_in_offset.x = rep(data->window_vp.width) + 0.f;
        data->window_ease_in_offset.y = rep(data->window_vp.height) + 0.f;
        data->expand_point_bottom_left = in.expand_point;
        data->hide_titlebar = implies(in.options, ShowWindowOptions::HideTitlebar);
        if (data->hide_titlebar)
        {
            data->titlebar_height = 0.f;
        }
    }

    // Window viewport manipulation/queries.
    Render::RenderViewport BasicWindow::window_viewport() const
    {
        return Widgets::window_viewport(data.get());
    }

    void BasicWindow::window_viewport(const Render::RenderViewport& viewport)
    {
        data->window_vp = viewport;
    }

    Render::RenderViewport BasicWindow::final_viewport() const
    {
        return data->window_vp;
    }

    bool BasicWindow::animating() const
    {
        return data->window_ease_in_offset != 0.f;
    }

    // Queries for enclosed content.
    Render::RenderViewport BasicWindow::content_viewport(const Render::RenderViewport& viewport) const
    {
        // reduce the margins a bit.
        auto new_viewport = viewport;
        new_viewport.width = Width{ rep(viewport.width) - Data::padding * 2 };
        new_viewport.height = Height{ rep(viewport.height) - rep(window_height_offset_for_content(*data)) };
        new_viewport.offset_x = Render::ViewportOffsetX{ rep(viewport.offset_x) + Data::padding };
        new_viewport.offset_y = Render::ViewportOffsetY{ rep(viewport.offset_y) + Data::padding };
        return new_viewport;
    }

    Height BasicWindow::min_viewable_window_content_height() const
    {
        return window_height_offset_for_content(*data);
    }

    float BasicWindow::horiz_padding() const
    {
        return Data::padding;
    }

    // General queries.
    ID BasicWindow::id() const
    {
        return data->id;
    }

    BuildWindowResponse BasicWindow::build(CmdBuffer::DrawList* lst, Glyph::Atlas* atlas, UIState* state)
    {
        BuildWindowResponse resp{};
        // Process input.
        {
            auto window_vp = window_viewport();
            auto titlebar_rect = titlebar_box(*data, window_vp);
            if (mouse_in_viewport(state->mouse.ui_mouse, window_vp))
            {
                auto adjusted_mouse = adjusted_mouse_for_viewport(state->mouse.ui_mouse, window_vp);
                ID hot_widget = ID::Zero;
                if (basic_aabb({ .pos = titlebar_rect.pos, .size = titlebar_rect.size }, adjusted_mouse))
                {
                    hot_widget = data->title_bar_id;
                    // Test if the button is hovered.
                    auto close_button = close_button_box(*data, window_vp);
                    if (basic_aabb({ .pos = close_button.pos, .size = close_button.size }, adjusted_mouse))
                    {
                        hot_widget = data->close_button_id;
                    }
                }

                auto edge = resizing_edge(state->mouse.ui_mouse, window_vp);
                if (edge != Resizing::None)
                {
                    hot_widget = data->resize_edge_id[rep(edge)];
                    // Change the cursor.
                    if (empty_focus_widget(*state))
                    {
                        auto cursor = resize_to_cursor(edge);
                        change_cursor(state, cursor);
                    }
                }
                else if (empty_focus_widget(*state)
                            // If we're about to set the hot widget to something we computed above,
                            // Set the cursor appropriately as well.
                            and hot_widget != ID::Zero
                            and state->hot_widget == ID::Zero)
                {
                    change_cursor(state, CursorStyle::Default);
                }

                try_set_hot_widget(state, hot_widget);
                if (down(*state, MouseButton::L))
                {
                    try_set_focus_widget(state, hot_widget);
                    if (state->focus_widget == hot_widget)
                    {
                        set_focus_window(state, data->id);
                    }
                }
            }

            // Resize.
            for (int i = 0; i < count_of<Resizing>; ++i)
            {
                if (state->focus_widget == data->resize_edge_id[i])
                {
                    auto new_vp = mouse_move_resize(*state, Resizing{ i }, window_vp);
                    // Clamp the viewport.
                    data->window_vp.height = std::max(Height{ static_cast<int>(titlebar_rect.size.y + 10.f) }, new_vp.height);
                    data->window_vp.width = std::max(Width{ static_cast<int>(data->button_size.x + 10.f) }, new_vp.width);
                    // If the height got clamped, we do not want to adjusted the y-offset otherwise the window will move.
                    if (data->window_vp.height == new_vp.height)
                    {
                        data->window_vp.offset_y = new_vp.offset_y;
                    }
                    // Similarly for the x-offset.
                    if (data->window_vp.width == new_vp.width)
                    {
                        data->window_vp.offset_x = new_vp.offset_x;
                    }
                }
            }

            // Moving.
            if (state->focus_widget == data->title_bar_id)
            {
                auto offset = mouse_move_drag(*state);
                offset_viewport(&data->window_vp, offset);
            }

            // Closing.
            if (state->focus_widget == data->close_button_id
                and state->hot_widget == data->close_button_id
                and not down(*state, MouseButton::L))
            {
                resp.close = true;
            }
        }

        auto window_vp = convert(window_viewport());
        // Blur the background for some flare.
        CmdBuffer::standard_window_blur(lst, window_vp);

        CmdBuffer::push_clip(lst, window_vp);

        const auto& colors = Config::widget_colors();
        // Basic window rect.
        CmdBuffer::start_shapes(lst, Render::VertShader::OneOneTransform);
        {
            Vec2f left{ 0.f, 0.f };
            Vec2f size{ rep(window_vp.width) + 0.f, rep(window_vp.height) + 0.f };
            // First lets clear the rect.
            auto bg_color = Config::diff_colors().background;
            bg_color.a = data->background_alpha;
            CmdBuffer::solid_rect(lst, Render::FragShader::BasicColor, left, size, bg_color);
            // Now strike it with the color we want.
            CmdBuffer::strike_rect(lst, Render::FragShader::BasicColor, left, size, 2.f, colors.window_border);
        }

        // Window title bar.
        if (not data->hide_titlebar)
        {
            const float title_bar_start_y = rep(window_vp.height) - data->titlebar_height;
            data->button_size = data->titlebar_height;

            Vec2f left{ 0.f, title_bar_start_y };
            Vec2f size{ rep(window_vp.width) + 0.f, data->titlebar_height };
            CmdBuffer::solid_rect(lst, Render::FragShader::BasicColor, left, size, colors.window_title_background);
            // Render the close button hover if necessary.
            if (state->hot_widget == data->close_button_id)
            {
                // Only Hovered.
                if (state->focus_widget == ID::Zero)
                {
                    // Reuse the shader from above.
                    auto close_button_rect = close_button_box(*data, convert(window_vp));
                    CmdBuffer::solid_rect(lst, Render::FragShader::BasicColor, close_button_rect.pos, close_button_rect.size, colors.window_close_button_hover);
                }

                // Pressed.
                if (state->focus_widget == data->close_button_id)
                {
                    // Reuse the shader from above.
                    auto close_button_rect = close_button_box(*data, convert(window_vp));
                    CmdBuffer::solid_rect(lst, Render::FragShader::BasicColor, close_button_rect.pos, close_button_rect.size, colors.window_close_button_pressed);
                }
            }

            auto font_ctx = atlas->render_font_context(data->font_size);
            CmdBuffer::start_glyph_run(lst, Render::VertShader::OneOneTransform);
            // Name.
            Vec2f pos{ Data::padding, 0.f };
            // Center the name.
            pos.y = title_bar_start_y + (data->titlebar_height - rep(data->font_size)) / 2.f;
            font_ctx.render_text(lst, data->title, pos, colors.window_title_font_color);

            // 'X' button.
            auto glyph_size = font_ctx.icon_glyph_size(Glyph::SpecialGlyph::X);
            pos.x = (rep(window_vp.width) - data->button_size.x) + (data->button_size.x - glyph_size.x) / 2.f;
            pos.y = title_bar_start_y + (data->titlebar_height + glyph_size.y) / 2.f;
            font_ctx.render_icon_glyph_no_offsets(lst, Glyph::SpecialGlyph::X, pos, colors.window_title_font_color);
        }

        // Move offset forward.
        data->window_ease_in_offset = apply_anim(data->window_ease_in_offset, state->anim_fast_rate);

        // Continue showing frames until animation is complete.
        if (data->window_ease_in_offset != 0.f)
        {
            Render::request_frames();
        }

        // End clipping region.
        CmdBuffer::pop_clip(lst);

        return resp;
    }

    // Completes the UI for the window.
    void BasicWindow::end(UIState* state)
    {
        auto window_vp = window_viewport();
        if (mouse_in_viewport(state->mouse.ui_mouse, window_vp)
            and state->hot_widget == ID::Zero
            and empty_focus_widget(*state))
        {
            try_set_hot_widget(state, data->id);
            if (any_mouse_down(*state))
            {
                try_set_focus_widget(state, data->id);
            }
        }
    }
} // namespace UI::Widgets