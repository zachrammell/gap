#pragma once

#include "diff-core.h"
#include "gap-strings.h"
#include "glyph-cache.h"
#include "ui-common.h"

namespace Diff
{
    struct DiffTextView;

    // Creation.
    DiffTextView* make_diff_text_view(Glyph::Atlas* atlas, UI::Widgets::ID id);

    // Cleanup.
    void release_diff_text_view(DiffTextView* widget);

    // Interaction.
    void populate_text(DiffTextView* widget, const TextFile& text);

    // Building.
    void build_diff_text_view(DiffTextView* widget,
                                CmdBuffer::DrawList* lst,
                                UI::UIState* state);
} // namespace Diff