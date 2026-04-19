#pragma once

#include "diff-core.h"
#include "glyph-cache.h"
#include "ui-common.h"

namespace Diff
{
    struct DiffPanelResponse
    {
        bool updated_cfg;
    };

    struct DiffPanel;

    // Creation.
    DiffPanel* make_diff_panel(Glyph::Atlas* atlas);

    // Cleanup.
    void release_diff_panel(DiffPanel* panel);

    // Interaction.
    void file_A(DiffPanel* panel, const TextFile& file);
    void file_B(DiffPanel* panel, const TextFile& file);
    void apply_diff(DiffPanel* panel, Feed::MessageFeed* feed);
    void sync_config(DiffPanel* panel, Feed::MessageFeed* feed);
    void try_file_drop(DiffPanel* panel, String8 path, UI::UIState* state, Feed::MessageFeed* feed);

    // Building.
    DiffPanelResponse build_diff_panel(DiffPanel* panel,
                                        CmdBuffer::CmdList* cmd_lst,
                                        CmdBuffer::DrawList* core_lst,
                                        UI::UIState* state,
                                        Feed::MessageFeed* feed);
} // namespace Diff