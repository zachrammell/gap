#include "feed.h"

#include <cassert>

#include "config.h"
#include "enum-utils.h"
#include "os.h"
#include "util.h"
#include "vec.h"

namespace Feed
{
    namespace
    {
        struct MessageData
        {
            MessageData* next;
            MessageData* prev;
            String8 message;
            uint64_t start;
            Vec4f color;

            // 5 seconds.
            static constexpr int message_lifetime = Thousand(5);
        };

        struct MessageDataList
        {
            MessageData* first;
            MessageData* last;
            uint64_t count;
        };

        Vec4f decay_message_color(const MessageData& data, uint64_t time)
        {
            auto new_color = data.color;
            // We want the messages to live for a prescribed amount of time.
            const auto final_time = data.start + MessageData::message_lifetime;
            // Your time has passed...
            if (final_time < time)
            {
                new_color.a = 0.f;
                return new_color;
            }
            const auto dt = final_time - time;
            float percent = 1.f - static_cast<float>(dt) / MessageData::message_lifetime;
            percent = std::min(1.f, percent);
            new_color.a = lerp(data.color.a, 0.f, percent);
            return new_color;
        }

        // Global data.
        MessageFeed* global_feed_instance;
    } // namespace [anon]

    struct MessageFeed::Data
    {
        Arena::Arena* msg_arena;
        MessageDataList msg_lst{};
        MessageData* free_list{};
        OS::Mutex thread_lst_mutex;
        MessageDataList thread_msg_lst{};
        Arena::Arena* thread_msg_arena;
    };

    namespace
    {
        void push_message(MessageFeed::Data* data, MessageData msg)
        {
            MessageData* node = nullptr;
            if (data->free_list != nullptr)
            {
                node = data->free_list;
                SLLStackPop(data->free_list);
            }
            else
            {
                node = Arena::push_array_no_zero<MessageData>(data->msg_arena, 1);
            }
            *node = msg;
            node->message = str8_copy(data->msg_arena, node->message);
            DLLPushFront(data->msg_lst.first, data->msg_lst.last, node);
            ++data->msg_lst.count;
        }

        void remove_message(MessageFeed::Data* data, MessageData* node)
        {
            assert(data->msg_lst.count != 0);
            DLLRemove(data->msg_lst.first, data->msg_lst.last, node);
            --data->msg_lst.count;
            node->next = nullptr;
            SLLStackPush(data->free_list, node);
        }

        void push_threaded_message(MessageFeed::Data* data, MessageData msg)
        {
            OS::lock_mutex(data->thread_lst_mutex);
            MessageData* node = Arena::push_array_no_zero<MessageData>(data->thread_msg_arena, 1);
            *node = msg;
            node->message = str8_copy(data->thread_msg_arena, node->message);
            DLLPushFront(data->thread_msg_lst.first, data->thread_msg_lst.last, node);
            ++data->thread_msg_lst.count;
            OS::unlock_mutex(data->thread_lst_mutex);
        }

        // Since this usually happens in batches, we assume the mutex lock is around the call to this function.
        void remove_threaded_message(MessageFeed::Data* data, MessageData* node)
        {
            assert(data->thread_msg_lst.count != 0);
            DLLRemove(data->thread_msg_lst.first, data->thread_msg_lst.last, node);
            --data->thread_msg_lst.count;
            node->next = nullptr;
        }

        void flush_threaded_messages(MessageFeed::Data* data)
        {
            // If there are threaded messages, we're going to purge them and queue them up.
            MessageData* node = data->thread_msg_lst.first;
            // No messages.
            if (node == nullptr)
                return;
            // Now we can lock and purge.
            OS::lock_mutex(data->thread_lst_mutex);
            while ((node = data->thread_msg_lst.first) != nullptr)
            {
                push_message(data, *node);
                remove_threaded_message(data, node);
            }
            // Clear the arena.
            Arena::clear(data->thread_msg_arena);
            data->thread_msg_lst = {};
            OS::unlock_mutex(data->thread_lst_mutex);
        }

        String8 prepend_tid(char* buf, uint64_t buf_size, String8 msg)
        {
            auto thread_id = OS::thread_id();
            String8 threaded_str = fmt_string(buf, buf_size, "tid[%u] %S", rep(thread_id), msg);
            return threaded_str;
        }
    } // namespace [anon]

    MessageFeed::MessageFeed():
        data{ new Data }
    {
        data->msg_arena = Arena::alloc(Arena::default_params);
        data->thread_msg_arena = Arena::alloc(Arena::default_params);
        data->thread_lst_mutex = OS::alloc_mutex();
    }

    MessageFeed::~MessageFeed() = default;

    void MessageFeed::queue_info(std::string_view message)
    {
        // Reap before we push_back to possibly avoid the allocation.
        reap();
        push_message(data.get(), { .message = str8_cppview(message),
                                   .start = rep(OS::get_ticks()),
                                   .color = Config::feed_colors().info });
    }

    void MessageFeed::queue_error(std::string_view error)
    {
        // Reap before we push_back to possibly avoid the allocation.
        reap();
        push_message(data.get(), { .message = str8_cppview(error),
                                   .start = rep(OS::get_ticks()),
                                   .color = Config::feed_colors().error });
    }

    void MessageFeed::queue_warning(std::string_view warning)
    {
        // Reap before we push_back to possibly avoid the allocation.
        reap();
        push_message(data.get(), { .message = str8_cppview(warning),
                                   .start = rep(OS::get_ticks()),
                                   .color = Config::feed_colors().warning });
    }

