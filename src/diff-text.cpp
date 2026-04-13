#include "diff-text.h"

#include "arena.h"
#include "basic-scrollbox.h"

namespace Diff
{
    struct DiffTextView
    {
        Arena::Arena* arena;
        Arena::Position base_pos;
        TextFile text;
        uint64_t longest_line;
        UI::Widgets::IndexedScrollBox* scroll;
        Glyph::Atlas* atlas;
    };

    namespace
    {
        UI::Widgets::IndexedScrollContentSize content_size(DiffTextView* widget, Glyph::RenderFontContext* font_ctx)
        {
            UI::Widgets::IndexedScrollContentSize size{};
            const int line_height = font_ctx->current_font_line_height();
            const float glyph_width_est = font_ctx->measure_text("H").x;
            size.v_size = widget->text.line_starts.size;
            size.entry_size.y = static_cast<float>(line_height);
            size.entry_size.x = glyph_width_est * widget->longest_line;

#if 0
            // If line numbers are enabled, factor in the largest number.
            if (data->show_line_numbers)
            {
                auto num_digits = digits(rep(buffer(data)->line_count()));
                // +1 more for the '.' and one more for padding.
                size.entry_size.x += num_digits * glyph_width_est + glyph_width_est + glyph_width_est;
            }
#endif

            size.entry_size.x += glyph_width_est;

            return size;
        }
    } // namespace [anon]

    // Creation.
    DiffTextView* make_diff_text_view(Glyph::Atlas* atlas, UI::Widgets::ID id)
    {
        Arena::Arena* arena = Arena::alloc(Arena::default_params);
        DiffTextView* widget = Arena::push_array<DiffTextView>(arena, 1);
        widget->arena = arena;
        // We need to do gross C++ here.
        {
            uint8_t* blob = Arena::push_array_no_zero_aligned<uint8_t>(arena,
                                                                sizeof(UI::Widgets::IndexedScrollBox),
                                                                Arena::Alignment{ alignof(UI::Widgets::IndexedScrollBox) });
            widget->scroll = new(blob) UI::Widgets::IndexedScrollBox{ id };
        }
        widget->base_pos = Arena::pos(arena);
        widget->atlas = atlas;
        return widget;
    }

    // Cleanup.
    void release_diff_text_view(DiffTextView* widget)
    {
        // Destroy C++ object.
        using SBox = UI::Widgets::IndexedScrollBox;
        widget->scroll->~SBox();

        Arena::Arena* arena = widget->arena;
        Arena::release(arena);
    }

    // Interaction.
    void populate_text(DiffTextView* widget, const TextFile& text)
    {
        // Copy the text file into our widget arena.
        Arena::pop_to(widget->arena, widget->base_pos);
        widget->text = text_file_copy_to(widget->arena, text);
        // Find the longest line.
        widget->longest_line = 0;
        for EachIndex(l, widget->text.line_starts.size)
        {
            String8 txt = text_file_line_text(widget->text, Editor::CursorLine{ l });
            widget->longest_line = std::max(widget->longest_line, txt.size);
        }
        widget->scroll->scroll_to({});
    }

    // Building.
    void build_diff_text_view(DiffTextView* widget,
                                CmdBuffer::DrawList* lst,
                                UI::UIState* state)
    {
        CmdBuffer::ClipRect clip = CmdBuffer::current_clip(*lst);
        Glyph::RenderFontContext font_ctx = widget->atlas->render_font_context(Glyph::FontSize{ Config::diff_state().diff_font_size });
        UI::Widgets::IndexedScrollContentSize scroll_size = content_size(widget, &font_ctx);
        const Config::DiffColors& colors = Config::diff_colors();

        const float wheel_offset_amt = UI::standard_font_padding(Glyph::FontSize{ font_ctx.current_font_size() }) * 2.f;
        const int line_height = font_ctx.current_font_line_height();

        // Setup the scroll widget.
        CmdBuffer::ClipRect content_clip = UI::convert(widget->scroll->content_viewport(UI::convert(clip)));
        {
            // Constrain the 'x' size here so we don't get a phantom horizontal scrollbar.
            scroll_size.entry_size.x = std::clamp(scroll_size.entry_size.x - rep(content_clip.width), 0.f, scroll_size.entry_size.x);
            widget->scroll->content_size(scroll_size);
            widget->scroll->build(lst, state, wheel_offset_amt, UI::Widgets::BuildScrollBoxFlags::None);
        }
        CmdBuffer::push_clip(lst, content_clip);
        // Core text.
        {
            // Find the line ranges.
            UI::Widgets::IndexedScrollOffset off = widget->scroll->position();
            // Note: the scrollbox starts at offset 0, but CursorLine is 1-indexed.
            Editor::CursorLine first = Editor::CursorLine(off.idx + 1);
            const int lines_per_v = rep(content_clip.height) / line_height;
            Editor::CursorLine last = Editor::CursorLine{ rep(first) + (off.offset.y > 0.f) + lines_per_v };
            last = std::clamp(last, first, Editor::CursorLine{ widget->text.line_starts.size });

            Vec2f start_pos;
            // Note: X-offset needs to pull text to left of viewport.
            start_pos.x = -off.offset.x;
            start_pos.y = rep(content_clip.height) + off.offset.y - font_ctx.current_font_size();

            CmdBuffer::start_glyph_run(lst, Render::VertShader::OneOneTransform);
            for (; first <= last; first = extend(first))
            {
                String8 txt = text_file_line_text(widget->text, first);
                font_ctx.render_text(lst, txt, start_pos, colors.eq);
                start_pos.y -= line_height;
            }
        }

        // Test.
        CmdBuffer::start_shapes(lst, Render::VertShader::OneOneTransform);
        UI::PosSize sz = UI::pos_size_clip(content_clip);
        sz.pos.x = 0;
        sz.pos.y = 0;
        CmdBuffer::solid_rect(lst, Render::FragShader::BasicColor, sz.pos, sz.size, hex_to_vec4f(0xff000011));
        CmdBuffer::pop_clip(lst);
    }
} // namespace Diff