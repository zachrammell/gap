#pragma once

#include <memory>

#include "basic-window.h"
#include "clipboard-manager.h"
#include "glyph-cache.h"
#include "renderer.h"
#include "ui-common.h"

namespace Config
{
    struct BuildExplorerResponse
    {
        bool request_config_path = false;
        bool config_updated = false;
        bool close = false;
    };

    class Explorer
    {
    public:
        struct Data;

        Explorer(Glyph::Atlas* atlas);
        ~Explorer();

        // Interaction.
        void start(const ScreenDimensions& screen, UI::UIState* state);
        void sync_config();
        bool try_close_dialog();
        bool sub_dialog_open(UI::UIState* state);
        void populate_path_value(String8 new_path);

        // Queries.
        UI::Widgets::ID id() const;
        String8 target_config_path() const;

        // Building.
        BuildExplorerResponse build(CmdBuffer::DrawList* lst,
                                    UI::UIState* state,
                                    Feed::MessageFeed* feed);
    private:
        std::unique_ptr<Data> data;
    };
} // namespace Config