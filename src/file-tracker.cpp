#include "file-tracker.h"

#include <cassert>

#include "gap-bits.h"
#include "os.h"
#include "util.h"

namespace FileTrack
{
    namespace
    {
        MonitorStripe* stripe_idx_from_slot_idx(MonitorStripeArray* array, uint64_t idx)
        {
            return &array->stripes[idx % array->count];
        }

        // Globals.
        MonitoredFileMap* async_map;
    } // namespace [anon]

    struct MonitoredFileEntry
    {
        MonitoredFileEntry* next;
        char path[512];
        uint64_t path_len;
        uint64_t gen;
        OS::DenseTime last_modified;
    };

    // Creation.
    MonitoredFileMap make_map_file(Arena::Arena* arena)
    {
        MonitoredFileMap result{};
        // Create the stripe array first.
        result.stripes.count = std::max(1U, rep(OS::system_info()->processor_count));
        result.stripes.stripes = Arena::push_array<MonitorStripe>(arena, result.stripes.count);
        for EachIndex(i, result.stripes.count)
        {
            result.stripes.stripes[i].mutex = OS::alloc_mutex();
        }
        const uint64_t pow_2_aligned_size = up_pow2(256);
        result.capacity = pow_2_aligned_size;
        result.nil_buckets = result.capacity;
        result.buckets = Arena::push_array<MonitoredFileEntry*>(arena, result.capacity);
        return result;
    }

    // Mapping files.
    MappedFile map_file_map_path(Arena::Arena* arena, MonitoredFileMap* map, String8 path)
    {
        MappedFile result{};
        auto scratch = Arena::scratch_begin({ &arena, 1 });
        String8 abs_path = str8_empty;
        OS::Error err = OS::canonical_file_path(scratch.arena, &abs_path, path);
        if (err != OS::Error::None)
        {
            // Should report an error or something?
            abs_path = path;
        }
        HashResult hash;
        hash_str8(abs_path, &hash);
        uint64_t idx = (map->capacity - 1) & hash.result[0];
        MonitorStripe* stripe = stripe_idx_from_slot_idx(&map->stripes, idx);
        // Lock the mutex for this stripe and start insertion.
        OS::lock_mutex(stripe->mutex);
        MonitoredFileEntry* slot = map->buckets[idx];
        if (slot == nullptr)
        {
            if (stripe->free_list != nullptr)
            {
                slot = stripe->free_list;
                SLLStackPop(stripe->free_list);
                zero_bytes(slot);
            }
            else
            {
                slot = Arena::push_array<MonitoredFileEntry>(arena, 1);
            }
            // Copy the string.
            uint64_t len = std::min(static_cast<uint64_t>(std::size(slot->path) - 1), abs_path.size);
            memcpy(slot->path, abs_path.str, len);
            slot->path[len + 1] = 0;
            slot->path_len = len;
            slot->last_modified = OS::file_properties(abs_path).modified;

            map->buckets[idx] = slot;
            ++map->count;
            --map->nil_buckets;
            map->load = std::max(map->load, uint64_t(1));
        }
        else
        {
            uint64_t elms = 0;
            bool insert = true;
            while (slot->next != nullptr)
            {
                if (str8_match_exact(str8(slot->path, slot->path_len), abs_path))
                {
                    insert = false;
                    break;
                }
                ++elms;
                slot = slot->next;
            }

            if (str8_match_exact(str8(slot->path, slot->path_len), abs_path))
            {
                insert = false;
            }

            if (insert)
            {
                MonitoredFileEntry* new_entry = nullptr;
                if (stripe->free_list)
                {
                    new_entry = stripe->free_list;
                    SLLStackPop(stripe->free_list);
                    zero_bytes(new_entry);
                }
                else
                {
                    new_entry = Arena::push_array<MonitoredFileEntry>(arena, 1);
                }
                // Copy the string.
                uint64_t len = std::min(static_cast<uint64_t>(std::size(new_entry->path) - 1), abs_path.size);
                memcpy(new_entry->path, abs_path.str, len);
                new_entry->path[len + 1] = 0;
                new_entry->path_len = len;
                new_entry->last_modified = OS::file_properties(abs_path).modified;

                slot->next = new_entry;
                slot = new_entry;
                ++map->count;
                elms += 2; // +1 for current slot and +1 for new node.
                map->load = std::max(map->load, elms);
            }
        }
        result.abs_path = str8(slot->path, slot->path_len);
        result.gen = FileGen{ slot->gen };
        OS::unlock_mutex(stripe->mutex);
        Arena::scratch_end(scratch);
        return result;
    }

