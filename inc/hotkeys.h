#pragma once

#include "gap-strings.h"
#include "types.h"

namespace Feed
{
    class MessageFeed;
} // namespace Feed

namespace Hotkeys
{
    struct HotkeyEntry
    {
        Hotkey cmd = Hotkey::None;
        OS::Key key = OS::Key::Null;
        OS::KeyMods mods = OS::KeyMods::None;
    };

    struct HotkeyCollection
    {
        HotkeyEntry* first;
        HotkeyEntry* last;
    };

    struct CustomHotkeyEntry
    {
        CustomHotkeyEntry* next;
        CustomHotkeyID id;
        OS::Key key = OS::Key::Null;
        OS::KeyMods mods = OS::KeyMods::None;
    };

    struct CustomHotkeyList
    {
        CustomHotkeyEntry* first;
        CustomHotkeyEntry* last;
        uint64_t count;
    };

    // They're all opaque at this level.  The manager is type-aware.
    using CustomHotkeyFn = void(*)(void*);

    struct CustomHotkeyInfo
    {
        String8 name;
        String8 desc;
        String8 fn_name;
        CustomHotkeyFn fn;
    };

    struct HotkeyCombo
    {
        OS::Key key = OS::Key::Null;
        OS::KeyMods mods = OS::KeyMods::None;

        bool operator==(const HotkeyCombo&) const = default;
    };

    struct HotkeyMatch
    {
        Hotkey hk;
        CustomHotkeyID cust_hk_id;
    };

    struct HotkeyMatchNode
    {
        HotkeyMatchNode* next;
        CustomHotkeyGroup group;
        HotkeyMatch match;
    };

    struct HotkeyMatchList
    {
        HotkeyMatchNode* first;
        HotkeyMatchNode* last;
        uint64_t count;
    };

    void init();
    bool load(String8 path, Feed::MessageFeed* feed);
    bool save(String8 path, Feed::MessageFeed* feed);
    bool needs_save();

    // Queries.
    HotkeyCollection global_hotkeys();
    HotkeyCollection editor_hotkeys();
    HotkeyCollection file_explorer_hotkeys();
    HotkeyCollection find_replace_hotkeys();
    HotkeyCollection simple_cmd_hotkeys();
    HotkeyEntry match_global_hotkey(OS::Key key, OS::KeyMods mods);
    HotkeyEntry match_editor_hotkey(OS::Key key, OS::KeyMods mods);
    HotkeyEntry match_file_explorer_hotkey(OS::Key key, OS::KeyMods mods);
    HotkeyEntry match_find_replace_hotkey(OS::Key key, OS::KeyMods mods);
    HotkeyEntry match_simple_cmd_hotkey(OS::Key key, OS::KeyMods mods);
    HotkeyEntry entry_for(Hotkey hk);
    HotkeyMatch match_group(CustomHotkeyGroup group, OS::Key key, OS::KeyMods mods);
    CustomHotkeyID match_custom_hotkey(CustomHotkeyGroup group, OS::Key key, OS::KeyMods mods);
    CustomHotkeyList custom_hotkeys_for_group(CustomHotkeyGroup group);
    CustomHotkeyInfo custom_hotkey_info(CustomHotkeyID id);
    HotkeyCombo custom_hotkey_binding(CustomHotkeyGroup group, CustomHotkeyID id);
    bool is_default_binding(Hotkey hk);
    bool is_custom_hotkey_bound(CustomHotkeyID id);
    bool valid_match(HotkeyMatch c);
    bool is_match(HotkeyMatch c, Hotkey hk);
    bool is_match(HotkeyMatch c, CustomHotkeyID id);
    HotkeyMatchList bindings_for(Arena::Arena* arena, HotkeyCombo binding);

    // Custom hotkeys.
    void start_custom_hotkey_bindings();
    void update_custom_hotkey_info(CustomHotkeyID id, CustomHotkeyInfo info);
    CustomHotkeyID make_custom_hotkey(CustomHotkeyGroup group, HotkeyCombo binding);
    void remove_custom_hotkey(CustomHotkeyGroup group, CustomHotkeyID id);

    // Helpers.
    String8 hotkey_as_string(Arena::Arena* arena, OS::Key k, OS::KeyMods mods);
    String8 hotkey_as_string(Arena::Arena* arena, Hotkey hk);
    String8 hotkey_description(Hotkey hk);
    String8 hotkey_group_as_string(CustomHotkeyGroup group);
    String8 describe_match(HotkeyMatch match);
    void rebind(Hotkey hk, OS::Key k, OS::KeyMods mods);
    void rebind(CustomHotkeyID id, OS::Key k, OS::KeyMods mods);
    void clear_binding(Hotkey hk);
    void clear_binding(CustomHotkeyID id);
    void reset_to_default(Hotkey hk);
} // namespace Hotkeys