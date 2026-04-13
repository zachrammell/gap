#pragma once

#include <memory>
#include <string_view>

#include "basic-window.h"
#include "glyph-cache.h"
#include "renderer.h"
#include "ui-common.h"

namespace UI::Widgets
{
    struct BuildColorPickerResponse
    {
        Vec4f result_color;
        bool color_change = false;
        bool close = false;
    };

    class ColorPicker
    {
    public:
        struct Data;

        ColorPicker(Glyph::Atlas* atlas, ID id);
        ~ColorPicker();

        // Interaction.
        void start(const ScreenDimensions& screen, const Vec2i& mouse_pos, const Vec4f& color, UIState* state);
        void sync_config();

        // Queries.
        ID id() const;

        // Building.
        BuildColorPickerResponse build(CmdBuffer::DrawList* lst,
                                        UIState* state);
    private:
        std::unique_ptr<Data> data;
    };
} // namespace UI::Widgets