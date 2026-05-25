#include "arena.h"

#include <cassert>

#include "macros.h"
#include "os.h"
#include "thread-ctx.h"
#include "util.h"

namespace Arena
{
    namespace
    {
        template <typename T>
        constexpr T align_pow_2(T x, T y)
        {
            return (x + y - 1) & (~(y - 1));
        }

        // The actual arena header is much smaller than this, but skipping more bytes guarantees we can
        // add some more meta info about the header later.
        constexpr uint64_t arena_header = 128;
        static_assert(sizeof(Arena) <= arena_header);

#if BUILD_DEBUG
        ScratchTrackerNode* push_tracker_node(Thread::TLD* tld, const char* file, int line)
        {
            ScratchTrackerNode* node = push_array<ScratchTrackerNode>(tld->scratch_track_arena, 1);
            node->file = file;
            node->line = line;
            DLLPushBack(tld->scratch_track_lst.first, tld->scratch_track_lst.last, node);
            ++tld->scratch_track_lst.count;
            return node;
        }

        void release_tracker_node(Thread::TLD* tld, ScratchTrackerNode* node)
        {
            DLLRemove(tld->scratch_track_lst.first, tld->scratch_track_lst.last, node);
            --tld->scratch_track_lst.count;
        }
#endif // BUILD_DEBUG

        Arena* alloc_internal(ArenaCreateParams params)
        {
            // Ensure that we round up allocations to keep sizes within powers of 2.
            auto reserve_size = params.reserve_size;
            auto commit_size = params.commit_size;
            if (implies(params.flags, Flags::LargePages))
            {
                reserve_size = ReserveSize{ align_pow_2(rep(reserve_size), rep(OS::system_info()->large_page_size)) };
                commit_size = CommitSize{ align_pow_2(rep(commit_size), rep(OS::system_info()->large_page_size)) };
            }
            else
            {
                reserve_size = ReserveSize{ align_pow_2(rep(reserve_size), rep(OS::system_info()->page_size)) };
                commit_size = CommitSize{ align_pow_2(rep(commit_size), rep(OS::system_info()->page_size)) };
            }
            void* base = nullptr;
            if (implies(params.flags, Flags::LargePages))
            {
                base = OS::mem_reserve_large(OS::AllocationSize{ rep(reserve_size) });
                OS::mem_commit_large(base, OS::AllocationSize{ rep(commit_size) });
            }
            else
            {
                base = OS::mem_reserve(OS::AllocationSize{ rep(reserve_size) });
                OS::mem_commit(base, OS::AllocationSize{ rep(commit_size) });
            }
            // In the off chance that the OS decided to reuse this memory region, we must unpoison it first prior to writing to it.
            ASAN_UNPOISON_MEMORY_REGION(base, arena_header);
            Arena* arena = reinterpret_cast<Arena*>(base);
            arena->prev = nullptr;
            arena->current = arena;
            arena->flags = params.flags;
            arena->req_cmt_size = params.commit_size;
            arena->req_res_size = params.reserve_size;
            arena->base_pos = Position{ 0 };
            arena->pos = Position{ arena_header };
            arena->os_cmt = commit_size;
            arena->os_res = reserve_size;
            ASAN_POISON_MEMORY_REGION(base, rep(commit_size));
            ASAN_UNPOISON_MEMORY_REGION(base, arena_header);
            return arena;
        }

