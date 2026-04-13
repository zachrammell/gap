#pragma once

namespace Diff
{
    struct LineStarts
    {
        Editor::CharOffset* array;
        uint64_t size;
    };

    struct TextFile
    {
        String8 content;
        LineStarts line_starts;
    };

    // Text files.
    TextFile text_file_read(Arena::Arena* arena, String8 path);
    String8 text_file_line_text(const TextFile& file, Editor::CursorLine line);
    Editor::CursorLine text_file_line_for_offset(const TextFile& file, Editor::CharOffset off);
    TextFile text_file_copy_to(Arena::Arena* arena, const TextFile& file);
} // namespace Diff