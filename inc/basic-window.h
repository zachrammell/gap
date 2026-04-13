#pragma once

#include <memory>
#include <string_view>

#include "glyph-cache.h"
#include "renderer.h"
#include "types.h"
#include "ui-common.h"

namespace UI::Widgets
{
    enum class ShowWindowOptions : uint8_t
    {
        None         = 0,
        HideTitlebar = 1U << 0,
    };

    struct ShowWindowData
    {
        Render::RenderViewport initial_viewport;
        Vec2f expand_point; // % Range [0,1] starting from bottom-right.
        ShowWindowOptions options = ShowWindowOptions::None;
    };

    struct BuildWindowResponse
    {
        bool close = false;
    };

    class BasicWindow
    {
    public:
        struct Data;

        BasicWindow(ID id);
        ~BasicWindow();

        // Setup.
        void title(std::string_view s);
        void background_alpha(float a);
        void sync_config(Glyph::Atlas* atlas);
        void show(const ShowWindowData& in);

        // Window viewport manipulation/queries.
        Render::RenderViewport window_viewport() const;
        void window_viewport(const Render::RenderViewport& viewport);
        // The window viewport with no anmiation offsets applied.
        Render::RenderViewport final_viewport() const;
        bool animating() const;
        // Queries for enclosed content.
        Render::RenderViewport content_viewport(const Render::RenderViewport& viewport) const;
        // Retrieves the window dimensions such that there is exactly 1 pixel of viewable content.
        Height min_viewable_window_content_height() const;
        float horiz_padding() const;

        // General queries.
        ID id() const;

        // Building.
        BuildWindowResponse build(CmdBuffer::DrawList* lst, Glyph::Atlas* atlas, UIState* state);
        // Completes the UI for the window.
        void end(UIState* state);
    private:
        std::unique_ptr<Data> data;
    };

    // Note: 'point' should be in adjusted viewport coords.
    constexpr ShowWindowData spawn_window_at_point(const ScreenDimensions& screen, const Render::RenderViewport& initial_vp, const Vec2i& point)
    {
        ShowWindowData show_data{
            .initial_viewport = initial_vp,
            .expand_point = { 0.f, 1.f } // Spawn expanding down and to the right.
        };

        show_data.initial_viewport.offset_x = Render::ViewportOffsetX{ point.x };
        show_data.initial_viewport.offset_y = Render::ViewportOffsetY{ point.y - rep(initial_vp.height) };
        // See if we can spawn the window at the x coord.
        if (point.x + rep(initial_vp.width) > rep(screen.width))
        {
            show_data.initial_viewport.offset_x = Render::ViewportOffsetX{ rep(screen.width) - rep(initial_vp.width) };
            show_data.expand_point.x = (static_cast<float>(point.x) - rep(show_data.initial_viewport.offset_x)) / rep(initial_vp.width);
        }

        // See if we can spawn the window at the y coord.
        if (point.y - rep(initial_vp.height) < 0)
        {
            show_data.initial_viewport.offset_y = Render::ViewportOffsetY{ };
            show_data.expand_point.y = static_cast<float>(point.y) / rep(initial_vp.height);
        }

        return show_data;
    }
} // namespace UI::Widgets