        void* push_internal(Arena* arena, AllocSize size, Alignment align, ZeroMem zero)
        {
            Arena* current = arena->current;
            Position pos_pre = Position{ align_pow_2(rep(current->pos), rep(align)) };
            Position pos_post = extend(pos_pre, rep(size));
            // Chain, if necessary.
            if (rep(current->os_res) < rep(pos_post) and not (implies(arena->flags, Flags::NoChain)))
            {
                Arena* new_blk = nullptr;
                ReserveSize res_size = current->req_res_size;
                CommitSize cmt_size = current->req_cmt_size;
                if (rep(size) + arena_header > rep(res_size))
                {
                    res_size = ReserveSize{ align_pow_2(rep(size) + arena_header, rep(align)) };
                    cmt_size = CommitSize{ align_pow_2(rep(size) + arena_header, rep(align)) };
                }
                ArenaCreateParams params{
                    .flags = current->flags,
                    .reserve_size = res_size,
                    .commit_size = cmt_size
                };
                new_blk = alloc(params);
                new_blk->base_pos = extend(current->base_pos, rep(current->os_res));
                SLLStackPush_N(arena->current, new_blk, prev);

                current = new_blk;
                // Recompute the position based on the current.
                pos_pre = Position{ align_pow_2(rep(current->pos), rep(align)) };
                pos_post = extend(pos_pre, rep(size));
            }
            // Now we figure out what the zero target is.
            uint64_t size_to_zero = 0;
            if (is_yes(zero))
            {
                size_to_zero = std::min(rep(current->os_cmt), rep(pos_post)) - rep(pos_pre);
            }

            // Commit new pages if necessary.
            if (rep(current->os_cmt) < rep(pos_post))
            {
                uint64_t cmt_post_aligned = rep(pos_post) + rep(current->req_cmt_size) - 1;
                cmt_post_aligned -= cmt_post_aligned % rep(current->req_cmt_size);
                uint64_t cmt_post_clamped = std::min(cmt_post_aligned, rep(current->os_res));
                uint64_t cmt_size = cmt_post_clamped - rep(current->os_cmt);
                uint8_t* cmt_ptr = reinterpret_cast<uint8_t*>(current) + rep(current->os_cmt);
                if (implies(current->flags, Flags::LargePages))
                {
                    OS::mem_commit_large(cmt_ptr, OS::AllocationSize{ cmt_size });
                }
                else
                {
                    OS::mem_commit(cmt_ptr, OS::AllocationSize{ cmt_size });
                }
                current->os_cmt = CommitSize{ cmt_post_clamped };
            }
            // Push onto current block.
            void* result = nullptr;
            if (rep(current->os_cmt) >= rep(pos_post))
            {
                result = reinterpret_cast<uint8_t*>(current) + rep(pos_pre);
                current->pos = pos_post;
                ASAN_UNPOISON_MEMORY_REGION(result, rep(size));
                if (size_to_zero != 0)
                {
                    memset(result, 0, size_to_zero);
                }
            }
            return result;
        }
#ifdef BUILD_TRACK_ARENA
        // Globals.
        Arena* track_arena;
        ArenaTrackerList track_lst;
        // We need a mutex to synchronize these mutations.
        OS::Mutex track_mtx = OS::Mutex::Sentinel;
        bool tracker_dirty_inst;

        void lock_lst_if_avail()
        {
            if (track_mtx != OS::Mutex::Sentinel)
            {
                OS::lock_mutex(track_mtx);
            }
        }

        void unlock_lst_if_avail()
        {
            if (track_mtx != OS::Mutex::Sentinel)
            {
                OS::unlock_mutex(track_mtx);
            }
        }

        void arena_tracker_push_alloc(Arena* alloc_arena, const char* file, int line)
        {
            lock_lst_if_avail();
            ArenaTrackerNode* node = track_lst.free_lst;
            if (node != nullptr)
            {
                SLLStackPop(track_lst.free_lst);
                zero_bytes(node);
            }
            else
            {
                // Note: We use the raw push_internal here so we do not track ourselves.
                node = static_cast<ArenaTrackerNode*>(push_internal(track_arena,
                                                                    AllocSize{ sizeof(ArenaTrackerNode) },
                                                                    Alignment{ alignof(ArenaTrackerNode) },
                                                                    ZeroMem::Yes));
            }
            node->arena = alloc_arena;
            node->alloc_file = file;
            node->alloc_file_len = strlen(file);
            node->line = line;
            node->peak_pos = pos(alloc_arena);
            node->peak_file = file;
            node->peak_file_len = node->alloc_file_len;
            node->peak_line = line;
            DLLPushBack(track_lst.first, track_lst.last, node);
            ++track_lst.count;
            // Write the node back to the arena.
            alloc_arena->tracker_node = node;
            tracker_dirty_inst = true;
            unlock_lst_if_avail();
        }

        void arena_tracker_pop_alloc(Arena* release_arena)
        {
            lock_lst_if_avail();
            ArenaTrackerNode* node = release_arena->tracker_node;
            assert(node != nullptr);
            DLLRemove(track_lst.first, track_lst.last, node);
            --track_lst.count;
            SLLStackPush(track_lst.free_lst, node);
            tracker_dirty_inst = true;
            unlock_lst_if_avail();
        }

