#include <cstring>

#include "Error.h"
#include "kickcat/EoE/FrameReassembler.h"

namespace kickcat::EoE
{
    std::vector<std::vector<uint8_t>> fragmentFrame(uint8_t const* frame, size_t len,
                                                    uint16_t mailbox_size, uint8_t frame_number, uint8_t port)
    {
        constexpr size_t overhead = sizeof(mailbox::Header) + sizeof(EoE::Header);
        if (mailbox_size <= overhead)
        {
            THROW_ERROR("Mailbox too small to carry an EoE fragment");
        }

        size_t usable = mailbox_size - overhead;
        size_t chunk  = (usable / 32) * 32;   // block-aligned payload for every non-final fragment
        if (chunk < 32)
        {
            THROW_ERROR("Mailbox too small to carry a 32-octet EoE block");
        }

        std::vector<std::vector<uint8_t>> fragments;
        uint16_t complete_size_blocks = blocks32(static_cast<uint32_t>(len));

        size_t  sent     = 0;
        uint8_t fragment = 0;
        while (true)
        {
            size_t remaining = len - sent;
            size_t this_data = remaining;
            if (this_data > chunk)
            {
                this_data = chunk;
            }
            bool last = (sent + this_data) >= len;

            std::vector<uint8_t> msg(overhead + this_data, 0);
            auto* header = pointData<mailbox::Header>(msg.data());
            auto* eoe    = pointData<EoE::Header>(header);

            header->len      = static_cast<uint16_t>(sizeof(EoE::Header) + this_data);
            header->address  = 0;
            header->channel  = 0;
            header->priority = 0;
            header->type     = mailbox::Type::EoE;
            header->count    = 0;

            eoe->type          = EoE::frame_type::FRAGMENT;
            eoe->port          = port & 0xF;
            eoe->last_fragment = last;
            eoe->time_appended = 0;
            eoe->time_request  = 0;
            eoe->reserved      = 0;
            eoe->fragment_number = fragment;
            eoe->frame_number    = frame_number & 0xF;
            if (fragment == 0)
            {
                eoe->offset = complete_size_blocks;   // initiate fragment carries complete_size, not offset
            }
            else
            {
                eoe->offset = blocks32(static_cast<uint32_t>(sent));
            }

            std::memcpy(pointData<uint8_t>(eoe), frame + sent, this_data);
            fragments.push_back(std::move(msg));

            sent += this_data;
            fragment++;
            if (last)
            {
                break;
            }
        }

        return fragments;
    }


    void FrameReassembler::reset()
    {
        partials_.clear();
    }


    FrameReassembler::Partial* FrameReassembler::find(uint8_t key)
    {
        for (auto& partial : partials_)
        {
            if (partial.key == key)
            {
                return &partial;
            }
        }
        return nullptr;
    }


    void FrameReassembler::erase(uint8_t key)
    {
        for (auto it = partials_.begin(); it != partials_.end(); ++it)
        {
            if (it->key == key)
            {
                partials_.erase(it);
                return;
            }
        }
    }


    std::optional<std::vector<uint8_t>> FrameReassembler::push(uint8_t const* raw_eoe_msg, size_t len)
    {
        if (len < sizeof(mailbox::Header) + sizeof(EoE::Header))
        {
            return std::nullopt;
        }

        auto const* header = pointData<mailbox::Header>(raw_eoe_msg);
        auto const* eoe    = pointData<EoE::Header>(header);

        if ((header->type != mailbox::Type::EoE) or (eoe->type != EoE::frame_type::FRAGMENT))
        {
            return std::nullopt;
        }

        if ((header->len < sizeof(EoE::Header)) or (sizeof(mailbox::Header) + header->len > len))
        {
            return std::nullopt;
        }
        size_t data_len = header->len - sizeof(EoE::Header);
        uint8_t const* data = pointData<uint8_t>(eoe);

        uint8_t key = static_cast<uint8_t>((eoe->port << 4) | eoe->frame_number);

        // Strip the trailing timestamp (last fragment only) so it does not pollute the frame.
        size_t frame_bytes = data_len;
        if (eoe->last_fragment and eoe->time_appended)
        {
            if (data_len < sizeof(uint32_t))
            {
                erase(key);
                return std::nullopt;
            }
            frame_bytes = data_len - sizeof(uint32_t);
        }

        Partial* partial = nullptr;
        if (eoe->fragment_number == 0)
        {
            erase(key);
            if (partials_.size() >= MAX_PARTIALS)
            {
                partials_.erase(partials_.begin());
            }
            partials_.push_back(Partial{key, 0, {}});
            partial = &partials_.back();
            partial->buffer.assign(data, data + frame_bytes);
        }
        else
        {
            partial = find(key);
            if ((partial == nullptr) or (eoe->offset != partial->next_offset_blocks))
            {
                erase(key);
                return std::nullopt;
            }
            partial->buffer.insert(partial->buffer.end(), data, data + frame_bytes);
        }

        if (eoe->last_fragment)
        {
            std::vector<uint8_t> frame = std::move(partial->buffer);
            erase(key);
            return frame;
        }

        partial->next_offset_blocks = blocks32(static_cast<uint32_t>(partial->buffer.size()));
        return std::nullopt;
    }
}
