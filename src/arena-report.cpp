#include "arena-report.h"

#include "arena.h"
#include "basic-button.h"
#include "basic-scrollbox.h"
#include "basic-window.h"
#include "config.h"
#include "util.h"
#include "widgets.h"

namespace Arena::Report
{
    namespace
    {
        constexpr uint64_t sentinel_snap_idx = uint64_t(-1);

        struct ReportEntry
        {
            String8 locus;
            HumanFileSize size;
            HumanFileSize commit_size;
            String8 max_alloc_locus;
            HumanFileSize max_alloc_size;
            uint64_t snap_idx;
        };

        struct ReportEntryArray
        {
            ReportEntry* entries;
            uint64_t size;

            uint64_t longest_locus;
            uint64_t longest_size;
            uint64_t longest_commit;
            uint64_t longest_max_locus;
            uint64_t longest_max_size;
        };

        ReportEntryArray make_report_entries(Arena* arena, const ArenaTrackerSnapshot& snap)
        {
            ReportEntryArray result{};
            // +1 for the meta entry to sum it all up.
            result.entries = push_array_no_zero<ReportEntry>(arena, snap.size + 1);
            result.size = snap.size + 1;
            CommitSize total_cmt{};
            Position total_pos{};
            Position abs_max{};
            String8 max_locus = str8_empty;
            for EachIndex(i, snap.size)
            {
                const ArenaTrackerSnapshotEntry& snap_e = snap.elements[i];
                ReportEntry* entry = &result.entries[i + 1];
                entry->locus = str8(const_cast<char*>(snap_e.alloc_file), snap_e.alloc_file_len);
                entry->max_alloc_locus = str8(const_cast<char*>(snap_e.peak_file), snap_e.peak_file_len);
                entry->size = to_human_readable_file_size(arena, OS::FileLength{ rep(snap_e.arena_snapshot.pos) });
                entry->commit_size = to_human_readable_file_size(arena, OS::FileLength{ rep(snap_e.arena_snapshot.os_cmt) });
                entry->max_alloc_size = to_human_readable_file_size(arena, OS::FileLength{ rep(snap_e.peak_pos) });
                // Slice to just the filename of the locations.
                entry->locus = filename(entry->locus);
                entry->locus = str8_fmt(arena, "%S(%d)", entry->locus, snap_e.line);
                entry->max_alloc_locus = filename(entry->max_alloc_locus);
                entry->max_alloc_locus = str8_fmt(arena, "%S(%d)", entry->max_alloc_locus, snap_e.peak_line);
                entry->snap_idx = i;

                result.longest_locus = std::max(result.longest_locus, entry->locus.size);
                result.longest_size = std::max(result.longest_size, entry->size.string.size);
                result.longest_commit = std::max(result.longest_commit, entry->commit_size.string.size);
                result.longest_max_locus = std::max(result.longest_max_locus, entry->max_alloc_locus.size);
                result.longest_max_size = std::max(result.longest_max_size, entry->max_alloc_size.string.size);

                total_pos = Position{ rep(snap_e.arena_snapshot.pos) + rep(total_pos) };
                total_cmt = CommitSize{ rep(snap_e.arena_snapshot.os_cmt) + rep(total_cmt) };
                if (abs_max < snap_e.peak_pos)
                {
                    abs_max = snap_e.peak_pos;
                    max_locus = entry->max_alloc_locus;
                }
            }
            // Meta entry, at the top.
            ReportEntry* meta = &result.entries[0];
            meta->locus = str8_mut(str8_literal("Totals"));
            meta->max_alloc_locus = str8_empty;
            meta->size = to_human_readable_file_size(arena, OS::FileLength{ rep(total_pos) });
            meta->commit_size = to_human_readable_file_size(arena, OS::FileLength{ rep(total_cmt) });
            meta->max_alloc_size = to_human_readable_file_size(arena, OS::FileLength{ rep(abs_max) });
            meta->max_alloc_locus = max_locus;
            meta->snap_idx = sentinel_snap_idx;

            // Wrap up longest totals now based on meta entry.
            result.longest_locus = std::max(result.longest_locus, meta->locus.size);
            result.longest_size = std::max(result.longest_size, meta->size.string.size);
            result.longest_commit = std::max(result.longest_commit, meta->commit_size.string.size);
            result.longest_max_locus = std::max(result.longest_max_locus, meta->max_alloc_locus.size);
            result.longest_max_size = std::max(result.longest_max_size, meta->max_alloc_size.string.size);

            return result;
        }

