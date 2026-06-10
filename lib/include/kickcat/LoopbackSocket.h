#ifndef KICKCAT_LOOPBACK_SOCKET_H
#define KICKCAT_LOOPBACK_SOCKET_H

#include <cstring>
#include <functional>
#include <utility>
#include <vector>

#include "kickcat/AbstractSocket.h"
#include "kickcat/EmulatedNetwork.h"
#include "kickcat/Frame.h"
#include "kickcat/ESC/EmulatedESC.h"

namespace kickcat
{
    // In-process transport: every master frame is run through a set of emulated
    // slaves and the slaves are ticked once, so a master Bus can drive emulated
    // slaves in the same process - no shared memory, no second process. One
    // master writeThenRead == one simulator iteration. The tick callback advances
    // the slave application(s) (Slave::routine, output validation, ...).
    class LoopbackSocket final : public AbstractSocket
    {
    public:
        LoopbackSocket(std::vector<EmulatedESC*> escs, std::function<void()> tick)
            : network_(std::move(escs))
            , tick_(std::move(tick))
        {
        }

        void open(std::string const&) override {}
        void setTimeout(nanoseconds) override {}
        void close() noexcept override {}

        // Topology / fault-injection passthrough for tests that need more than the
        // default daisy chain.
        EmulatedNetwork& network() { return network_; }

        int32_t write(void const* data, int32_t size) override
        {
            Frame frame;
            std::memcpy(frame.data(), data, static_cast<size_t>(size));
            network_.route(frame);
            tick_();
            std::memcpy(buffer_, frame.data(), static_cast<size_t>(size));
            size_ = size;
            pending_ = true;
            return size;
        }

        int32_t read(void* data, int32_t size) override
        {
            if (not pending_)
            {
                return 0;
            }
            int32_t n = size_;
            if (size < n)
            {
                n = size;  // never write past the caller's buffer
            }
            std::memcpy(data, buffer_, static_cast<size_t>(n));
            pending_ = false;
            return n;
        }

    private:
        EmulatedNetwork network_;
        std::function<void()> tick_;
        uint8_t buffer_[ETH_MAX_SIZE];
        int32_t size_    = 0;
        bool    pending_ = false;
    };
}

#endif
