#include "widgets.h"

#include <bit>

#include "types.h"
#include "util.h"

namespace UI::Widgets
{
    namespace
    {
        constexpr auto generic_id_bits_start = std::bit_width(rep(ID::IdStart));

        ID chop_unique(uint64_t x)
        {
            // Chop bits from pre-defined variants.
            x = x << generic_id_bits_start;
            return ID{ x };
        }
    } // namespace [anon]

    ID make_id(std::string_view name)
    {
        HashInput in{};
        in.bytes = reinterpret_cast<const uint8_t*>(name.data());
        in.len = name.size();
        HashResult result{};
        if (not hash_bytes(in, &result))
            return ID::Zero;
        // Take the lower-half.
        auto id = result.result[0];
        return chop_unique(id);
    }

    ID make_id_seed(ID seed, std::string_view name)
    {
        // First, get a hash of 'name'.
        HashInput in{};
        in.bytes = reinterpret_cast<const uint8_t*>(name.data());
        in.len = name.size();
        HashResult result{};
        if (not hash_bytes(in, &result))
            return ID::Zero;
        // Now we can combine them into a new hash.
        struct Values
        {
            HashResult r1;
            ID r2;
        };

        Values values{ .r1 = result, .r2 = seed };
        in = as_hash_input(values);
        if (not hash_bytes(in, &result))
            return ID::Zero;
        // Take lower-half again.
        auto id = result.result[0];
        return chop_unique(id);
    }

    ID make_id_seed_idx(ID seed, uint64_t idx)
    {
        // First, get a hash of 'name'.
        HashInput in = as_hash_input(idx);
        HashResult result{};
        if (not hash_bytes(in, &result))
            return ID::Zero;
        // Now we can combine them into a new hash.
        struct Values
        {
            HashResult r1;
            ID r2;
        };

        Values values{ .r1 = result, .r2 = seed };
        in = as_hash_input(values);
        if (not hash_bytes(in, &result))
            return ID::Zero;
        // Take lower-half again.
        auto id = result.result[0];
        return chop_unique(id);
    }

    ID make_multi_seed(MultiSeed seed, std::string_view name)
    {
        struct Values
        {
            HashResult seed;
            HashResult name;
        };

        Values values{};
        HashInput in{};
        in.bytes = reinterpret_cast<const uint8_t*>(seed.first);
        in.len = (seed.last - seed.first) * sizeof(ID);
        if (not hash_bytes(in, &values.seed))
            return ID::Zero;
        // Hash the name.
        in.bytes = reinterpret_cast<const uint8_t*>(name.data());
        in.len = name.size();
        if (not hash_bytes(in, &values.name))
            return ID::Zero;
        in = as_hash_input(values);
        HashResult result{};
        if (not hash_bytes(in, &result))
            return ID::Zero;
        // Take the lower-half.
        auto id = result.result[0];
        return chop_unique(id);
    }
} // namespace UI::Widgets

// Widget implementations below.
#include "basic-box.cpp"
#include "basic-button.cpp"
#include "basic-checkbox.cpp"
#include "basic-colorbox.cpp"
#include "basic-colorpicker.cpp"
#include "basic-confirm.cpp"
#include "basic-listbox.cpp"
#include "basic-scrollbox.cpp"
#include "basic-textbox.cpp"
#include "basic-window.cpp"

#include "cmd-buffer-api.cpp"
#include "tooltips.cpp"