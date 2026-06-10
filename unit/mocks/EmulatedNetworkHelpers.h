#ifndef KICKCAT_MOCK_EMULATED_NETWORK_HELPERS_H
#define KICKCAT_MOCK_EMULATED_NETWORK_HELPERS_H

#include <cstring>
#include <memory>
#include <vector>

#include "kickcat/ESC/EmulatedESC.h"

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
}

#endif