        enum class SortColumn
        {
            None,
            AscPosition,
            AscCmt,
            AscMaxPosition,
            DescMark,
            DescPosition,
            DescCmt,
            DescMaxPosition,
            Count
        };

        constexpr SortColumn reverse(SortColumn c)
        {
            return SortColumn{ (rep(c) + rep(SortColumn::DescMark)) % (count_of<SortColumn>) };
        }

        static_assert(reverse(SortColumn::AscPosition) == SortColumn::DescPosition);
        static_assert(reverse(SortColumn::DescPosition) == SortColumn::AscPosition);
        static_assert(reverse(SortColumn::AscCmt) == SortColumn::DescCmt);
        static_assert(reverse(SortColumn::DescCmt) == SortColumn::AscCmt);
        static_assert(reverse(SortColumn::AscMaxPosition) == SortColumn::DescMaxPosition);
        static_assert(reverse(SortColumn::DescMaxPosition) == SortColumn::AscMaxPosition);
    } // namespace [anon]

    struct ArenaReport::Data
    {
        Arena* snap_arena;
        Arena* report_arena;
        ArenaTrackerSnapshot snapshot;
        ReportEntryArray report;
        UI::Widgets::BasicWindow window;
        UI::Widgets::ScrollBox scrollbox;
        Glyph::Atlas* atlas;
        float wheel_offset_amount = 0.f;
        SortColumn sort_column = SortColumn::None;

        static constexpr float padding = 2.f;
        static constexpr String8View sep = str8_literal(" : ");
    };

    namespace
    {
        void setup_ui_data(ArenaReport::Data* data, Glyph::FontSize font_size)
        {
            data->window.background_alpha(0.8f);
            data->scrollbox.scroll_to(0.f);
            data->wheel_offset_amount = UI::standard_font_padding(font_size);
        }

        Render::RenderViewport initial_window_viewport(const ScreenDimensions& screen)
        {
            auto viewport = Render::RenderViewport::basic(screen);
            viewport.width = Width{ rep(viewport.width) / 2 };
            viewport.height = Height{ rep(viewport.height) / 2 };
            viewport.offset_x = Render::ViewportOffsetX{ (rep(screen.width) - rep(viewport.width)) / 2 };
            viewport.offset_y = Render::ViewportOffsetY{ rep(viewport.height) / 2 };
            return viewport;
        }

        Vec2f content_size(ArenaReport::Data* data, Glyph::RenderFontContext* font_ctx)
        {
            Vec2f total_size{};
            // General element layout is:
            // Arena location : pos (bytes) : peak file : peak pos (bytes).

            // To get the total size we will heuristically measure a single glyph and multiply.
            const float glyph_width_est = font_ctx->measure_text("H").x;
            const float sep_width = font_ctx->measure_text(str8_mut(ArenaReport::Data::sep)).x;
            total_size.x = (data->report.longest_locus * glyph_width_est + sep_width
                            + data->report.longest_size * glyph_width_est + sep_width
                            + data->report.longest_commit * glyph_width_est + sep_width
                            + data->report.longest_max_locus * glyph_width_est + sep_width
                            + data->report.longest_max_size * glyph_width_est + ArenaReport::Data::padding);
            total_size.y = static_cast<float>(UI::standard_font_padding(Glyph::FontSize{ font_ctx->current_font_size() }) * data->report.size);
            return total_size;
        }

        struct WindowContentViewports
        {
            Render::RenderViewport sort_buttons_vp;
            Render::RenderViewport scroll_vp;
        };

        int sort_buttons_height(Glyph::FontSize font_size, const Render::RenderViewport& vp)
        {
            int buttons_vp_height = std::min(static_cast<int>(UI::standard_font_padding(font_size) + ArenaReport::Data::padding), rep(vp.height));
            return buttons_vp_height;
        }

