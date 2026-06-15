#ifndef KICKCAT_SBUF_QUEUE_H
#define KICKCAT_SBUF_QUEUE_H

#include "LockedRing.h"
#include "kickcat/types.h"


namespace kickcat
{
    constexpr uint32_t SBUF_INVALID_INDEX = UINT32_MAX;

    // Zero-copy buffer-pool queue over shared memory: allocate() a free buffer,
    // fill it in place, ready() it; the consumer get()s, reads in place, free()s.
    // A buffer-pool over two LockedRings (free + ready) -- for small messages that
    // don't need the pool, use LockedRing directly.
    template<typename T, uint32_t N>
    class SBufQueue
    {
    public:
        struct Item
        {
            uint32_t index;
            uint32_t len;
            T* address;
        };

        struct Context
        {
            typename LockedRing<Item, N>::Context free;
            typename LockedRing<Item, N>::Context ready;
            T buffers[N];
        };

        SBufQueue(Context& location)
            : SBufQueue(&location)
        {
        }

        SBufQueue(Context* location)
            : ctx_   { location }
            , free_  { ctx_->free }
            , ready_ { ctx_->ready }
        {
        }

        ~SBufQueue() = default;

        void initContext()
        {
            free_.init();
            ready_.init();

            for (uint32_t i = 0; i < N; ++i)
            {
                // Fill free buffer ring
                (void) free_.push({i, 0, nullptr});
            }
        }

        Item allocate(nanoseconds timeout)
        {
            return pop(free_, timeout);
        };


        void ready(Item const& item)
        {
            (void) ready_.push(item);
        }


        Item get(nanoseconds timeout)
        {
            return pop(ready_, timeout);
        }


        void free(Item const& item)
        {
            (void) free_.push(item);
        }

        uint32_t freed()
        {
            return free_.size();
        }

        uint32_t readied()
        {
            return ready_.size();
        }

        constexpr static std::size_t item_size() { return sizeof(T); }
        constexpr static uint32_t    depth()     { return N;         }


    private:
        Item pop(LockedRing<Item, N>& queue, nanoseconds timeout)
        {
            Item item{ SBUF_INVALID_INDEX, 0, nullptr };
            if (queue.popWait(item, timeout))
            {
                item.address = &ctx_->buffers[item.index];   // process-dependent
            }
            return item;
        }

        Context* ctx_;
        LockedRing<Item, N> free_;
        LockedRing<Item, N> ready_;
    };
}

#endif
