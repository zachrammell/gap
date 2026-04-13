#pragma once

#include "arena.h"

namespace Thread
{
    struct TLD
    {
        // Scratch arenas per-thread.
        Arena::Arena* arenas[2];
#if BUILD_DEBUG
        Arena::Arena* scratch_track_arena;
        Arena::ScratchTrackerList scratch_track_lst;
#endif // BUILD_DEBUG
    };

    // Thread-local data setup.
    TLD* tld_alloc();
    void tld_release(TLD* tld);
    void tld_select(TLD* tld);
    TLD* tld_selected();
} // namespace Thread