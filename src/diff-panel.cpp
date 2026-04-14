#include "diff-panel.h"

#include <cassert>

#include "diff-text.h"
#include "gap-core.h"

namespace Diff
{
    namespace
    {
        struct PartitionPanel
        {
            static constexpr float padding = 2.f;

            PartitionPanel* sib_next;
            PartitionPanel* sib_prev;
            CmdBuffer::DrawList* draw_lst;
            UI::Widgets::ID id;
            DiffTextView* view;
            float pct_of_parent;
            float ease_offset;
        };

        read_only PartitionPanel null_panel_inst = {};

        PartitionPanel* null_panel()
        {
            return &null_panel_inst;
        }

        bool null_panel(PartitionPanel* panel)
        {
            return panel == &null_panel_inst;
        }

        CmdBuffer::ClipRect clip_from_parent(CmdBuffer::ClipRect parent_clip, PartitionPanel* first, PartitionPanel* target)
        {
            Vec4f clip = UI::clip_as_vec(parent_clip);
            Vec2f parent_size{ rep(parent_clip.width) + 0.f, rep(parent_clip.height) + 0.f };
            // Make the width the same as the start offset (so we can sum widths based on %).
            clip.p1[0] = clip.p0[0];
            // Note: We only layout on one axis so this loop is simplified.
            for (;not null_panel(first); first = first->sib_next)
            {
                clip.p1[0] += parent_size.xy[0] * first->pct_of_parent;
                if (first == target)
                    break;
                clip.p0[0] = clip.p1[0];
            }
            return UI::vec_as_clip(clip);
        }

        void init_panel(PartitionPanel* panel, UI::Widgets::ID seed_id, uint32_t seed_idx, Glyph::Atlas* atlas)
        {
            panel->id = UI::Widgets::make_id_seed_idx(seed_id, seed_idx);
            panel->draw_lst = CmdBuffer::alloc_draw_list();
            panel->ease_offset = 1.f;
            panel->pct_of_parent = .5f;
            panel->sib_next = panel->sib_prev = null_panel();
            // Allocate the diff text view.
            panel->view = make_diff_text_view(atlas, UI::Widgets::make_id_seed(panel->id, "txt_view"));
        }
    } // namespace [anon]

    struct DiffPanel
    {
        Arena::Arena* arena;
        Glyph::Atlas* atlas;
        CmdBuffer::DrawList* frame_lst;
        UI::Widgets::ID id;
        PartitionPanel A;
        PartitionPanel B;
    };

    // Creation.
    DiffPanel* make_diff_panel(Glyph::Atlas* atlas)
    {
        Arena::Arena* arena = Arena::alloc(Arena::default_params);
        DiffPanel* panel = Arena::push_array<DiffPanel>(arena, 1);
        panel->arena = arena;
        panel->atlas = atlas;
        panel->frame_lst = CmdBuffer::alloc_draw_list();
        panel->id = UI::Widgets::ID::DiffPanel;
        init_panel(&panel->A, panel->id, 0, atlas);
        init_panel(&panel->B, panel->id, 1, atlas);
        // Connect A and B.
        panel->A.sib_next = &panel->B;
        panel->B.sib_prev = &panel->A;
        return panel;
    }

    // Cleanup.
    void release_diff_panel(DiffPanel* panel)
    {
        for (PartitionPanel* child = &panel->A;
            not null_panel(child);
            child = child->sib_next)
        {
            release_diff_text_view(child->view);
        }
        CmdBuffer::release_draw_list(panel->frame_lst);
        CmdBuffer::release_draw_list(panel->A.draw_lst);
        CmdBuffer::release_draw_list(panel->B.draw_lst);
        Arena::Arena* arena = panel->arena;
        Arena::release(arena);
    }

    // Interaction.
    void file_A(DiffPanel* panel, const TextFile& file)
    {
        populate_text(panel->A.view, file);
    }

    void file_B(DiffPanel* panel, const TextFile& file)
    {
        populate_text(panel->B.view, file);
    }

