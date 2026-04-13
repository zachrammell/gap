#pragma once

#include "glyph-cache.h"
#include "ui-common.h"

namespace Diff
{
    struct DiffPanel;

    // Creation.
    DiffPanel* make_diff_panel(Glyph::Atlas* atlas);

    // Cleanup.
    void release_diff_panel(DiffPanel* panel);

    // Building.
    void build_diff_panel(DiffPanel* panel,
                            CmdBuffer::CmdList* cmd_lst,
                            CmdBuffer::DrawList* core_lst,
                            UI::UIState* state,
                            Feed::MessageFeed* feed);
} // namespace Diff