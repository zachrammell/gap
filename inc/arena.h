#pragma once

#include <algorithm>
#include <type_traits>

#include "gap-core.h"
#include "types.h"

namespace Arena
{
    enum class Flags : uint32_t
    {
        None       = 0,
        NoChain    = 1U << 0,
        LargePages = 1U << 1,
    };

    enum class ReserveSize : uint64_t { };
    enum class CommitSize : uint64_t { };
    enum class Position : uint64_t { };
    enum class AllocSize : uint64_t { };
    enum class Alignment : uint64_t { };

    enum class ZeroMem : bool { No, Yes };

    struct ArenaCreateParams
    {
        Flags flags = Flags::None;
        ReserveSize reserve_size = ReserveSize{ MB(64) };
        CommitSize commit_size = CommitSize{ KB(64) };
    };

    inline constexpr ArenaCreateParams default_params{};

    struct Arena;

    struct ArenaTrackerNode
    {
        ArenaTrackerNode* next;
        ArenaTrackerNode* prev;
        Arena* arena;
        const char* alloc_file;
        uint64_t alloc_file_len;
        int line;
        Position peak_pos;
        const char* peak_file;
        uint64_t peak_file_len;
        int peak_line;
    };

    struct ArenaTrackerList
    {
        ArenaTrackerNode* first;
        ArenaTrackerNode* last;
        ArenaTrackerNode* free_lst;
        uint64_t count;
    };

    struct Arena
    {
        Arena* prev;
        Arena* current;
#ifdef BUILD_TRACK_ARENA
        ArenaTrackerNode* tracker_node;
#endif // BUILD_TRACK_ARENA
        Flags flags;
        CommitSize req_cmt_size;  // Requested commit size.
        ReserveSize req_res_size; // Requested reserve size.
        Position base_pos;
        Position pos;
        CommitSize os_cmt; // Computed commit size for OS.
        ReserveSize os_res; // Computed reserve size for the OS.
    };

    struct ArenaTrackerSnapshotEntry
    {
        Arena arena_snapshot;
        const char* alloc_file;
        uint64_t alloc_file_len;
        int line;
        Position peak_pos;
        const char* peak_file;
        uint64_t peak_file_len;
        int peak_line;
    };

    struct ArenaTrackerSnapshot
    {
        ArenaTrackerSnapshotEntry* elements;
        uint64_t size;
    };

#if BUILD_DEBUG
    struct ScratchTrackerNode
    {
        ScratchTrackerNode* next;
        ScratchTrackerNode* prev;
        const char* file;
        int line;
    };

    struct ScratchTrackerList
    {
        ScratchTrackerNode* first;
        ScratchTrackerNode* last;
        uint64_t count;
    };
#endif // BUILD_DEBUG

    struct Temp
    {
        Arena* arena;
        Position pos;
#if BUILD_DEBUG
        ScratchTrackerNode* tracker;
#endif // BUILD_DEBUG
    };

    struct Conflicts
    {
        Arena** conflicts;
        uint64_t count;
    };

    inline constexpr Conflicts no_conflicts = {};

#ifdef BUILD_TRACK_ARENA
    // Tracker APIs.
    void init_tracker_arena();
    void init_tracker_mutex();
#endif // BUILD_TRACK_ARENA
    ArenaTrackerSnapshot arena_tracker_snapshot(Arena* arena);
    bool tracker_dirty();
    void mark_tracker_clean();

    // Arena creation/destruction.
#ifdef BUILD_TRACK_ARENA
    Arena* alloc(ArenaCreateParams params, const char* file = __builtin_FILE(), int line = __builtin_LINE());
#else
    Arena* alloc(ArenaCreateParams params);
#endif // BUILD_TRACK_ARENA
    void release(Arena* arena);

    // Basic push/pop core functions.
#ifdef BUILD_TRACK_ARENA
    void* push(Arena* arena, AllocSize size, Alignment align, ZeroMem zero, const char* file = __builtin_FILE(), int line = __builtin_LINE());
#else
    void* push(Arena* arena, AllocSize size, Alignment align, ZeroMem zero);
#endif // BUILD_TRACK_ARENA
    Position pos(const Arena* arena);
    void pop_to(Arena* arena, Position pos);

    // Push/pop helpers.
    void clear(Arena* arena);
    void pop(Arena* arena, AllocSize size);

    // Decommit helpers.
    void shrink_cmt_to_pos(Arena* arena);

    // Temporary arena allocation.
    Temp temp_begin(Arena* arena);
    void temp_end(Temp temp);

    // (Related to above) temporary per-thread scratch arenas.
    Temp scratch_begin(Conflicts conflicts, const char* file = __builtin_FILE(), int line = __builtin_LINE());
    void scratch_end(Temp scratch);
    void validate_scratch_arenas();

    // Typed helper functions.
#ifdef BUILD_TRACK_ARENA
    template <typename T>
    T* push_array_no_zero_aligned(Arena* arena, size_t count, Alignment align, const char* file = __builtin_FILE(), int line = __builtin_LINE())
    {
        static_assert(std::is_trivially_destructible_v<T>);
        return static_cast<T*>(push(arena, AllocSize{ sizeof(T)*count }, align, ZeroMem::No, file, line));
    }

    template <typename T>
    T* push_array_aligned(Arena* arena, size_t count, Alignment align, const char* file = __builtin_FILE(), int line = __builtin_LINE())
    {
        static_assert(std::is_trivially_destructible_v<T>);
        return static_cast<T*>(push(arena, AllocSize{ sizeof(T)*count }, align, ZeroMem::Yes, file, line));
    }

    template <typename T>
    T* push_array_no_zero(Arena* arena, size_t count, const char* file = __builtin_FILE(), int line = __builtin_LINE())
    {
        return push_array_no_zero_aligned<T>(arena, count, std::max(Alignment{ 8 }, Alignment{ alignof(T) }), file, line);
    }

    template <typename T>
    T* push_array(Arena* arena, size_t count, const char* file = __builtin_FILE(), int line = __builtin_LINE())
    {
        return push_array_aligned<T>(arena, count, std::max(Alignment{ 8 }, Alignment{ alignof(T) }), file, line);
    }
#else
    template <typename T>
    T* push_array_no_zero_aligned(Arena* arena, size_t count, Alignment align)
    {
        static_assert(std::is_trivially_destructible_v<T>);
        return static_cast<T*>(push(arena, AllocSize{ sizeof(T)*count }, align, ZeroMem::No));
    }

    template <typename T>
    T* push_array_aligned(Arena* arena, size_t count, Alignment align)
    {
        static_assert(std::is_trivially_destructible_v<T>);
        return static_cast<T*>(push(arena, AllocSize{ sizeof(T)*count }, align, ZeroMem::Yes));
    }

    template <typename T>
    T* push_array_no_zero(Arena* arena, size_t count)
    {
        return push_array_no_zero_aligned<T>(arena, count, std::max(Alignment{ 8 }, Alignment{ alignof(T) }));
    }

    template <typename T>
    T* push_array(Arena* arena, size_t count)
    {
        return push_array_aligned<T>(arena, count, std::max(Alignment{ 8 }, Alignment{ alignof(T) }));
    }
#endif // BUILD_TRACK_ARENA
} // namespace Arena