        WindowContentViewports window_content_viewports(Glyph::FontSize font_size, const Render::RenderViewport& viewport)
        {
            int btn_height = sort_buttons_height(font_size, viewport);
            auto btn_vp = viewport;
            btn_vp.height = Height{ btn_height };
            btn_vp.offset_y = UI::offset_from(viewport.offset_y, rep(viewport.height) - btn_height);

            auto scroll_vp = viewport;
            scroll_vp.height = Height{ std::max(0, rep(viewport.height) - static_cast<int>(ArenaReport::Data::padding) - btn_height) };
            return { .sort_buttons_vp = btn_vp,
                     .scroll_vp = scroll_vp };
        }

        void acquire_snapshot(ArenaReport::Data* data)
        {
            // Create a snapshot.
            clear(data->snap_arena);
            data->snapshot = arena_tracker_snapshot(data->snap_arena);
            mark_tracker_clean();
        }

        bool sort_cmp_asc_position(const ArenaTrackerSnapshotEntry& l, const ArenaTrackerSnapshotEntry& r)
        {
            return l.arena_snapshot.pos < r.arena_snapshot.pos;
        }

        bool sort_cmp_asc_cmt(const ArenaTrackerSnapshotEntry& l, const ArenaTrackerSnapshotEntry& r)
        {
            return l.arena_snapshot.os_cmt < r.arena_snapshot.os_cmt;
        }

        bool sort_cmp_asc_max_position(const ArenaTrackerSnapshotEntry& l, const ArenaTrackerSnapshotEntry& r)
        {
            return l.peak_pos < r.peak_pos;
        }

        bool sort_cmp_desc_position(const ArenaTrackerSnapshotEntry& l, const ArenaTrackerSnapshotEntry& r)
        {
            return l.arena_snapshot.pos > r.arena_snapshot.pos;
        }

        bool sort_cmp_desc_cmt(const ArenaTrackerSnapshotEntry& l, const ArenaTrackerSnapshotEntry& r)
        {
            return l.arena_snapshot.os_cmt > r.arena_snapshot.os_cmt;
        }

        bool sort_cmp_desc_max_position(const ArenaTrackerSnapshotEntry& l, const ArenaTrackerSnapshotEntry& r)
        {
            return l.peak_pos > r.peak_pos;
        }

        using SortPred = bool(*)(const ArenaTrackerSnapshotEntry&, const ArenaTrackerSnapshotEntry&);

        void apply_sort(ArenaReport::Data* data)
        {
            bool apply_sort = false;
            SortPred pred = nullptr;
            switch (data->sort_column)
            {
            case SortColumn::None:
                break;
            case SortColumn::AscPosition:
                pred = sort_cmp_asc_position;
                apply_sort = true;
                break;
            case SortColumn::AscCmt:
                pred = sort_cmp_asc_cmt;
                apply_sort = true;
                break;
            case SortColumn::AscMaxPosition:
                pred = sort_cmp_asc_max_position;
                apply_sort = true;
                break;
            case SortColumn::DescPosition:
                pred = sort_cmp_desc_position;
                apply_sort = true;
                break;
            case SortColumn::DescCmt:
                pred = sort_cmp_desc_cmt;
                apply_sort = true;
                break;
            case SortColumn::DescMaxPosition:
                pred = sort_cmp_desc_max_position;
                apply_sort = true;
                break;
            }

            if (not apply_sort)
                return;
            std::sort(data->snapshot.elements, data->snapshot.elements + data->snapshot.size, pred);
            // Create a report.
            data->report = make_report_entries(data->report_arena, data->snapshot);
        }

        void build_report(ArenaReport::Data* data)
        {
            clear(data->report_arena);
            if (data->sort_column != SortColumn::None)
            {
                apply_sort(data);
            }
            else
            {
                // Create a report.
                data->report = make_report_entries(data->report_arena, data->snapshot);
            }
            // After we're done here, we should mark that the arena is not dirty because we don't want to report against ourself.
            mark_tracker_clean();
        }

