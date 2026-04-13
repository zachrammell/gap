#include "basic-listbox.h"

#include <vector>

#include "config.h"

namespace UI::Widgets
{
    namespace
    {
        using EntryVector = std::vector<ListEntry>;

        void init_entries(EntryVector* entries, ID listbox_id)
        {
            EntryIndex idx{};
            for (auto& entry : *entries)
            {
                entry.id = make_id_seed_idx(listbox_id, rep(idx));
                entry.index = idx;
                entry.props = EntryProperties::None;

                idx = extend(idx);
            }
        }
    } // namespace [anon]

    struct BasicListbox::Data
    {
        static constexpr int entry_border_width = 2;

        ID id = ID::Zero;
        EntryVector entries;
        Height entry_height{ };
        Vec2f offset;
        Glyph::FontSize font_size = Glyph::FontSize{ 18 };
        EntryIndex selected = EntryIndex::Sentinel;
    };

    namespace
    {
        ListEntriesResult list_entries_in_view(BasicListbox::Data* data, const Render::RenderViewport& viewport)
        {
            // Start x,y.
            float start_x = 0.f;
            float start_y = rep(viewport.height) + fmodf(data->offset.y, rep(data->entry_height) + 0.f) - rep(data->entry_height);

            // Specific entry for the starting 'y' position.
            ListEntry* last = data->entries.data() + data->entries.size();
            ListEntry* first = last;
            if (not data->entries.empty())
            {
                auto idx = static_cast<size_t>(data->offset.y / rep(data->entry_height));
                if (idx < data->entries.size())
                {
                    first = data->entries.data() + idx;
                }
            }

            float entry_h = static_cast<float>(rep(data->entry_height));
            float entry_w = static_cast<float>(rep(viewport.width));

            return {
                .first = first,
                .last = last,
                .start_xy = { start_x, start_y },
                .entry_size = { entry_w, entry_h},
                .padding = BasicListbox::Data::entry_border_width
            };
        }

        Vec2f offset_for_entry(BasicListbox::Data* data, EntryIndex index)
        {
            Vec2f offset;
            offset.y = static_cast<float>(rep(data->entry_height) * rep(index)) - static_cast<float>(rep(data->entry_height));
            return offset;
        }

        AABBData box_for_entry(BasicListbox::Data* data, EntryIndex index, const Render::RenderViewport& viewport)
        {
            Vec2f top = offset_for_entry(data, index);
            top.y += rep(data->entry_height);
            Vec2f size{ rep(viewport.width) + 0.f, rep(data->entry_height) + 0.f };
            return { .pos = top, .size = size };
        }
    } // namespace [anon]

    BasicListbox::BasicListbox(ID parent_id):
        data{ new Data{ .id = make_id_seed(parent_id, "listbox") } } { }

    BasicListbox::~BasicListbox() = default;

    // Interaction.
    void BasicListbox::entry_count(EntryCount count)
    {
        data->entries.resize(rep(count));
        init_entries(&data->entries, data->id);
        data->selected = EntryIndex::Sentinel;
    }

    void BasicListbox::entry_height(Height height)
    {
        data->entry_height = height;
    }

    void BasicListbox::offset(const Vec2f& offset)
    {
        data->offset = offset;
    }

    void BasicListbox::up(const Render::RenderViewport& viewport)
    {
        if (data->selected == EntryIndex::Sentinel)
        {
            auto result = ::UI::Widgets::list_entries_in_view(data.get(), viewport);
            if (result.first != result.last)
            {
                data->selected = EntryIndex(result.first - data->entries.data());
            }
            else
            {
                // There are either no entries or nothing in view to go off of.
                return;
            }
        }
        else if (rep(data->selected) != 0)
        {
            data->selected = retract(data->selected);
        }
    }

