#pragma once

#include <memory>

#include "thread-work.h"

namespace Thread
{
    template <Enum E>
    constexpr bool valid_handle(E handle)
    {
        return handle == E{};
    }

    enum class TaskDurationMS : uint64_t { };

    class ThreadPool
    {
    public:
        struct Data;

        ThreadPool();
        ~ThreadPool();

        // Init.
        void startup(Arena::Arena* arena);

        // Shutdown.
        void shutdown();

        // Async notification.
        void async_notify();

        // Queuing tasks.

        // Retrieving results.

        // Task cancellation.

        // Queries.
        uint64_t thread_count() const;
    private:
        std::unique_ptr<Data> data;
    };

    // Global setup.
    void set_system_thread_pool(ThreadPool* pool);

    // Global helpers.
    ThreadPool* system_thread_pool();
} // namespace Thread