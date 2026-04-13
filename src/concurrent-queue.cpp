#include "concurrent-queue.h"

#include <cassert>

#include "gap-bits.h"
#include "os.h"

// Ideas borrowed from https://www.snellman.net/blog/archive/2016-12-13-ring-buffers/, but made to be largely atomic operations.
namespace Thread
{
    namespace
    {
        uint32_t ccq_mask(uint32_t idx, uint32_t cap)
        {
            return idx & (cap - 1);
        }

        SUPPRESS_NONSTANDARD_EXTENSION_WARNING();
        struct QGroup
        {
            union
            {
                struct
                {
                    uint32_t read;
                    uint32_t write;
                };
                uint64_t group;
            };
        };
        ENABLE_NONSTANDARD_EXTENSION_WARNING();

        QGroup ccq_load_prod_group(const ConcurrentQueue* queue)
        {
            QGroup result{};
            result.group = os_atomic_u64_eval(&queue->prod_group);
            return result;
        }

        QGroup ccq_load_cons_group(const ConcurrentQueue* queue)
        {
            QGroup result{};
            result.group = os_atomic_u64_eval(&queue->cons_group);
            return result;
        }
    } // namespace [anon]

    // Creation.
    ConcurrentQueue make_concurrent_queue(uint32_t capacity)
    {
        ConcurrentQueue queue{};
        uint64_t pow_2_aligned_size = up_pow2(capacity);
        queue.capacity = static_cast<uint32_t>(pow_2_aligned_size & 0xFFFFFFFF);
        return queue;
    }

    void ccq_clear(ConcurrentQueue* queue)
    {
        queue->cons_group = queue->prod_group = 0;
    }

    // Operations.
    uint32_t ccq_prod_push(ConcurrentQueue* queue)
    {
        assert(not ccq_prod_full(queue));
        QGroup idx{};
        QGroup next_idx{};
        do
        {
            next_idx = idx = ccq_load_prod_group(queue);
            next_idx.write = idx.write + 1;
        } while (static_cast<uint64_t>(os_atomic_u64_eval_cond_assign(&queue->prod_group, next_idx.group, idx.group)) != idx.group);
        return ccq_mask(idx.write, queue->capacity);
    }

    uint32_t ccq_prod_commit_push(ConcurrentQueue* queue)
    {
        assert(not ccq_cons_full(queue));
        QGroup idx{};
        QGroup next_idx{};
        do
        {
            next_idx = idx = ccq_load_cons_group(queue);
            next_idx.write = ccq_load_prod_group(queue).write;
        } while (static_cast<uint64_t>(os_atomic_u64_eval_cond_assign(&queue->cons_group, next_idx.group, idx.group)) != idx.group);
        assert(next_idx.write == idx.write + 1);
        return ccq_mask(idx.write, queue->capacity);
    }

    uint32_t ccq_cons_shift(ConcurrentQueue* queue)
    {
        assert(not ccq_cons_empty(queue));
        QGroup idx{};
        QGroup next_idx{};
        do
        {
            next_idx = idx = ccq_load_cons_group(queue);
            next_idx.read = idx.read + 1;
        } while (static_cast<uint64_t>(os_atomic_u64_eval_cond_assign(&queue->cons_group, next_idx.group, idx.group)) != idx.group);
        return ccq_mask(idx.read, queue->capacity);
    }

    uint32_t ccq_cons_commit_shift(ConcurrentQueue* queue)
    {
        assert(not ccq_prod_empty(queue));
        QGroup idx{};
        QGroup next_idx{};
        do
        {
            next_idx = idx = ccq_load_prod_group(queue);
            next_idx.read = ccq_load_cons_group(queue).read;
        } while (static_cast<uint64_t>(os_atomic_u64_eval_cond_assign(&queue->prod_group, next_idx.group, idx.group)) != idx.group);
        assert(next_idx.read == idx.read + 1);
        return ccq_mask(idx.read, queue->capacity);
    }

    // Queries.
    bool ccq_prod_empty(const ConcurrentQueue* queue)
    {
        QGroup group = ccq_load_prod_group(queue);
        return group.write == group.read;
    }

    bool ccq_cons_empty(const ConcurrentQueue* queue)
    {
        QGroup group = ccq_load_cons_group(queue);
        return group.write == group.read;
    }

    bool ccq_prod_full(const ConcurrentQueue* queue)
    {
        return ccq_prod_size(queue) == queue->capacity;
    }

    bool ccq_cons_full(const ConcurrentQueue* queue)
    {
        return ccq_cons_size(queue) == queue->capacity;
    }

    uint32_t ccq_prod_size(const ConcurrentQueue* queue)
    {
        QGroup group = ccq_load_prod_group(queue);
        return group.write - group.read;
    }

    uint32_t ccq_cons_size(const ConcurrentQueue* queue)
    {
        QGroup group = ccq_load_cons_group(queue);
        return group.write - group.read;
    }
} // namespace Thread