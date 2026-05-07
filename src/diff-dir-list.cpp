#include "diff-dir-list.h"

namespace Diff
{
    

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