    void MessageFeed::queue_info(String8 message)
    {
        // Reap before we push_back to possibly avoid the allocation.
        reap();
        push_message(data.get(), { .message = message,
                                   .start = rep(OS::get_ticks()),
                                   .color = Config::feed_colors().info });
    }

    void MessageFeed::queue_error(String8 error)
    {
        // Reap before we push_back to possibly avoid the allocation.
        reap();
        push_message(data.get(), { .message = error,
                                   .start = rep(OS::get_ticks()),
                                   .color = Config::feed_colors().error });
    }

    void MessageFeed::queue_warning(String8 warning)
    {
        // Reap before we push_back to possibly avoid the allocation.
        reap();
        push_message(data.get(), { .message = warning,
                                   .start = rep(OS::get_ticks()),
                                   .color = Config::feed_colors().warning });
    }

    void MessageFeed::thread_safe_queue_info(String8 message)
    {
        // Prepend the thread id.
        constexpr size_t buf_size = 256;
        char buf[buf_size];
        String8 threaded_str = prepend_tid(buf, buf_size, message);
        push_threaded_message(data.get(), { .message = threaded_str,
                                            .start = rep(OS::get_ticks()),
                                            .color = Config::feed_colors().info });
    }

    void MessageFeed::thread_safe_queue_error(String8 error)
    {
        // Prepend the thread id.
        constexpr size_t buf_size = 256;
        char buf[buf_size];
        String8 threaded_str = prepend_tid(buf, buf_size, error);
        push_threaded_message(data.get(), { .message = threaded_str,
                                            .start = rep(OS::get_ticks()),
                                            .color = Config::feed_colors().error });
    }

    void MessageFeed::thread_safe_queue_warning(String8 warning)
    {
        // Prepend the thread id.
        constexpr size_t buf_size = 256;
        char buf[buf_size];
        String8 threaded_str = prepend_tid(buf, buf_size, warning);
        push_threaded_message(data.get(), { .message = threaded_str,
                                            .start = rep(OS::get_ticks()),
                                            .color = Config::feed_colors().warning });
    }

    void MessageFeed::build(CmdBuffer::DrawList* lst, Glyph::Atlas* atlas)
    {
        flush_threaded_messages(data.get());
        if (data->msg_lst.count == 0)
            return;
        // DO NOT reap() in the render loop!  This is performance-sensitive.
        const auto& state = Config::feed_state();
        const auto& diff_colors = Config::diff_colors();

        const auto clip = CmdBuffer::current_clip(*lst);

        Glyph::FontSize font_size{ state.feed_font_size };

        // Get the font context for the rendering loop.
        auto font_ctx = atlas->render_font_context(font_size);

        const auto ticks = rep(OS::get_ticks());

        constexpr float render_offset = 20.f;

        // Render each message.
        Vec2f pos = Vec2f(render_offset, render_offset);

        // Render the backgrounds for better readability when lots of text is
        // present in an editor view.
        CmdBuffer::start_shapes(lst, Render::VertShader::OneOneTransform);
        auto bg_pos = pos;
        // This helps the final rect to be positioned correctly (10% of the font size should wrap the
        // entire message).
        bg_pos.y = render_offset - rep(font_size) * 0.1f;
        Vec2f bg_size { 0.f, static_cast<float>(font_size) };
        for EachNode(msg, data->msg_lst.first)
        {
            // Compute the background width.
            bg_size.x = font_ctx.measure_text(msg->message).x;
            // Inherit the background from the editor for a nice fade effect.
            auto color = diff_colors.background;
            color.a = decay_message_color(*msg, ticks).a;
            CmdBuffer::solid_rect(lst, Render::FragShader::BasicColor, bg_pos, bg_size, color);
            bg_pos.y += rep(font_size);

            if (bg_pos.y > rep(clip.height))
                break;
        }

        // Render the text.
        CmdBuffer::start_glyph_run(lst, Render::VertShader::OneOneTransform);
        bool request_frames = false;
        for EachNode(msg, data->msg_lst.first)
        {
            auto color = decay_message_color(*msg, ticks);
            font_ctx.render_text(lst, msg->message, pos, color);
            pos.y += rep(font_size);
            request_frames = request_frames or color.a != 0.f;

            if (pos.y > rep(clip.height))
                break;
        }

        // Only check the newest message, but only if noisy events aren't enabled otherwise we'll spam with frame requests.
        if (request_frames
            and not (Config::system_core().noisy_events or Config::system_core().noisy_flattened_events))
        {
            Render::request_frames();
        }
    }

    void MessageFeed::reap()
    {
        const auto now = rep(OS::get_ticks());
        // Iterate the list in reverse removing messages until we reach a message
        // with an active lifetime.
        for (MessageData* node = data->msg_lst.last, *prev = nullptr;
            node != nullptr;
            node = prev)
        {
            prev = node->prev;
            if (now - node->start <= MessageData::message_lifetime)
                break;
            remove_message(data.get(), node);
        }
        // If the message count is 0, we will clear the arena and reclaim some memory, specifically for
        // the strings.
        if (data->msg_lst.count == 0)
        {
            data->msg_lst = {};
            data->free_list = {};
            Arena::clear(data->msg_arena);
            return;
        }
    }

    void global_feed(MessageFeed* feed)
    {
        global_feed_instance = feed;
    }

    MessageFeed* global_feed()
    {
        return global_feed_instance;
    }
} // namespace Feed