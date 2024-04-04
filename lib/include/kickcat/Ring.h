#ifndef KICKCAT_RING_H
#define KICKCAT_RING_H

#include <cstddef>

namespace kickcat
{
    template<typename T, uint32_t N>
    class Ring
    {
        static_assert((N > 0) && ((N & (N - 1)) == 0), "N shall be a power of two");

    public:
        struct Context
        {
            uint32_t head;
            uint32_t tail;
            T data[N];
        };

        Ring(Context& location) : Ring(&location) {}
        Ring(Context* location = nullptr)
        {
            ctx_ = location;
        }
        ~Ring() = default;

        uint32_t capacity() const   { return N;                       }
        uint32_t size() const       { return ctx_->head - ctx_->tail; }
        uint32_t available() const  { return capacity() - size();     }
        bool isFull() const         { return size() == capacity();    }
        bool isEmpty() const        { return size() == 0;             }

        void reset()
        {
            ctx_->head = 0;
            ctx_->tail = 0;
        }

        bool push(T const& entry)
        {
            if (isFull())
            {
                return false;
            }

            uint32_t index = ctx_->head & (N-1);
            ctx_->data[index] = entry;
            ++ctx_->head;

            return true;
        }

        bool pop(T& entry)
        {
            if (isEmpty())
            {
                return false;
            }

            uint32_t index = ctx_->tail & (N-1);
            entry = ctx_->data[index];
            ++ctx_->tail;

            return true;
        }

    private:
        Context* ctx_;
    };
}
