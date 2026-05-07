#pragma once

#include "feed.h"
#include "glyph-cache.h"
#include "types.h"
#include "ui-common.h"

namespace Diff
{
    struct DiffDirPanelResponse
    {
        bool close;
    };

    struct DiffDirPanel;

    // Creation.
    DiffDirPanel* make_diff_dir_panel(Glyph::Atlas* atlas);

    // Clenaup.
    void release_diff_dir_panel(DiffDirPanel* panel);

    // Interaction.
    void diff_dir_panel_start(DiffDirPanel* panel, const ScreenDimensions& screen, UI::UIState* state);
    void diff_dir_panel_sync_config(DiffDirPanel* panel);

    // Building.
    DiffDirPanelResponse build_diff_dir_panel(DiffDirPanel* panel,
                                                CmdBuffer::CmdList* cmd_lst,
                                                CmdBuffer::DrawList* core_lst,
                                                UI::UIState* state,
                                                Feed::MessageFeed* feed);
} // namespace Diff