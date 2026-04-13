#pragma once

#include "macros.h"
#include "types.h"

namespace Thread
{
    SUPPRESS_NONSTANDARD_EXTENSION_WARNING();
    struct ConcurrentQueue
    {
        union
        {
            struct
            {
                uint32_t prod_read;
                uint32_t prod_write;
            };
            uint64_t prod_group;
        };
        union
        {
            struct
            {
                uint32_t cons_read;
                uint32_t cons_write;
            };
            uint64_t cons_group;
        };
        uint32_t capacity;
    };
    ENABLE_NONSTANDARD_EXTENSION_WARNING();

    // Creation.
    ConcurrentQueue make_concurrent_queue(uint32_t capacity);
    void ccq_clear(ConcurrentQueue* queue);

    // Operations.
    uint32_t ccq_prod_push(ConcurrentQueue* queue);
    uint32_t ccq_prod_commit_push(ConcurrentQueue* queue); // Push for the consumer after the respective element is written.
    uint32_t ccq_cons_shift(ConcurrentQueue* queue);
    uint32_t ccq_cons_commit_shift(ConcurrentQueue* queue); // Commit read space for producer.

    // Queries.
    bool ccq_prod_empty(const ConcurrentQueue* queue);
    bool ccq_cons_empty(const ConcurrentQueue* queue);
    bool ccq_prod_full(const ConcurrentQueue* queue);
    bool ccq_cons_full(const ConcurrentQueue* queue);
    uint32_t ccq_prod_size(const ConcurrentQueue* queue);
    uint32_t ccq_cons_size(const ConcurrentQueue* queue);
} // namespace Thread