    void BasicListbox::down(const Render::RenderViewport& viewport)
    {
        if (data->selected == EntryIndex::Sentinel)
        {
            auto result = ::UI::Widgets::list_entries_in_view(data.get(), viewport);
            if (result.first != result.last)
            {
                data->selected = EntryIndex(result.first - data->entries.data());
            }
            else
            {
                // There are either no entries or nothing in view to go off of.
                return;
            }
        }
        else if (rep(data->selected) + 1 < data->entries.size())
        {
            data->selected = extend(data->selected);
        }
    }

    void BasicListbox::select(EntryIndex index)
    {
        if (rep(index) < data->entries.size())
        {
            data->selected = index;
        }
    }

    // Queries.
    EntryCount BasicListbox::entry_count() const
    {
        return EntryCount(data->entries.size());
    }

    Height BasicListbox::entry_height() const
    {
        return data->entry_height;
    }

    Vec2f BasicListbox::content_size() const
    {
        Vec2f size;
        size.y = static_cast<float>(rep(data->entry_height) * data->entries.size());
        return size;
    }

    EntryIndex BasicListbox::selected() const
    {
        return data->selected;
    }

    AABBData BasicListbox::box_for_selected(const Render::RenderViewport& viewport) const
    {
        if (selected() == EntryIndex::Sentinel)
            return {};
        return box_for_entry(data.get(), selected(), viewport);
    }

    BuildListboxResponse BasicListbox::build(CmdBuffer::DrawList* lst, UIState* state)
    {
        BuildListboxResponse resp{};
        const auto clip = CmdBuffer::current_clip(*lst);
        resp.view = list_entries_in_view(data.get(), convert(clip));
        // Process input.
        {
            if (mouse_in_clip(state->mouse.ui_mouse, clip))
            {
                ID hot_widget = data->id;
                auto adjusted_mouse = adjusted_mouse_for_clip(state->mouse.ui_mouse, clip);
                // Adjust the mouse 'y' since the mouse coords start at the top-left.
                adjusted_mouse.y = rep(clip.height) - adjusted_mouse.y;
                auto list = resp.view;
                for (; list.first != list.last; ++list.first)
                {
                    auto box = box_for_entry(data.get(), list.first->index, convert(clip));
                    // Adjust the box for the current offset.
                    box.pos = box.pos - data->offset;

                    if (box.pos.y > rep(clip.height))
                        break;

                    if (basic_aabb(box, adjusted_mouse))
                    {
                        hot_widget = list.first->id;
                        break;
                    }
                }

                try_set_hot_widget(state, hot_widget);
                if (UI::down(*state, MouseButton::L))
                {
                    try_set_focus_widget(state, hot_widget);
                }

                if (empty_focus_widget(*state))
                {
                    if (state->hot_widget == hot_widget)
                    {
                        change_cursor(state, CursorStyle::Default);
                    }
                }
            }
        }
        const auto& colors = Config::widget_colors();

        ListEntriesResult result = ::UI::Widgets::list_entries_in_view(data.get(), convert(clip));

        CmdBuffer::start_shapes(lst, Render::VertShader::OneOneTransform);
        ListEntry* first = result.first;
        ListEntry* last = result.last;
        Vec2f size{ rep(clip.width) + 0.f, rep(data->entry_height) + 0.f };
        Vec2f pos = result.start_xy;
        const bool lmouse_down = UI::down(*state, MouseButton::L);
        for (; first != last; ++first)
        {
            if (first->index == data->selected
                or first->id == state->hot_widget)
            {
                // Test to see if the mouse was clicked and this widget was active.  If so, we want to send
                // the caller the info.
                if (state->focus_widget == first->id and not lmouse_down)
                {
                    resp.select = true;
                    resp.selected = first->index;
                    data->selected = first->index;
                }
                CmdBuffer::solid_rect(lst, Render::FragShader::BasicColor, pos, size, colors.window_border);
            }
            else
            {
                CmdBuffer::strike_rect(lst, Render::FragShader::BasicColor, pos, size, Data::entry_border_width, colors.window_border);
            }
            pos.y -= size.y;

            if (pos.y < -size.y)
                break;
        }
        return resp;
    }
} // namespace UI::Widgets