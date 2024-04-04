#ifndef KICKCAT_SBUF_QUEUE_H
#define KICKCAT_SBUF_QUEUE_H

#include "Ring.h"
#include "kickcat/OS/Linux/Mutex.h"
#include "kickcat/OS/Linux/ConditionVariable.h"


namespace kickcat
{
    constexpr uint32_t SBUF_INVALID_INDEX = UINT32_MAX;

    template<typename T, uint32_t N>
    class SBufQueue
    {
    public:
        enum Mode
        {
            BLOCKING,
            NON_BLOCKING
        };

        struct Item
        {
            uint32_t index;
            uint32_t len;
            T* address;
        };

        struct Queue
        {
            Mutex lock;
            ConditionVariable cond;
            Ring<Item, N> ring;
        };

        struct Context
        {
            struct Descriptors
            {
                pthread_mutex_t lock;
                pthread_cond_t cond;
                typename Ring<Item, N>::Context ring;
            };

            Descriptors free;
            Descriptors ready;
            T buffers[N];
        };

        SBufQueue(Context& location)
            : SBufQueue(&location)
        {

        }

        SBufQueue(Context* location)
            : ctx_      { location }
            , free_     { ctx_->free.lock,  ctx_->free.cond,  ctx_->free.ring    }
            , ready_    { ctx_->ready.lock, ctx_->ready.cond, ctx_->ready.ring   }
        {

        }

        ~SBufQueue() = default;

        void initContext()
        {
            free_.lock.init();
            free_.cond.init();
            ready_.lock.init();
            ready_.cond.init();

            free_.ring.reset();
            ready_.ring.reset();

            for (uint32_t i = 0; i < N; ++i)
            {
                // Fill free buffer ring
                (void) free_.ring.push({i, 0, nullptr});
            }
        }

        Item alloc(enum Mode mode = BLOCKING)
        {
            return pop(free_, mode);
        };


        void ready(Item const& item)
        {
            push(ready_, item);
        }


        Item get(enum Mode mode = BLOCKING)
        {
            return pop(ready_, mode);
        }


        void free(Item const& item)
        {
            push(free_, item);
        }

        uint32_t freed()
        {
            return free_.ring.size();
        }

        uint32_t readied()
        {
            return ready_.ring.size();
        }


    private:
        void push(Queue& queue, Item const& item)
        {
            {
                LockGuard guard(queue.lock);
                (void) queue.ring.push(item);
            }
            queue.cond.signal();
        }


        Item pop(Queue& queue, enum Mode mode)
        {
            Item item{ SBUF_INVALID_INDEX, 0, nullptr };

            LockGuard guard(queue.lock);
            if (mode == NON_BLOCKING and queue.ring.isEmpty())
            {
                return item;
            }

            (void) queue.cond.wait(queue.lock, [&queue](){ return not queue.ring.isEmpty(); } );
            (void) queue.ring.pop(item);

            // Determine item address (process dependent)
            item.address = &ctx_->buffers[item.index];
            return item;
        }

        Context* ctx_;
        Queue free_;
        Queue ready_;
    };
}

#endif