    bool map_file_fetch(MappedFile* mapped, MonitoredFileMap* map, String8 path)
    {
        bool result = false;
        auto scratch = Arena::scratch_begin(Arena::no_conflicts);
        String8 abs_path = str8_empty;
        OS::Error err = OS::canonical_file_path(scratch.arena, &abs_path, path);
        if (err != OS::Error::None)
        {
            // Should report an error or something?
            abs_path = path;
        }
        HashResult hash;
        hash_str8(abs_path, &hash);
        uint64_t idx = (map->capacity - 1) & hash.result[0];
        MonitorStripe* stripe = stripe_idx_from_slot_idx(&map->stripes, idx);
        // Lock the mutex for this stripe and start insertion.
        OS::lock_mutex(stripe->mutex);
        MonitoredFileEntry* slot = map->buckets[idx];
        while (slot != nullptr)
        {
            if (str8_match_exact(str8(slot->path, slot->path_len), abs_path))
            {
                result = true;
                mapped->abs_path = str8(slot->path, slot->path_len);
                mapped->gen = FileGen{ slot->gen };
                break;
            }
            slot = slot->next;
        }
        OS::unlock_mutex(stripe->mutex);
        Arena::scratch_end(scratch);
        return result;
    }

    void map_file_unmap_path(MonitoredFileMap* map, String8 path)
    {
        auto scratch = Arena::scratch_begin(Arena::no_conflicts);
        String8 abs_path = str8_empty;
        OS::Error err = OS::canonical_file_path(scratch.arena, &abs_path, path);
        if (err != OS::Error::None)
        {
            // Should report an error or something?
            abs_path = path;
        }
        HashResult hash;
        hash_str8(abs_path, &hash);
        uint64_t idx = (map->capacity - 1) & hash.result[0];
        MonitorStripe* stripe = stripe_idx_from_slot_idx(&map->stripes, idx);
        // Lock the mutex for this stripe and start insertion.
        OS::lock_mutex(stripe->mutex);
        MonitoredFileEntry* slot = map->buckets[idx];
        MonitoredFileEntry* prev_slot = nullptr;
        while (slot != nullptr)
        {
            if (str8_match_exact(str8(slot->path, slot->path_len), abs_path))
                break;
            prev_slot = slot;
            slot = slot->next;
        }

        if (slot != nullptr)
        {
            MonitoredFileEntry* removed = slot;
            SLLStackPop(slot);
            if (prev_slot != nullptr)
            {
                prev_slot->next = slot;
            }
            else
            {
                map->buckets[idx] = slot;
                ++map->nil_buckets;
            }
            --map->count;
            // Load is incorrect but... I don't care yet.
            removed->next = nullptr;
            SLLStackPush(stripe->free_list, removed);
        }
        OS::unlock_mutex(stripe->mutex);
        Arena::scratch_end(scratch);
    }

    // Generation changes.
    uint64_t map_change_gen(MonitoredFileMap* map)
    {
        return os_atomic_u64_eval(&map->change_gen);
    }

    // Async work.
    void async_set_map(MonitoredFileMap* map)
    {
        async_map = map;
    }

    void async_map_tick()
    {
        if (async_map == nullptr)
            return;
        for EachIndex(i, async_map->capacity)
        {
            MonitorStripe* stripe = stripe_idx_from_slot_idx(&async_map->stripes, i);
            OS::lock_mutex(stripe->mutex);
            MonitoredFileEntry* slot = async_map->buckets[i];
            for EachNode(n, slot)
            {
                OS::FileProperties props = OS::file_properties(str8(n->path, n->path_len));
                if (props.modified != n->last_modified)
                {
                    ++n->gen;
                    n->last_modified = props.modified;
                    GAP_UNUSED_RESULT(os_atomic_u64_inc_eval(&async_map->change_gen));
                }
            }
            OS::unlock_mutex(stripe->mutex);
        }
    }
} // namespace FileTrack