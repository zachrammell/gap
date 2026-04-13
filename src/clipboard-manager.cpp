#include "clipboard-manager.h"

#include <cassert>

#include <algorithm>
#include <string>

#include "feed.h"
#include "os.h"
#include "scoped-handle.h"

namespace Clipboard
{
    namespace
    {
        struct ClipboardData
        {
            String8 cached_clip = str8_empty;
            bool in_error = true;  // Assume unpopulated.
        };

        bool data_valid(const ClipboardData& data)
        {
            return not data.in_error;
        }

        void strip_crlf(String8* str)
        {
            auto last = std::remove(str->str, str->str + str->size, '\r');
            str->size = static_cast<uint64_t>(last - str->str);
        }

        void cache_clipboard(Arena::Arena* arena, ClipboardData* data, Feed::MessageFeed* feed)
        {
            data->cached_clip = str8_empty;
            auto err = OS::clipboard_text(arena, &data->cached_clip);
            data->in_error = err != OS::Error::None;
            if (err != OS::Error::None)
            {
                feed->queue_error(OS::error_text(err));
            }
        }

        bool set_clipboard(String8 buf, Feed::MessageFeed* feed)
        {
            auto err = OS::set_clipboard(buf);
            if (err != OS::Error::None)
            {
                feed->queue_error(OS::error_text(err));
                return false;
            }
            return true;
        }

        bool set_clipboard_and_html(String8 buf, String8 html, Feed::MessageFeed* feed)
        {
            auto err = OS::set_clipboard_html(buf, html);
            if (err != OS::Error::None)
            {
                feed->queue_error(OS::error_text(err));
                return false;
            }
            return true;
        }
    } // namespace [anon]

    struct BufferMeta { };

    struct ClipboardManager::Data
    {
        OS::ClipboardIdentity id = OS::ClipboardIdentity::Sentinel;
        Arena::Arena* arena = nullptr;
        ClipboardData clipboard;
    };

    ClipboardManager::ClipboardManager():
        data{ new Data }
    {
        data->arena = Arena::alloc(Arena::default_params);
    }

    ClipboardManager::~ClipboardManager()
    {
        Arena::release(data->arena);
        data->arena = nullptr;
        data->clipboard.cached_clip = str8_empty;
    }

    // Queries.
    bool ClipboardManager::has_meta() const
    {
        return OS::clipboard_id() == data->id;
    }

    String8 ClipboardManager::clipboard_data(Arena::Arena* arena, Feed::MessageFeed* feed, RemoveCRLF remove_crlf)
    {
        String8 result{};
        auto current = OS::clipboard_id();
        if (data->id == current)
        {
            // It's possible we haven't populated the clipboard data yet.  Let's populate it.
            if (not data_valid(data->clipboard))
            {
                // Clear the previous arena data.
                Arena::clear(data->arena);
                cache_clipboard(data->arena, &data->clipboard, feed);
            }
            result = str8_copy(arena, data->clipboard.cached_clip);
            if (is_yes(remove_crlf))
            {
                strip_crlf(&result);
            }
            return result;
        }
        // Clear the previous arena data.
        Arena::clear(data->arena);
        cache_clipboard(data->arena, &data->clipboard, feed);
        result = str8_copy(arena, data->clipboard.cached_clip);
        if (is_yes(remove_crlf))
        {
            strip_crlf(&result);
        }
        data->id = current;
        return result;
    }

    bool ClipboardManager::set_clipboard(String8 buf, Feed::MessageFeed* feed)
    {
        if (::Clipboard::set_clipboard(buf, feed))
        {
            data->id = OS::clipboard_id();
            // Clear the old data so it can be reset on 'clipboard_data'.
            data->clipboard = ClipboardData{ };
            return true;
        }
        return false;
    }

    bool ClipboardManager::set_clipboard_and_html(String8 buf, String8 html, Feed::MessageFeed* feed)
    {
        if (::Clipboard::set_clipboard_and_html(buf, html, feed))
        {
            data->id = OS::clipboard_id();
            // Clear the old data so it can be reset on 'clipboard_data'.
            data->clipboard = ClipboardData{ };
            return true;
        }
        return false;
    }
} // namespace Clipboard