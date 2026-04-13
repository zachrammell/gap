#pragma once

#include <memory>

#include "glyph-cache.h"
#include "renderer.h"
#include "ui-common.h"
#include "vec.h"

namespace UI::Widgets
{
    enum class EntryCount : uint32_t { };

    enum class EntryIndex : uint32_t
    {
        Sentinel = sentinel_for<EntryIndex>
    };

    enum class EntryProperties : uint32_t
    {
        None     = 0,
        Selected = 1u << 0,
        Hovered  = 1u << 1,
        Hidden   = 1u << 2,
    };

    struct ListEntry
    {
        ID id = ID::Zero;
        EntryIndex index;
        EntryProperties props;
    };

    struct ListEntriesResult
    {
        ListEntry* first;
        ListEntry* last;
        Vec2f start_xy;
        Vec2f entry_size;
        Vec2f padding;
    };

    struct BuildListboxResponse
    {
        ListEntriesResult view{};
        EntryIndex selected = EntryIndex::Sentinel;
        bool select = false;
    };

    class BasicListbox
    {
    public:
        struct Data;

        BasicListbox(ID parent_id);
        ~BasicListbox();

        // Interaction.
        void entry_count(EntryCount count);
        void entry_height(Height height);
        void offset(const Vec2f& offset);
        void up(const Render::RenderViewport& viewport);
        void down(const Render::RenderViewport& viewport);
        void select(EntryIndex index);

        // Queries.
        EntryCount entry_count() const;
        Height entry_height() const;
        Vec2f content_size() const;
        EntryIndex selected() const;
        AABBData box_for_selected(const Render::RenderViewport& viewport) const;

        // Building.
        BuildListboxResponse build(CmdBuffer::DrawList* lst, UIState* state);
    private:
        std::unique_ptr<Data> data;
    };
} // namespace UI::Widgets