#pragma once

#include "gap-strings.h"
#include "types.h"

namespace Assets
{
    enum class AssetID
    {
#define DAT_ASSET(id, file) id,
#include "assets.dat"
#undef DAT_ASSET
        Invalid,
        Count
    };

    enum class AssetLength : uint64_t { };

    struct AssetBuffer
    {
        unsigned char* buf;
        uint64_t len;
    };

    struct AssetDescription
    {
        String8 name;
        String8 proxy_file;
    };

    AssetLength asset_length(AssetID id);
    AssetDescription describe(AssetID id);
    bool populate_asset(AssetBuffer in, AssetID id);
} // namespace Assets