    MergedLineNode* push_merge_line(Arena::Arena* arena, MergedLineList* lst, MergedLine line)
    {
        MergedLineNode* node = Arena::push_array<MergedLineNode>(arena, 1);
        node->line = line;
        SLLQueuePush(lst->first, lst->last, node);
        ++lst->count;
        return node;
    }

    void apply_diff(DiffPanel* panel)
    {
        auto scratch = Arena::scratch_begin(Arena::no_conflicts);
        TextFile* a = text_file(panel->A.view);
        TextFile* b = text_file(panel->B.view);
        EditList edits = diff_file_lines(scratch.arena, *a, *b);
        // What we want is a sequence of 'lines' for both A and B which
        // represent the 'merged' files together.  We will merge deletes
        // and inserts and create 'gap' lines which will be rendered as
        // an empty region in the text view.
        MergedLineList lst_A = {};
        MergedLineList lst_B = {};
        MergedLineList lst_merge_B = {}; // This is to have as a write-back for the B list.
        for EachNode(e, edits.first)
        {
            switch (e->edit.type)
            {
            case EditType::Del:
                {
                    // Add delete from A.
                    LineRange rng_a = text_file_line_range(*a, Editor::CursorLine(e->edit.idx_a));
                    MergedLine line_a = {
                        .first = rng_a.first,
                        .last = rng_a.last,
                        .type = EditType::Del,
                    };
                    push_merge_line(scratch.arena, &lst_A, line_a);
                    MergedLine line_b = {
                        .first = Editor::CharOffset::Sentinel,
                        .last = Editor::CharOffset::Sentinel,
                        .type = EditType::Invalid,
                    };
                    push_merge_line(scratch.arena, &lst_merge_B, line_b);
                }
                break;
            case EditType::Ins:
                {
                    // Add insert from B.
                    LineRange rng_b = text_file_line_range(*b, Editor::CursorLine(e->edit.idx_b));
                    MergedLine line_b = {
                        .first = rng_b.first,
                        .last = rng_b.last,
                        .type = EditType::Ins,
                    };
                    // Try to pull from the merged list.  If we have one, we don't need to add
                    // a sentinel to the A side.
                    MergedLineNode* node = lst_merge_B.first;
                    if (node == nullptr)
                    {
                        node = push_merge_line(scratch.arena, &lst_B, line_b);
                        MergedLine line_a = {
                            .first = Editor::CharOffset::Sentinel,
                            .last = Editor::CharOffset::Sentinel,
                            .type = EditType::Invalid,
                        };
                        push_merge_line(scratch.arena, &lst_A, line_a);
                    }
                    // We already have an entry for A.
                    else
                    {
                        node->line = line_b;
                        SLLQueuePop(lst_merge_B.first, lst_merge_B.last);
                        node->next = nullptr;
                        --lst_merge_B.count;
                        SLLQueuePush(lst_B.first, lst_B.last, node);
                        ++lst_B.count;
                    }
                }
                break;
            case EditType::Eq:
                {
                    // If there are any entries on the merge list, we need to add them now as gap entries.
                    MergedLineNode* node = lst_merge_B.first;
                    while (node != nullptr)
                    {
                        // Add gap from B.
                        LineRange rng_b = text_file_line_range(*b, Editor::CursorLine(e->edit.idx_b));
                        MergedLine line_b = {
                            .first = Editor::CharOffset::Sentinel,
                            .last = Editor::CharOffset::Sentinel,
                            .type = EditType::Invalid,
                        };
                        // Insert B.
                        node->line = line_b;
                        SLLQueuePop(lst_merge_B.first, lst_merge_B.last);
                        node->next = nullptr;
                        --lst_merge_B.count;
                        SLLQueuePush(lst_B.first, lst_B.last, node);
                        ++lst_B.count;

                        // Move node forward.
                        node = lst_merge_B.first;
                    }
                    // Insert on both sides.
                    LineRange rng_b = text_file_line_range(*b, Editor::CursorLine(e->edit.idx_b));
                    LineRange rng_a = text_file_line_range(*a, Editor::CursorLine(e->edit.idx_a));
                    MergedLine line_a = {
                        .first = rng_a.first,
                        .last = rng_a.last,
                        .type = EditType::Eq,
                    };
                    MergedLine line_b = {
                        .first = rng_b.first,
                        .last = rng_b.last,
                        .type = EditType::Eq,
                    };
                    push_merge_line(scratch.arena, &lst_A, line_a);
                    push_merge_line(scratch.arena, &lst_B, line_b);
                    assert(lst_A.count == lst_B.count);
                }
                break;
            case EditType::Invalid:
                break;
            }
        }

        populate_diff(panel->A.view, lst_A);
        populate_diff(panel->B.view, lst_B);
        Arena::scratch_end(scratch);
    }

