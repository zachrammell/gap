#pragma once

#include <memory>

#include "cmd-buffer-api.h"
#include "glyph-cache.h"
#include "types.h"
#include "ui-common.h"

namespace Arena::Report
{
    struct ArenaReportResponse
    {
        String8 locus;
        int line;
        bool open_locus = false;
        bool close = false;
    };

    class ArenaReport
    {
    public:
        struct Data;

        ArenaReport(Glyph::Atlas* atlas);
        ~ArenaReport();

        // Interaction.
        void sync_config();
        void start(const ScreenDimensions& screen, UI::UIState* state);

        ArenaReportResponse build(CmdBuffer::DrawList* lst, UI::UIState* state);
    private:
        std::unique_ptr<Data> data;
    };
} // namespace Arena