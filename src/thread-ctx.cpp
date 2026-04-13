#include "thread-ctx.h"

#include <cassert>

namespace Thread
{
    // Globals.
    thread_local TLD* tld_thread_local = 0;

    // Thread-local data setup.
    TLD* tld_alloc()
    {
        Arena::Arena* arena = Arena::alloc(Arena::default_params);
        TLD* tld = Arena::push_array<TLD>(arena, 1);
        tld->arenas[0] = arena;
        tld->arenas[1] = Arena::alloc(Arena::default_params);
#if BUILD_DEBUG
        tld->scratch_track_arena = Arena::alloc(Arena::default_params);
#endif // BUILD_DEBUG
        return tld;
    }

    void tld_release(TLD* tld)
    {
        // Make sure we deallocation the arena which does not contain the TLD first.
        Arena::release(tld->arenas[1]);
        Arena::release(tld->arenas[0]);
    }

    void tld_select(TLD* tld)
    {
        tld_thread_local = tld;
    }

    TLD* tld_selected()
    {
        return tld_thread_local;
    }
} // namespace Thread