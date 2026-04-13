#pragma once

#include <memory>
#include <string_view>

#include "arena.h"
#include "gap-strings.h"

namespace Feed
{
    class MessageFeed;
} // namespace Feed

namespace Clipboard
{
    enum class RemoveCRLF : bool { No, Yes };

    class ClipboardManager
    {
    public:
        struct Data;

        ClipboardManager();
        ~ClipboardManager();

        // Queries.
        bool has_meta() const;
        String8 clipboard_data(Arena::Arena* arena, Feed::MessageFeed* feed, RemoveCRLF remove_crlf);

        // Setters.
        bool set_clipboard(String8 buf, Feed::MessageFeed* feed);
        bool set_clipboard_and_html(String8 buf, String8 html, Feed::MessageFeed* feed);

    private:
        std::unique_ptr<Data> data;
    };
} // namespace Clipboard