        void build_arena_list(ArenaReport::Data* data,
                                ArenaReportResponse* report_resp,
                                CmdBuffer::DrawList* lst,
                                UI::UIState* state)
        {
            const auto& colors = Config::widget_colors();
            auto font_size = Glyph::FontSize{ Config::widget_state().window_title_font_size };
            auto font_ctx = data->atlas->render_font_context(font_size);
            const float glyph_width_est = font_ctx.measure_text("H").x;
            auto [btn_vp, scroll_vp] = window_content_viewports(font_size, data->window.content_viewport(data->window.window_viewport()));
            UI::Widgets::BuildButtonInput btn_in{
                .id = UI::Widgets::make_id_seed(UI::Widgets::ID::ArenaReport, "Locus"),
                .label = "Locus",
                .pos = {},
                .padding = ArenaReport::Data::padding,
                .forced_size = {},
                .thickness = ArenaReport::Data::padding
            };
            // Create the sort buttons.
            {
                CmdBuffer::push_clip(lst, UI::convert(btn_vp));
                const float sep_width = font_ctx.measure_text(str8_mut(ArenaReport::Data::sep)).x;
                Glyph::SpecialGlyph sort_ico[2] =
                {
                    Glyph::SpecialGlyph::ArrowUp,
                    Glyph::SpecialGlyph::ArrowDown,
                };
                // Offset based on scroll offset as well.
                btn_in.pos.x = -data->scrollbox.position().x;
                // 'Locus'.
                UI::Widgets::basic_button(lst, state, &font_ctx, btn_in, UI::Widgets::BuildButtonFlags::None);
                // 'Size'.
                // Offset pos to the beginning of the next column.
                btn_in.pos.x += glyph_width_est * data->report.longest_locus + sep_width;
                btn_in.id = UI::Widgets::make_id_seed(UI::Widgets::ID::ArenaReport, "Size");
                btn_in.label = "Size";
                if (data->sort_column == SortColumn::AscPosition or data->sort_column == SortColumn::DescPosition)
                {
                    UI::Widgets::BuildIconicTextButtonInput ico_btn{
                        .btn_in = btn_in,
                        .icon = sort_ico[data->sort_column > SortColumn::DescMark],
                        .icon_color = colors.window_title_font_color,
                    };
                    auto resp = UI::Widgets::basic_left_iconic_text_button(lst, state, &font_ctx, ico_btn, UI::Widgets::BuildButtonFlags::None);
                    if (resp.clicked)
                    {
                        data->sort_column = reverse(data->sort_column);
                        build_report(data);
                    }
                }
                else
                {
                    auto resp = UI::Widgets::basic_button(lst, state, &font_ctx, btn_in, UI::Widgets::BuildButtonFlags::None);
                    if (resp.clicked)
                    {
                        data->sort_column = SortColumn::DescPosition;
                        build_report(data);
                    }
                }
                // 'Commit size'
                // Offset pos to the beginning of the next column.
                btn_in.pos.x += glyph_width_est * data->report.longest_size + sep_width;
                btn_in.id = UI::Widgets::make_id_seed(UI::Widgets::ID::ArenaReport, "Cmt");
                btn_in.label = "Cmt";
                if (data->sort_column == SortColumn::AscCmt or data->sort_column == SortColumn::DescCmt)
                {
                    UI::Widgets::BuildIconicTextButtonInput ico_btn{
                        .btn_in = btn_in,
                        .icon = sort_ico[data->sort_column > SortColumn::DescMark],
                        .icon_color = colors.window_title_font_color,
                    };
                    auto resp = UI::Widgets::basic_left_iconic_text_button(lst, state, &font_ctx, ico_btn, UI::Widgets::BuildButtonFlags::None);
                    if (resp.clicked)
                    {
                        data->sort_column = reverse(data->sort_column);
                        build_report(data);
                    }
                }
                else
                {
                    auto resp = UI::Widgets::basic_button(lst, state, &font_ctx, btn_in, UI::Widgets::BuildButtonFlags::None);
                    if (resp.clicked)
                    {
                        data->sort_column = SortColumn::DescCmt;
                        build_report(data);
                    }
                }
                // 'Peak file'
                // Offset pos to the beginning of the next column.
                btn_in.pos.x += glyph_width_est * data->report.longest_commit + sep_width;
                btn_in.id = UI::Widgets::make_id_seed(UI::Widgets::ID::ArenaReport, "Peak file");
                btn_in.label = "Peak file";
                UI::Widgets::basic_button(lst, state, &font_ctx, btn_in, UI::Widgets::BuildButtonFlags::None);
                // 'Peak size'
                // Offset pos to the beginning of the next column.
                btn_in.pos.x += glyph_width_est * data->report.longest_max_locus + sep_width;
                btn_in.id = UI::Widgets::make_id_seed(UI::Widgets::ID::ArenaReport, "Peak size");
                btn_in.label = "Peak size";
                if (data->sort_column == SortColumn::AscMaxPosition or data->sort_column == SortColumn::DescMaxPosition)
                {
                    UI::Widgets::BuildIconicTextButtonInput ico_btn{
                        .btn_in = btn_in,
                        .icon = sort_ico[data->sort_column > SortColumn::DescMark],
                        .icon_color = colors.window_title_font_color,
                    };
                    auto resp = UI::Widgets::basic_left_iconic_text_button(lst, state, &font_ctx, ico_btn, UI::Widgets::BuildButtonFlags::None);
                    if (resp.clicked)
                    {
                        data->sort_column = reverse(data->sort_column);
                        build_report(data);
                    }
                }
                else
                {
                    auto resp = UI::Widgets::basic_button(lst, state, &font_ctx, btn_in, UI::Widgets::BuildButtonFlags::None);
                    if (resp.clicked)
                    {
                        data->sort_column = SortColumn::DescMaxPosition;
                        build_report(data);
                    }
                }
                CmdBuffer::pop_clip(lst);
            }
            auto content_vp = data->scrollbox.content_viewport(scroll_vp);
            data->scrollbox.content_size(content_size(data, &font_ctx), content_vp);
            CmdBuffer::push_clip(lst, UI::convert(scroll_vp));
            {
                auto scroll_resp = data->scrollbox.build(lst,
                                                            state,
                                                            data->wheel_offset_amount,
                                                            UI::Widgets::BuildScrollBoxFlags::None);
                // If the scroll changed set the focus so no other widgets try to process the event.
                if (scroll_resp.scroll_changed)
                {
                    UI::try_set_focus_widget(state, UI::Widgets::ID::ArenaReport);
                }
            }

            const float entry_height = UI::standard_font_padding(font_size);
            const float line_height = static_cast<float>(font_ctx.current_font_line_height());
            const float text_adjust_y = (entry_height + line_height) / 5.f;
            Vec2f pos{};
            pos.y = 0.f + rep(content_vp.height);
            pos.y += data->scrollbox.position().y;
            CmdBuffer::push_clip(lst, UI::convert(content_vp));
            CmdBuffer::start_glyph_run(lst, Render::VertShader::OneOneTransform);
            // Clear padding/thickness for labels below.
            btn_in.padding = {};
            btn_in.thickness = {};
            for EachIndex(i, data->report.size)
            {
                ReportEntry* e = &data->report.entries[i];
                pos.x = -data->scrollbox.position().x;
                pos.y -= entry_height;
                // Haven't yet entered the viewport.
                if (pos.y > rep(content_vp.height))
                    continue;
                // Format: Arena location : pos (bytes) : peak file : peak pos (bytes).
                // Make the filenames clickable so we can navigate to those locations.
                btn_in.id = UI::Widgets::make_id_seed_idx(UI::Widgets::ID::ArenaReport, i);
                btn_in.label = sv_str8(e->locus);
                btn_in.pos = pos;
                auto resp = UI::Widgets::basic_button(lst, state, &font_ctx, btn_in, UI::Widgets::BuildButtonFlags::None);
                pos.x += resp.btn_size.x;
                if (resp.clicked and e->snap_idx != sentinel_snap_idx)
                {
                    report_resp->locus = str8(const_cast<char*>(data->snapshot.elements[e->snap_idx].alloc_file), data->snapshot.elements[e->snap_idx].alloc_file_len);
                    report_resp->line = data->snapshot.elements[e->snap_idx].line;
                    report_resp->open_locus = true;
                }
                pos.x += glyph_width_est * (data->report.longest_locus - e->locus.size);
                pos.y += text_adjust_y;
                pos = font_ctx.render_text(lst, str8_mut(ArenaReport::Data::sep), pos, colors.window_title_font_color);
                pos.y -= text_adjust_y;

                pos.y += text_adjust_y;
                pos = font_ctx.render_text(lst, e->size.string, pos, mag_to_color(e->size.mag));
                pos.y -= text_adjust_y;
                pos.x += glyph_width_est * (data->report.longest_size - e->size.string.size);
                pos.y += text_adjust_y;
                pos = font_ctx.render_text(lst, str8_mut(ArenaReport::Data::sep), pos, colors.window_title_font_color);
                pos.y -= text_adjust_y;

                pos.y += text_adjust_y;
                pos = font_ctx.render_text(lst, e->commit_size.string, pos, mag_to_color(e->commit_size.mag));
                pos.y -= text_adjust_y;
                pos.x += glyph_width_est * (data->report.longest_commit - e->commit_size.string.size);
                pos.y += text_adjust_y;
                pos = font_ctx.render_text(lst, str8_mut(ArenaReport::Data::sep), pos, colors.window_title_font_color);
                pos.y -= text_adjust_y;

                // To get a unique index for this, we just adjust past the end of the array.
                btn_in.id = UI::Widgets::make_id_seed_idx(UI::Widgets::ID::ArenaReport, i + data->report.size);
                btn_in.label = sv_str8(e->max_alloc_locus);
                btn_in.pos = pos;
                resp = UI::Widgets::basic_button(lst, state, &font_ctx, btn_in, UI::Widgets::BuildButtonFlags::None);
                pos.x += resp.btn_size.x;
                if (resp.clicked and e->snap_idx != sentinel_snap_idx)
                {
                    report_resp->locus = str8(const_cast<char*>(data->snapshot.elements[e->snap_idx].peak_file), data->snapshot.elements[e->snap_idx].peak_file_len);
                    report_resp->line = data->snapshot.elements[e->snap_idx].peak_line;
                    report_resp->open_locus = true;
                }
                pos.x += glyph_width_est * (data->report.longest_max_locus - e->max_alloc_locus.size);
                pos.y += text_adjust_y;
                pos = font_ctx.render_text(lst, str8_mut(ArenaReport::Data::sep), pos, colors.window_title_font_color);
                pos.y -= text_adjust_y;

                pos.y += text_adjust_y;
                pos = font_ctx.render_text(lst, e->max_alloc_size.string, pos, mag_to_color(e->max_alloc_size.mag));
                pos.y -= text_adjust_y;
                // Past the current viewport.
                if (pos.y < -entry_height)
                    break;
            }
            // Content viewport.
            CmdBuffer::pop_clip(lst);
            // Scroll viewport.
            CmdBuffer::pop_clip(lst);
        }
    } // namespace [anon]