        void arena_tracker_post_push(Arena* arena, const char* file, int line)
        {
            lock_lst_if_avail();
            ArenaTrackerNode* node = arena->tracker_node;
            if (node->peak_pos < pos(arena))
            {
                node->peak_pos = pos(arena);
                node->peak_file = file;
                node->peak_file_len = strlen(file);
                node->peak_line = line;
                tracker_dirty_inst = true;
            }
            unlock_lst_if_avail();
        }
#endif // BUILD_TRACK_ARENA
    } // namespace [anon]

#ifdef BUILD_TRACK_ARENA
    // Tracker APIs.
    void init_tracker_arena()
    {
        track_arena = alloc_internal(default_params);
    }

    void init_tracker_mutex()
    {
        track_mtx = OS::alloc_mutex();
    }
#endif // BUILD_TRACK_ARENA
    // Ensure that the range of bytes between these elements is consistent so we can blit the memory.
    static_assert(offsetof(ArenaTrackerNode, peak_line) - offsetof(ArenaTrackerNode, alloc_file)
                == offsetof(ArenaTrackerSnapshotEntry, peak_line) - offsetof(ArenaTrackerSnapshotEntry, alloc_file));
    ArenaTrackerSnapshot arena_tracker_snapshot(Arena* arena)
    {
        ArenaTrackerSnapshot snap{};
#ifdef BUILD_TRACK_ARENA
        // Lock all the arenas while we build the snapshot.
        lock_lst_if_avail();
        snap.size = track_lst.count;
        // Allocate using internal API so we do not try to lock again.
        snap.elements = static_cast<ArenaTrackerSnapshotEntry*>(push_internal(arena,
                                                                           AllocSize{ sizeof(ArenaTrackerSnapshotEntry) * snap.size },
                                                                           Alignment{ alignof(ArenaTrackerSnapshotEntry) },
                                                                           ZeroMem::No));
        uint64_t i = 0;
        for EachNode(n, track_lst.first)
        {
            ArenaTrackerSnapshotEntry* buf = &snap.elements[i++];
            memcpy(&buf->arena_snapshot, n->arena, sizeof(Arena));
            constexpr uint64_t off_node_start = offsetof(ArenaTrackerNode, alloc_file);
            constexpr uint64_t off_element_start = offsetof(ArenaTrackerSnapshotEntry, alloc_file);
            constexpr uint64_t len = offsetof(ArenaTrackerNode, peak_line) - off_node_start + sizeof(int);
            memcpy(reinterpret_cast<uint8_t*>(buf) + off_element_start, reinterpret_cast<uint8_t*>(n) + off_node_start, len);
        }
        unlock_lst_if_avail();
#else
        (void)arena;
#endif
        return snap;
    }

    bool tracker_dirty()
    {
#ifdef BUILD_TRACK_ARENA
        return tracker_dirty_inst;
#else
        return false;
#endif // BUILD_TRACK_ARENA
    }

    void mark_tracker_clean()
    {
#ifdef BUILD_TRACK_ARENA
        tracker_dirty_inst = false;
#endif // BUILD_TRACK_ARENA
    }

    // Arena creation/destruction.
#ifdef BUILD_TRACK_ARENA
    Arena* alloc(ArenaCreateParams params, const char* file, int line)
    {
        Arena* arena = alloc_internal(params);
        arena_tracker_push_alloc(arena, file, line);
        return arena;
    }
#else
    Arena* alloc(ArenaCreateParams params)
    {
        return alloc_internal(params);
    }
#endif // BUILD_TRACK_ARENA

    void release(Arena* arena)
    {
        for (Arena* a = arena->current, *prev = nullptr; a != nullptr; a = prev)
        {
#if BUILD_TRACK_ARENA
            arena_tracker_pop_alloc(a);
#endif // BUILD_TRACK_ARENA
            prev = a->prev;
            OS::mem_release(a, OS::AllocationSize{ rep(a->os_res) });
        }
    }

    // Basic push/pop core functions.
#ifdef BUILD_TRACK_ARENA
    void* push(Arena* arena, AllocSize size, Alignment align, ZeroMem zero, const char* file, int line)
    {
        void* result = push_internal(arena, size, align, zero);
        arena_tracker_post_push(arena, file, line);
        return result;
    }
#else
    void* push(Arena* arena, AllocSize size, Alignment align, ZeroMem zero)
    {
        return push_internal(arena, size, align, zero);
    }
#endif // BUILD_TRACK_ARENA

    Position pos(const Arena* arena)
    {
        const Arena* current = arena->current;
        return extend(current->base_pos, rep(current->pos));
    }