    // Building.
    void build_diff_panel(DiffPanel* panel,
                            CmdBuffer::CmdList* cmd_lst,
                            CmdBuffer::DrawList* core_lst,
                            UI::UIState* state,
                            Feed::MessageFeed*)
    {
        PROF_SCOPE();

        auto clip = CmdBuffer::current_clip(*core_lst);

        // Start the frame for the enclosing editor frame.
        CmdBuffer::new_frame(panel->frame_lst, core_lst->screen, { .dt = core_lst->delta_time, .app_time = core_lst->app_time });
        // Default clip rect for the screen.
        CmdBuffer::push_clip(panel->frame_lst, clip);
        // Default texture (atlas by default).
        CmdBuffer::push_texture(panel->frame_lst, panel->atlas->atlas_texture());
        // Default palette.
        CmdBuffer::push_color_palette(panel->frame_lst, *CmdBuffer::current_palette(*core_lst));

        // Build non-leaf UI.
        {
            CmdBuffer::start_shapes(panel->frame_lst, Render::VertShader::OneOneTransform);
            Vec4f region_color = Config::widget_colors().outline_selection;
            const float boundary_width_bias = Config::diff_state().diff_font_size / 3.f;
            for (PartitionPanel* child = &panel->A;
                // Non-leaf UI does only involves inner-panels (e.g. the fence post problem).
                not null_panel(child) and not null_panel(child->sib_next);
                child = child->sib_next)
            {
                PartitionPanel* sib = child->sib_next;
                CmdBuffer::ClipRect child_clip = clip_from_parent(clip, &panel->A, child);
                CmdBuffer::ClipRect sib_clip = clip_from_parent(clip, &panel->A, sib);
                CmdBuffer::ClipRect boundary_clip = {};

                Vec4f panelv_clip = clip_as_vec(clip);
                {
                    Vec4f childv_clip = clip_as_vec(child_clip);
                    Vec4f sibv_clip = clip_as_vec(sib_clip);
                    Vec4f boundaryv_clip{};
                    boundaryv_clip.p0[0] = childv_clip.p1[0] - PartitionPanel::padding;
                    boundaryv_clip.p1[0] = sibv_clip.p0[0] + PartitionPanel::padding;
                    boundaryv_clip.p0[1] = panelv_clip.p0[1];
                    boundaryv_clip.p1[1] = panelv_clip.p1[1];
                    boundary_clip = vec_as_clip(boundaryv_clip);
                }

                Widgets::ID boundary_id = Widgets::ID::Zero;
                {
                    Widgets::ID ids[] = { panel->id, child->id, sib->id };
                    Widgets::MultiSeed multi_seed_in{
                        .first = ids,
                        .last = ids + std::size(ids)
                    };
                    boundary_id = Widgets::make_multi_seed(multi_seed_in, "bndry");
                }

                if (mouse_in_clip(state->mouse.ui_mouse, pad_clip(boundary_clip, Vec2i(static_cast<int>(-boundary_width_bias)))))
                {
                    try_set_hot_widget(state, boundary_id);
                    if (down(*state, MouseButton::L))
                    {
                        bool first_focus = state->focus_widget != boundary_id;
                        try_set_focus_widget(state, boundary_id);
                        if (state->focus_widget == boundary_id
                            and first_focus)
                        {
                            // Stash some drag data.
                            Vec2f start_pct{ child->pct_of_parent, sib->pct_of_parent };
                            start_drag(state, boundary_id, state->mouse.ui_mouse, start_pct);
                        }
                    }
                }

                // Process movement.
                if (dragging(*state, boundary_id))
                {
                    const Vec2f* drag_data = drag_payload<Vec2f>(state);
                    constexpr float min_pixel_value = 50.f;
                    Vec2i mouse_delta = state->mouse.ui_mouse - state->drag.payload.start_point;
                    float total_size = panelv_clip.p1[0] - panelv_clip.p0[0];
                    float child_pct_before = drag_data->x; // Child %.
                    float child_pixels_before = child_pct_before * total_size;
                    float child_pixels_after = std::max(child_pixels_before + mouse_delta.xy[0], min_pixel_value);
                    float child_pct_after = child_pixels_after / total_size;

                    float pct_delta = child_pct_after - child_pct_before;
                    float sib_pct_before = drag_data->y; // Sib %.
                    float sib_pct_after = sib_pct_before - pct_delta;
                    float sib_pixels_after = sib_pct_after * total_size;
                    if (sib_pixels_after < 50.f)
                    {
                        sib_pixels_after = 50.f;
                        sib_pct_after = sib_pixels_after / total_size;
                        pct_delta = -(sib_pct_after - sib_pct_before);
                        child_pct_after = child_pct_before + pct_delta;
                    }
                    child->pct_of_parent = child_pct_after;
                    sib->pct_of_parent = sib_pct_after;
                }

                if (state->focus_widget == boundary_id
                    and not down(*state, MouseButton::L)
                    and clicked_count(*state, MouseButton::L) == 2)
                {
                    // If the boundary is double-clicked, we'll resize both boundaries to be even.
                    float pct_sum = child->pct_of_parent + sib->pct_of_parent;
                    child->pct_of_parent = 0.5f * pct_sum;
                    sib->pct_of_parent = 0.5f * pct_sum;
                }

                if ((state->hot_widget == boundary_id
                        and self_or_empty_focus_widget(*state, boundary_id))
                    or dragging(*state, boundary_id))
                {
                    auto [pos, size] = pos_size_clip(boundary_clip);
                    CmdBuffer::solid_rect(panel->frame_lst, Render::FragShader::BasicColor, pos, size, region_color);
                    change_cursor(state, UI::CursorStyle::LeftRightArrow);
                }
            }
        }

        // Note: This relies on only having the two panels.
        DiffTextView* scroll_changed[] = {
            nullptr,
            nullptr
        };
        uint64_t scroll_idx = 0;

        // Build leaf-UI.
        for (PartitionPanel* child = &panel->A;
            not null_panel(child);
            child = child->sib_next)
        {
            CmdBuffer::ClipRect child_clip = clip_from_parent(clip, &panel->A, child);
            // Setup command buffer for panel.
            CmdBuffer::new_frame(child->draw_lst, core_lst->screen, { .dt = core_lst->delta_time, .app_time = core_lst->app_time });
            // Create the rect.
            CmdBuffer::push_clip(child->draw_lst, child_clip);
            // Default texture (atlas by default).
            CmdBuffer::push_texture(child->draw_lst, panel->atlas->atlas_texture());
            // Default palette.
            CmdBuffer::push_color_palette(child->draw_lst, *CmdBuffer::current_palette(*core_lst));

            // Build core widget.
            auto r = build_diff_text_view(child->view, child->draw_lst, state);
            scroll_changed[scroll_idx++] = r.scroll_changed ? child->view : nullptr;

            CmdBuffer::pop_clip(child->draw_lst);
            CmdBuffer::pop_texture(child->draw_lst);
            CmdBuffer::pop_color_palette(child->draw_lst);

            CmdBuffer::push_draw_list(cmd_lst, child->draw_lst);
        }

        for EachIndex(i, std::size(scroll_changed))
        {
            DiffTextView* view = scroll_changed[i];
            if (view != nullptr)
            {
                DiffTextView* share_to = panel->A.view;
                if (view == panel->A.view)
                {
                    share_to = panel->B.view;
                }
                share_scroll_pos(share_to, view);
            }
        }

        CmdBuffer::pop_clip(panel->frame_lst);
        CmdBuffer::pop_texture(panel->frame_lst);
        CmdBuffer::pop_color_palette(panel->frame_lst);

        CmdBuffer::push_draw_list(cmd_lst, panel->frame_lst);
    }
} // namespace Diff