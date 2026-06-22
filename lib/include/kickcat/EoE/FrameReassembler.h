#ifndef KICKCAT_EOE_FRAMEREASSEMBLER_H
#define KICKCAT_EOE_FRAMEREASSEMBLER_H

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "kickcat/EoE/protocol.h"

namespace kickcat::EoE
{
    // Split one Ethernet frame into complete mailbox messages. Every fragment but the last is a
    // multiple of 32 octets (offsets are block-aligned). The mailbox 'count' is left 0 for the
    // caller to set. Throws if the mailbox cannot hold a 32-octet block.
    std::vector<std::vector<uint8_t>> fragmentFrame(uint8_t const* frame, size_t len,
                                                    uint16_t mailbox_size, uint8_t frame_number, uint8_t port);

    // Reassembles inbound EoE frames; holds no queue, push() returns a frame the moment its last
    // fragment arrives. Frames interleaved on a port are tracked independently per (port, frame
    // number). A trailing time_appended timestamp on the last fragment is stripped, not surfaced.
    class FrameReassembler
    {
    public:
        std::optional<std::vector<uint8_t>> push(uint8_t const* raw_eoe_msg, size_t len);
        void reset();

    private:
        struct Partial
        {
            uint8_t  key;                 // (port << 4) | frame_number
            uint16_t next_offset_blocks;
            std::vector<uint8_t> buffer;
        };

        // Caps memory against malformed input; the oldest partial is evicted when reached.
        static constexpr size_t MAX_PARTIALS = 16;

        Partial* find(uint8_t key);
        void erase(uint8_t key);

        std::vector<Partial> partials_;
    };
}

#endif
