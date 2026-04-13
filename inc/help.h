#pragma once

#include <memory>
#include <string_view>

#include "clipboard-manager.h"
#include "glyph-cache.h"
#include "renderer.h"
#include "types.h"
#include "ui-common.h"

namespace Help
{
    struct BuildHelpResponse
    {
        Hotkeys::CustomHotkeyID builder_id = Hotkeys::CustomHotkeyID::None;
        Hotkeys::CustomHotkeyID remove_id = Hotkeys::CustomHotkeyID::None;
        Hotkeys::CustomHotkeyGroup group = {};
        bool close = false;
        bool reload_hotkeys = false;
        bool open_hk_builder = false;
    };

    class Help
    {
    public:
        struct Data;

        Help(Glyph::Atlas* atlas);
        ~Help();

        // Interaction.
        void sync_config();
        void hotkeys_updated();
        void start(const ScreenDimensions& screen, UI::UIState* state);

        BuildHelpResponse build(CmdBuffer::DrawList* lst,
                                UI::UIState* state,
                                Clipboard::ClipboardManager* clipboard,
                                Feed::MessageFeed* feed);
    private:
        std::unique_ptr<Data> data;
    };
} // namespace Help