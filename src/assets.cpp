#include "assets.h"

#include "macros.h"

#define MINIZ_NO_STDIO
#define MINIZ_NO_DEFLATE_APIS
#include "miniz.h"

SUPPRESS_IF_CONSTEXPR_SUGGEST_WARNING();
#include "miniz.c"
ENABLE_IF_CONSTEXPR_SUGGEST_WARNING();

namespace Assets
{
    namespace
    {
        // Expand the assets here.
SUPPRESS_NONSTANDARD_EXTENSION_WARNING();
#include "gen-assets.h"
ENABLE_NONSTANDARD_EXTENSION_WARNING();

        struct InternalAssetDescription
        {
            String8View name;
            String8View proxy_file;
        };

        AssetDescription as_desc(const InternalAssetDescription& desc)
        {
            return { .name = str8_mut(desc.name), .proxy_file = str8_mut(desc.proxy_file) };
        }

        constinit InternalAssetDescription descriptions[] = {
#define DAT_ASSET(id, file) { .name = str8_literal(#id), .proxy_file = str8_literal(file) },
#include "assets.dat"
#undef DAT_ASSET
        };
    } // namespace [anon]

    AssetLength asset_length(AssetID id)
    {
        return AssetLength{ all_assets[rep(id)]->len };
    }

    AssetDescription describe(AssetID id)
    {
        return as_desc(descriptions[rep(id)]);
    }

    bool populate_asset(AssetBuffer in, AssetID id)
    {
        PROF_SCOPE();
        PROF_NAME_SCOPE("Asset %s", descriptions[rep(id)].name.str);
        const CompressedAsset* asset = all_assets[rep(id)];
        uLong uncmp_len = static_cast<uLong>(asset->len);
        uLong cmp_len = static_cast<uLong>(asset->cmp_len);
        auto status = uncompress(in.buf, &uncmp_len, asset->arr, cmp_len);
        if (status != Z_OK)
            return false;
        assert(uncmp_len == asset->len and asset->len <= in.len);
        return true;
    }
} // namespace Assets