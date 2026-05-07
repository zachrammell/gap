#pragma once

#include "glyph-cache.h"
#include "ui-common.h"

namespace Diff
{
    struct FlatDirEntry
    {
        FlatDirEntry* next;
        FlatDirEntry* prev;
        OS::DirIterResult item;
    };

    struct FlatDirEntryList
    {
        FlatDirEntry* first;
        FlatDirEntry* last;
        uint64_t count;
    };

    struct DiffDirListViewResponse
    {
    };

    struct DiffDirListView;

    // Creation.
    DiffDirListView* make_diff_dir_list_view(Glyph::Atlas* atlas, UI::Widgets::ID id);

    // Cleanup.
    void release_diff_dir_list_view(DiffDirListView* widget);

    // Interaction.

    // Building.
    DiffDirListViewResponse build_diff_dir_list_view(DiffDirListView* widget,
                                                        CmdBuffer::DrawList* lst,
                                                        UI::UIState* state);
} // namespace Diff