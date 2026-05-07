#pragma once

#include <stdint.h>

#include <string_view>

#include "enum-utils.h"

namespace UI::Widgets
{
    enum class ID : uint64_t
    {
        Zero,
        // Define builtin IDs for large components we know about.
        Help,
        ConfigExplorer,
        ArenaReport,
        DiffPanel,
        DiffDirPanel,
        ConfirmDlg,

        IdStart,

        Sentinel = sentinel_for<ID>
    };

    struct MultiSeed
    {
        ID* first;
        ID* last;
    };

    ID make_id(std::string_view name);
    ID make_id_seed(ID seed, std::string_view name);
    ID make_id_seed_idx(ID seed, uint64_t idx);
    ID make_multi_seed(MultiSeed seed, std::string_view name);
} // namespace UI::Widgets