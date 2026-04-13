#pragma once

#include <memory>
#include <string_view>

#include "cmd-buffer-api.h"
#include "glyph-cache.h"
#include "renderer.h"
#include "types.h"

namespace Feed
{
    class MessageFeed
    {
    public:
        struct Data;

        MessageFeed();
        ~MessageFeed();

        void queue_info(std::string_view message);
        void queue_error(std::string_view error);
        void queue_warning(std::string_view warning);

        void queue_info(String8 message);
        void queue_error(String8 error);
        void queue_warning(String8 warning);

        void thread_safe_queue_info(String8 message);
        void thread_safe_queue_error(String8 error);
        void thread_safe_queue_warning(String8 warning);

        void build(CmdBuffer::DrawList* lst, Glyph::Atlas* atlas);
    private:
        void reap();

        std::unique_ptr<Data> data;
    };

    // Intended for use outside the main thread.
    void global_feed(MessageFeed* feed);
    MessageFeed* global_feed();
} // namespace Feed