    void pop_to(Arena* arena, Position pos)
    {
        Position big_pos = Position{ std::max(arena_header, rep(pos)) };
        Arena* current = arena->current;
        for (Arena* prev = nullptr; current->base_pos >= big_pos; current = prev)
        {
#if BUILD_TRACK_ARENA
            arena_tracker_pop_alloc(current);
#endif // BUILD_TRACK_ARENA
            prev = current->prev;
            OS::mem_release(current, OS::AllocationSize{ rep(current->os_res) });
        }
        arena->current = current;
        Position new_pos = Position{ rep(big_pos) - rep(current->base_pos) };
        assert(new_pos <= current->pos);
        ASAN_POISON_MEMORY_REGION(reinterpret_cast<uint8_t*>(current) + rep(new_pos), (rep(current->pos) - rep(new_pos)));
        current->pos = new_pos;
    }

    // Push/pop helpers.
    void clear(Arena* arena)
    {
        pop_to(arena, Position{ 0 });
    }

    void pop(Arena* arena, AllocSize size)
    {
        Position old_pos = pos(arena);
        Position new_pos = old_pos;
        if (rep(size) < rep(old_pos))
        {
            new_pos = Position{ rep(old_pos) - rep(size) };
        }
        pop_to(arena, new_pos);
    }

    // Decommit helpers.
    void shrink_cmt_to_pos(Arena* arena)
    {
        uint64_t commits_per = rep(arena->pos) / rep(arena->req_cmt_size);
        uint64_t commits_per_rem = (rep(arena->req_cmt_size) * commits_per - rep(arena->pos)) != 0;
        CommitSize rem_commit = CommitSize{ (commits_per + commits_per_rem) * rep(arena->req_cmt_size) };
        if (implies(arena->flags, Flags::LargePages))
        {
            rem_commit = CommitSize{ align_pow_2(rep(rem_commit), rep(OS::system_info()->large_page_size)) };
        }
        else
        {
            rem_commit = CommitSize{ align_pow_2(rep(rem_commit), rep(OS::system_info()->page_size)) };
        }
        OS::AllocationSize cmt_spare = OS::AllocationSize{ rep(arena->os_cmt) - rep(rem_commit) };
        if (rep(cmt_spare) != 0)
        {
            OS::mem_decommit(reinterpret_cast<uint8_t*>(arena->current) + rep(rem_commit), cmt_spare);
            arena->os_cmt = rem_commit;
        }
    }

    // Temporary arena allocation.
    Temp temp_begin(Arena* arena)
    {
        Temp temp = {
            .arena = arena,
            .pos = pos(arena)
        };
        return temp;
    }

    void temp_end(Temp temp)
    {
        pop_to(temp.arena, temp.pos);
    }

    // (Related to above) temporary per-thread scratch arenas.
    Temp scratch_begin(Conflicts conflicts, const char* file, int line)
    {
        // This is per-thread (largely borrowed from RADDBG).
        Thread::TLD* tld = Thread::tld_selected();
        Arena* result = nullptr;
        Arena** arena_ptr = tld->arenas;
        for(uint64_t i = 0; i < std::size(tld->arenas); ++i, ++arena_ptr)
        {
            Arena** conflict_ptr = conflicts.conflicts;
            bool has_conflict = false;
            for(uint64_t j = 0; j < conflicts.count; ++j, ++conflict_ptr)
            {
                if(*arena_ptr == *conflict_ptr)
                {
                    has_conflict = true;
                    break;
                }
            }
            if(not has_conflict)
            {
                result = *arena_ptr;
                break;
            }
        }
        Temp tmp = temp_begin(result);
#if BUILD_DEBUG
        // Add some debug info.
        ScratchTrackerNode* node = push_tracker_node(tld, file, line);
        tmp.tracker = node;
#else
        GAP_UNUSED(file);
        GAP_UNUSED(line);
#endif // BUILD_DEBUG
        return tmp;
    }

    void scratch_end(Temp scratch)
    {
#if BUILD_DEBUG
        Thread::TLD* tld = Thread::tld_selected();
        release_tracker_node(tld, scratch.tracker);
#endif // BUILD_DEBUG
        shrink_cmt_to_pos(scratch.arena);
        temp_end(scratch);
    }

#if BUILD_DEBUG
    void validate_scratch_arenas()
    {
        Thread::TLD* tld = Thread::tld_selected();
        if (tld->scratch_track_lst.count != 0)
        {
            for EachNode(n, tld->scratch_track_lst.first)
            {
                fprintf(stderr, "Unreleased scratch arena: %s(%d)\n", n->file, n->line);
            }
            assert(false);
        }
        clear(tld->scratch_track_arena);
    }
#endif
} // namespace Arena
