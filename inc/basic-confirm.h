#pragma once

#include <memory>
#include <string_view>

#include "glyph-cache.h"
#include "types.h"
#include "ui-common.h"

namespace UI::Widgets
{
    struct BuildConfirmDlgResponse
    {
        bool close = false;
        ID clicked = ID::Zero;
    };

    struct ConfirmDlgEntry
    {
        ID btn_id;
        std::string_view btn_label;
    };

    struct BuildConfirmDlgInput
    {
        ConfirmDlgEntry* first;
        ConfirmDlgEntry* last;
        std::string_view description;
    };

    class ConfirmDlg
    {
    public:
        struct Data;

        ConfirmDlg();
        ~ConfirmDlg();

        // Interaction.
        void sync_config(Glyph::Atlas* atlas);
        void start(std::string_view title);

        BuildConfirmDlgResponse build(CmdBuffer::DrawList* lst, UI::UIState* state, Glyph::Atlas* atlas, const BuildConfirmDlgInput& in);
    private:
        std::unique_ptr<Data> data;
    };
} // namespace UI::Widgets