    ArenaReport::ArenaReport(Glyph::Atlas* atlas):
        data{ new Data{
            .window = { UI::Widgets::ID::ArenaReport },
            .scrollbox = { UI::Widgets::ID::ArenaReport },
            .atlas = atlas
        } }
    {
        data->window.title("Arena Report");
        data->snap_arena = alloc(default_params);
        data->report_arena = alloc(default_params);
    }

    ArenaReport::~ArenaReport() = default;

    // Interaction.
    void ArenaReport::sync_config()
    {
        data->window.sync_config(data->atlas);
        data->scrollbox.sync_config();
    }

    void ArenaReport::start(const ScreenDimensions& screen, UI::UIState* state)
    {
        UI::Widgets::ShowWindowData show_data{
            .initial_viewport = initial_window_viewport(screen),
            .expand_point = { 0.5f, 0.5f }
        };
        UI::set_focus_window(state, UI::Widgets::ID::ArenaReport);
        data->window.show(show_data);
        setup_ui_data(data.get(), Glyph::FontSize{ Config::widget_state().window_title_font_size });
        // Create a snapshot.
        acquire_snapshot(data.get());
        build_report(data.get());
    }

    ArenaReportResponse ArenaReport::build(CmdBuffer::DrawList* lst, UI::UIState* state)
    {
        ArenaReportResponse resp{};
        // First, let's see if we need to update our snapshot.
        if (tracker_dirty())
        {
            acquire_snapshot(data.get());
            build_report(data.get());
        }

        {
            auto window_resp = data->window.build(lst, data->atlas, state);
            resp.close = window_resp.close;
        }
        build_arena_list(data.get(), &resp, lst, state);
        data->window.end(state);
        return resp;
    }
} // namespace Arena