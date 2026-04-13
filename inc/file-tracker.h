#pragma once

#include "arena.h"
#include "gap-strings.h"
#include "os.h"
#include "types.h"

namespace FileTrack
{
    enum class FileGen : uint64_t { };

    struct MappedFile
    {
        String8 abs_path;
        FileGen gen;
    };

    struct MonitoredFileEntry;

    struct MonitorStripe
    {
        OS::Mutex mutex;
        MonitoredFileEntry* free_list;
    };

    struct MonitorStripeArray
    {
        MonitorStripe* stripes;
        uint64_t count;
    };

    struct MonitoredFileMap
    {
        MonitoredFileEntry** buckets;
        MonitorStripeArray stripes;
        uint64_t capacity;
        uint64_t count;
        uint64_t nil_buckets;
        uint64_t load;
        uint64_t change_gen;
    };

    // Creation.
    MonitoredFileMap make_map_file(Arena::Arena* arena);

    // Mapping files.
    MappedFile map_file_map_path(Arena::Arena* arena, MonitoredFileMap* map, String8 path);
    bool map_file_fetch(MappedFile* mapped, MonitoredFileMap* map, String8 path);
    void map_file_unmap_path(MonitoredFileMap* map, String8 path);

    // Generation changes.
    uint64_t map_change_gen(MonitoredFileMap* map);

    // Async work.
    void async_set_map(MonitoredFileMap* map);
    void async_map_tick();
} // namespace FileTracker