#pragma once

#include <memory>

#include "renderer.h"
#include "ui-common.h"
#include "vec.h"

namespace UI::Widgets
{
    enum class BuildScrollBoxFlags
    {
        None       = 0,
        DrawBorder = 1U << 0,
    };

    struct BuildScrollBoxResponse
    {
        bool scroll_changed = false;
        bool scroll_dragged = false;
    };

    struct IndexedScrollOffset
    {
        int64_t idx;
        Vec2f offset;
    };

    struct IndexedScrollContentSize
    {
        int64_t v_size;
        Vec2f entry_size;
    };

    struct ScrollBox
    {
    public:
        struct Data;

        ScrollBox(ID content_id);
        ~ScrollBox();

        // Setup.
        void content_size(const Vec2f& size);
        void content_size(const Vec2f& size, const Render::RenderViewport& viewport);
        void sync_config();

        // Queries for enclosed content.
        Vec2f position() const;
        Render::RenderViewport content_viewport(const Render::RenderViewport& viewport) const;
        const Vec2f& content_size() const;
        bool at_end_y() const;
        bool at_end_x() const;

        // Direct interaction.
        void scroll_to(float offset);
        void scroll_to_no_smooth_scroll(float offset);
        void make_box_viewable(const AABBData& box, const Render::RenderViewport& viewport);
        void scroll_to_end_y();
        void scroll_to_end_x();

        // Building.
        BuildScrollBoxResponse build(CmdBuffer::DrawList* lst,
                                        UIState* state,
                                        float scroll_amount,
                                        BuildScrollBoxFlags flags);
    private:
        std::unique_ptr<Data> data;
    };

    struct IndexedScrollBox
    {
    public:
        struct Data;

        IndexedScrollBox(ID content_id);
        ~IndexedScrollBox();

        // Setup.
        void content_size(const IndexedScrollContentSize& size);
        void sync_config();

        // Queries for enclosed content.
        IndexedScrollOffset position() const;
        Render::RenderViewport content_viewport(const Render::RenderViewport& viewport) const;
        const IndexedScrollContentSize& content_size() const;
        uint64_t indices_per_view(const Render::RenderViewport& viewport) const;

        // Direct interaction.
        void scroll_to(IndexedScrollOffset offset);
        void scroll_to_no_smooth_scroll(IndexedScrollOffset offset);
        void make_index_viewable(int64_t idx, Vec2f x_pos_size, const Render::RenderViewport& viewport);

        // Building.
        BuildScrollBoxResponse build(CmdBuffer::DrawList* lst,
                                        UIState* state,
                                        float scroll_amount,
                                        BuildScrollBoxFlags flags);
    private:
        std::unique_ptr<Data> data;
    };
} // namespace UI::Widgets