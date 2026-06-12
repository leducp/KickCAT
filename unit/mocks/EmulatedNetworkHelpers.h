#ifndef KICKCAT_MOCK_EMULATED_NETWORK_HELPERS_H
#define KICKCAT_MOCK_EMULATED_NETWORK_HELPERS_H

#include <cstring>
#include <memory>
#include <queue>
#include <vector>

#include "kickcat/AbstractSocket.h"
#include "kickcat/EmulatedNetwork.h"
#include "kickcat/ESC/EmulatedESC.h"
#include "kickcat/Frame.h"

namespace kickcat
{
    inline std::vector<std::unique_ptr<EmulatedESC>> makeSlaves(size_t n)
    {
        std::vector<std::unique_ptr<EmulatedESC>> slaves;
        for (size_t i = 0; i < n; ++i)
        {
            slaves.push_back(std::make_unique<EmulatedESC>());
        }
        return slaves;
    }

    inline std::vector<EmulatedESC*> pointers(std::vector<std::unique_ptr<EmulatedESC>> const& slaves)
    {
        std::vector<EmulatedESC*> ptrs;
        for (auto const& s : slaves)
        {
            ptrs.push_back(s.get());
        }
        return ptrs;
    }

    // Overlapped mapping like Bus::createMapping: input and output FMMUs share the
    // same logical range, the layout that makes a spliced LRW impossible to OR.
    inline void configureOverlappedPdo(EmulatedESC& esc, uint32_t logical_address, uint32_t input_value)
    {
        uint8_t current = State::PRE_OP;
        esc.write(reg::AL_STATUS, &current, 1);
        uint8_t next = State::SAFE_OP;
        esc.write(reg::AL_CONTROL, &next, 1);

        fmmu::Register fmmu;
        std::memset(&fmmu, 0, sizeof(fmmu::Register));
        fmmu.type               = 2; // master -> slave
        fmmu.logical_address    = logical_address;
        fmmu.length             = 4;
        fmmu.logical_stop_bit   = 0x7;
        fmmu.physical_address   = 0x1200;
        fmmu.activate           = 1;
        esc.write(reg::FMMU + 0x00, &fmmu, sizeof(fmmu::Register));

        fmmu.type             = 1; // slave -> master, same logical range
        fmmu.physical_address = 0x1100;
        esc.write(reg::FMMU + 0x10, &fmmu, sizeof(fmmu::Register));

        // Run internal logic so the state machine reaches SAFE_OP and applies the FMMUs
        DatagramHeader header{Command::BRD, 0, 0, sizeof(uint64_t), 0, 0, 0, 0};
        uint64_t dummy = 0;
        uint16_t wkc = 0;
        esc.processDatagram(&header, &dummy, &wkc);

        esc.write(0x1100, &input_value, sizeof(input_value));
    }


    enum class NIC
    {
        NOMINAL,
        REDUNDANCY,
    };

    // Physical model of one master cable: a written frame is routed through the segment
    // and pops out on the other NIC when the ring is intact, or loops back at the break
    // and returns on the NIC it was injected on.
    class PhysicalSocket final : public AbstractSocket
    {
    public:
        PhysicalSocket(EmulatedNetwork& net, NIC nic)
            : net_(net)
            , nic_(nic)
        {
        }

        void setPeer(PhysicalSocket* peer) { peer_ = peer; }

        void open(std::string const&) override {}
        void setTimeout(nanoseconds) override {}
        void close() noexcept override {}

        int32_t read(void* frame, int32_t frame_size) override
        {
            if (rx_.empty())
            {
                return -1;
            }
            std::vector<uint8_t> const& raw = rx_.front();
            int32_t size = static_cast<int32_t>(raw.size());
            if (size > frame_size)
            {
                size = frame_size;
            }
            std::memcpy(frame, raw.data(), static_cast<size_t>(size));
            rx_.pop();
            return size;
        }

        int32_t write(void const* frame, int32_t frame_size) override
        {
            if (cut_to_master)
            {
                return frame_size; // severed master cable: the frame never reaches the segment
            }

            Frame routed(frame, frame_size);
            if (not net_.route(routed, nic_ == NIC::REDUNDANCY))
            {
                return frame_size; // frame destroyed by an ESC (circulating flag)
            }

            PhysicalSocket* destination = this; // broken ring: loopback to the injection NIC
            if (net_.ringIntact())
            {
                destination = peer_;
            }
            if (destination->cut_to_master)
            {
                return frame_size; // response lost on the severed cable
            }
            if (destination->drop_next_delivery)
            {
                destination->drop_next_delivery = false;
                return frame_size;
            }
            destination->rx_.emplace(routed.data(), routed.data() + frame_size);
            return frame_size;
        }

        bool cut_to_master{false};
        bool drop_next_delivery{false};

    private:
        EmulatedNetwork& net_;
        NIC nic_;
        PhysicalSocket* peer_{nullptr};
        std::queue<std::vector<uint8_t>> rx_;
    };
}

#endif
