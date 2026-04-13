#include "diff-core.h"

namespace Diff
{
    void text_file_populate_line_starts(Arena::Arena* arena, TextFile* result)
    {
        // Let's start with a simple linked list of these line starts that we can then
        // flatten into an array.
        auto scratch = Arena::scratch_begin({ &arena, 1 });
        struct StartNode
        {
            StartNode* next;
            Editor::CharOffset start;
        };
        StartNode* first = nullptr;
        StartNode* last = nullptr;
        uint64_t count = 0;

        for EachIndex(i, result->content.size)
        {
            if (result->content.str[i] == '\n')
            {
                StartNode* n = Arena::push_array<StartNode>(scratch.arena, 1);
                n->start = Editor::CharOffset{ i + 1 };
                SLLQueuePush(first, last, n);
                ++count;
            }
        }
        // Flatten.
        result->line_starts.size = count + 1;
        result->line_starts.array = Arena::push_array_no_zero<Editor::CharOffset>(arena, result->line_starts.size);
        // First line.
        result->line_starts.array[0] = Editor::CharOffset{};
        uint64_t idx = 1;
        for EachNode(n, first)
        {
            result->line_starts.array[idx++] = n->start;
        }
        Arena::scratch_end(scratch);
    }

    TextFile text_file_read(Arena::Arena* arena, String8 path)
    {
        TextFile result = {};
        if (read_entire_file(arena, &result.content, path))
        {
            text_file_populate_line_starts(arena, &result);
        }
        else
        {
            fprintf(stderr, "ERROR: Failed to load file '%.*s'\n", int(path.size), path.str);
        }
        return result;
    }

    String8 text_file_line_text(const TextFile& file, Editor::CursorLine line)
    {
        // This is outside the file.  Return empty string.
        // Note: Lines are 1-indexed.
        if (rep(line) - 1 >= file.line_starts.size)
            return str8_empty;
        Editor::CharOffset start = file.line_starts.array[rep(line) - 1];
        Editor::CharOffset end = start;
        Editor::CursorLine next_l = extend(line);
        if (rep(next_l) - 1 >= file.line_starts.size)
        {
            end = Editor::CharOffset{ file.content.size };
        }
        else
        {
            end = file.line_starts.array[rep(next_l) - 1];
            // Note: We don't actually want to include the '\n' of the previous line,
            // so we remove it here.
            end = retract(end);
        }
        String8 substr = str8_substr(file.content, { .off = rep(start), .len = rep(distance(start, end)) });
        return substr;
    }

    Editor::CursorLine text_file_line_for_offset(const TextFile& file, Editor::CharOffset off)
    {
        if (file.line_starts.size <= 1)
            return Editor::CursorLine::Beginning;
        // We can binary search for the line.
        uint64_t low = 0;
        uint64_t high = file.line_starts.size - 1;
        uint64_t mid = 0;
        while (low <= high)
        {
            mid = low + ((high - low) / 2);
            if (mid == high)
                break;
            uint64_t mid_start = rep(file.line_starts.array[mid]);
            uint64_t mid_stop = rep(file.line_starts.array[mid + 1]);
            if (rep(off) < mid_start)
            {
                high = mid - 1;
            }
            else if (rep(off) >= mid_stop)
            {
                low = mid + 1;
            }
            else
            {
                break;
            }
        }
        return Editor::CursorLine{ mid + 1 };
    }

    TextFile text_file_copy_to(Arena::Arena* arena, const TextFile& file)
    {
        TextFile result = {};
        result.content = str8_copy(arena, file.content);
        result.line_starts.size = file.line_starts.size;
        result.line_starts.array = Arena::push_array_no_zero<Editor::CharOffset>(arena, result.line_starts.size);
        memcpy(result.line_starts.array, file.line_starts.array, result.line_starts.size * sizeof(Editor::CharOffset));
        return result;
    }
} // namespace Diff