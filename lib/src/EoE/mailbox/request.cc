#include <cstring>

#include "Error.h"
#include "kickcat/EoE/mailbox/request.h"

namespace kickcat::mailbox::request
{
    uint32_t eoeResultToStatus(uint16_t result)
    {
        switch (result)
        {
            case EoE::result::SUCCESS:                { return MessageStatus::SUCCESS;                   }
            case EoE::result::UNSPECIFIED_ERROR:      { return MessageStatus::EOE_UNSPECIFIED_ERROR;     }
            case EoE::result::UNSUPPORTED_FRAME_TYPE: { return MessageStatus::EOE_UNSUPPORTED_FRAME_TYPE;}
            case EoE::result::NO_IP_SUPPORT:          { return MessageStatus::EOE_NO_IP_SUPPORT;         }
            case EoE::result::DHCP_NOT_SUPPORTED:     { return MessageStatus::EOE_DHCP_NOT_SUPPORTED;    }
            case EoE::result::NO_FILTER_SUPPORT:      { return MessageStatus::EOE_NO_FILTER_SUPPORT;     }
            default:                                  { return MessageStatus::EOE_UNSPECIFIED_ERROR;     }
        }
    }


    static void initEoEHeader(EoE::Header* eoe, uint8_t frame_type, uint8_t port)
    {
        eoe->type            = frame_type;
        eoe->port            = port & 0xF;
        eoe->last_fragment   = 1;
        eoe->time_appended   = 0;
        eoe->time_request    = 0;
        eoe->reserved        = 0;
        eoe->fragment_number = 0;
        eoe->offset          = 0;
        eoe->frame_number    = 0;
    }


    SetIPParameterMessage::SetIPParameterMessage(uint16_t mailbox_size, EoE::IpParameters const& params, nanoseconds timeout)
        : AbstractMessage(mailbox_size, timeout)
    {
        eoe_ = pointData<EoE::Header>(header_);
        auto* parameter = pointData<EoE::Parameter>(eoe_);
        auto* payload   = pointData<uint8_t>(parameter);

        header_->priority = 0;
        header_->channel  = 0;
        header_->type     = mailbox::Type::EoE;
        initEoEHeader(eoe_, EoE::frame_type::SET_IP_REQ, 0);

        std::memset(parameter, 0, sizeof(EoE::Parameter));
        parameter->mac_address     = params.mac_included;
        parameter->ip_address      = params.ip_included;
        parameter->subnet_mask     = params.subnet_included;
        parameter->default_gateway = params.gateway_included;
        parameter->dns_server_ip   = params.dns_ip_included;
        parameter->dns_name        = params.dns_name_included;

        size_t offset = 0;
        auto append = [&](void const* src, size_t size)
        {
            std::memcpy(payload + offset, src, size);
            offset += size;
        };
        size_t needed = sizeof(mailbox::Header) + sizeof(EoE::Header) + sizeof(EoE::Parameter);
        if (params.mac_included)     { needed += sizeof(EoE::MAC);             }
        if (params.ip_included)      { needed += sizeof(EoE::IP);              }
        if (params.subnet_included)  { needed += sizeof(EoE::SUBNET_MASK);     }
        if (params.gateway_included) { needed += sizeof(EoE::DEFAULT_GATEWAY); }
        if (params.dns_ip_included)  { needed += sizeof(EoE::DNS_SERVER_IP);   }
        if (params.dns_name_included){ needed += sizeof(EoE::DNS_NAME);        }
        if (needed > data_.size())
        {
            THROW_ERROR("EoE Set IP request does not fit in the mailbox");
        }

        if (params.mac_included)     { append(params.mac,      sizeof(EoE::MAC));             }
        if (params.ip_included)      { append(params.ip,       sizeof(EoE::IP));              }
        if (params.subnet_included)  { append(params.subnet,   sizeof(EoE::SUBNET_MASK));     }
        if (params.gateway_included) { append(params.gateway,  sizeof(EoE::DEFAULT_GATEWAY)); }
        if (params.dns_ip_included)  { append(params.dns_ip,   sizeof(EoE::DNS_SERVER_IP));   }
        if (params.dns_name_included){ append(params.dns_name, sizeof(EoE::DNS_NAME));        }

        header_->len = static_cast<uint16_t>(sizeof(EoE::Header) + sizeof(EoE::Parameter) + offset);
    }


    ProcessingResult SetIPParameterMessage::process(uint8_t const* received)
    {
        auto const* header = pointData<mailbox::Header>(received);
        if ((header->address & mailbox::GATEWAY_MESSAGE_MASK) != 0)
        {
            return ProcessingResult::NOOP;
        }
        if ((header->type != mailbox::Type::EoE) or (header->count != header_->count))
        {
            return ProcessingResult::NOOP;
        }

        auto const* resp = pointData<EoE::ParameterResponse>(header);
        if (resp->type != EoE::frame_type::SET_IP_RESP)
        {
            return ProcessingResult::NOOP;
        }

        status_ = eoeResultToStatus(resp->result);
        return ProcessingResult::FINALIZE;
    }


    GetIPParameterMessage::GetIPParameterMessage(uint16_t mailbox_size, EoE::IpParameters* params, nanoseconds timeout)
        : AbstractMessage(mailbox_size, timeout)
        , client_params_(params)
    {
        eoe_ = pointData<EoE::Header>(header_);

        header_->priority = 0;
        header_->channel  = 0;
        header_->type     = mailbox::Type::EoE;
        header_->len      = sizeof(EoE::Header);
        initEoEHeader(eoe_, EoE::frame_type::GET_IP_REQ, 0);
    }


    ProcessingResult GetIPParameterMessage::process(uint8_t const* received)
    {
        auto const* header = pointData<mailbox::Header>(received);
        if ((header->address & mailbox::GATEWAY_MESSAGE_MASK) != 0)
        {
            return ProcessingResult::NOOP;
        }
        if ((header->type != mailbox::Type::EoE) or (header->count != header_->count))
        {
            return ProcessingResult::NOOP;
        }

        auto const* eoe = pointData<EoE::Header>(header);
        if (eoe->type != EoE::frame_type::GET_IP_RESP)
        {
            return ProcessingResult::NOOP;
        }

        // header->len is slave-supplied: bound it before deriving 'available', else over-read.
        if ((header->len < sizeof(EoE::Header) + sizeof(EoE::Parameter)) or
            (sizeof(mailbox::Header) + header->len > data_.size()))
        {
            status_ = MessageStatus::EOE_WRONG_SERVICE;
            return ProcessingResult::FINALIZE;
        }

        auto const* parameter = pointData<EoE::Parameter>(eoe);
        auto const* payload   = pointData<uint8_t>(parameter);
        size_t available = header->len - sizeof(EoE::Header) - sizeof(EoE::Parameter);

        EoE::IpParameters result{};
        size_t offset = 0;
        bool overflow = false;
        auto extract = [&](bool included, void* dst, size_t size, bool& flag)
        {
            if (not included)
            {
                return;
            }
            if (offset + size > available)
            {
                overflow = true;
                return;
            }
            std::memcpy(dst, payload + offset, size);
            offset += size;
            flag = true;
        };
        extract(parameter->mac_address,     result.mac,      sizeof(EoE::MAC),             result.mac_included);
        extract(parameter->ip_address,      result.ip,       sizeof(EoE::IP),              result.ip_included);
        extract(parameter->subnet_mask,     result.subnet,   sizeof(EoE::SUBNET_MASK),     result.subnet_included);
        extract(parameter->default_gateway, result.gateway,  sizeof(EoE::DEFAULT_GATEWAY), result.gateway_included);
        extract(parameter->dns_server_ip,   result.dns_ip,   sizeof(EoE::DNS_SERVER_IP),   result.dns_ip_included);
        extract(parameter->dns_name,        result.dns_name, sizeof(EoE::DNS_NAME),        result.dns_name_included);

        if (overflow)
        {
            status_ = MessageStatus::EOE_WRONG_SERVICE;
            return ProcessingResult::FINALIZE;
        }

        if (client_params_ != nullptr)
        {
            *client_params_ = result;
        }
        status_ = MessageStatus::SUCCESS;
        return ProcessingResult::FINALIZE;
    }


    SetAddressFilterMessage::SetAddressFilterMessage(uint16_t mailbox_size, EoE::AddressFilter const& filter, nanoseconds timeout)
        : AbstractMessage(mailbox_size, timeout)
    {
        // The AddressFilterHeader counts are 4-bit / 2-bit fields (ETG.1000.6 Table 86).
        if ((filter.addresses.size() > 0xF) or (filter.masks.size() > 0x3))
        {
            THROW_ERROR("EoE address filter exceeds the field-width limits (15 addresses, 3 masks)");
        }
        size_t needed = sizeof(mailbox::Header) + sizeof(EoE::Header) + sizeof(EoE::AddressFilterHeader)
                      + (filter.addresses.size() + filter.masks.size()) * 6;
        if (needed > data_.size())
        {
            THROW_ERROR("EoE Set address filter request does not fit in the mailbox");
        }

        eoe_ = pointData<EoE::Header>(header_);
        auto* filter_header = pointData<EoE::AddressFilterHeader>(eoe_);
        auto* payload       = pointData<uint8_t>(filter_header);

        header_->priority = 0;
        header_->channel  = 0;
        header_->type     = mailbox::Type::EoE;
        initEoEHeader(eoe_, EoE::frame_type::SET_FILTER_REQ, 0);

        std::memset(filter_header, 0, sizeof(EoE::AddressFilterHeader));
        filter_header->mac_filter_count  = static_cast<uint8_t>(filter.addresses.size()) & 0xF;
        filter_header->mac_filter_mask   = static_cast<uint8_t>(filter.masks.size()) & 0x3;
        filter_header->inhibit_broadcast = filter.inhibit_broadcast;

        size_t offset = 0;
        for (auto const& mac : filter.addresses)
        {
            std::memcpy(payload + offset, mac.data(), mac.size());
            offset += mac.size();
        }
        for (auto const& mask : filter.masks)
        {
            std::memcpy(payload + offset, mask.data(), mask.size());
            offset += mask.size();
        }

        header_->len = static_cast<uint16_t>(sizeof(EoE::Header) + sizeof(EoE::AddressFilterHeader) + offset);
    }


    ProcessingResult SetAddressFilterMessage::process(uint8_t const* received)
    {
        auto const* header = pointData<mailbox::Header>(received);
        if ((header->address & mailbox::GATEWAY_MESSAGE_MASK) != 0)
        {
            return ProcessingResult::NOOP;
        }
        if ((header->type != mailbox::Type::EoE) or (header->count != header_->count))
        {
            return ProcessingResult::NOOP;
        }

        auto const* resp = pointData<EoE::ParameterResponse>(header);
        if (resp->type != EoE::frame_type::SET_FILTER_RESP)
        {
            return ProcessingResult::NOOP;
        }

        status_ = eoeResultToStatus(resp->result);
        return ProcessingResult::FINALIZE;
    }


    EoEReceiveMessage::EoEReceiveMessage(uint16_t mailbox_size)
        : AbstractMessage(mailbox_size, 0ns)
    {
    }


    ProcessingResult EoEReceiveMessage::process(uint8_t const* received)
    {
        auto const* header = pointData<mailbox::Header>(received);
        if ((header->address & mailbox::GATEWAY_MESSAGE_MASK) != 0)
        {
            return ProcessingResult::NOOP;
        }
        if (header->type != mailbox::Type::EoE)
        {
            return ProcessingResult::NOOP;
        }

        auto const* eoe = pointData<EoE::Header>(header);
        if (eoe->type != EoE::frame_type::FRAGMENT)
        {
            return ProcessingResult::NOOP;   // a parameter response: leave it for its requester
        }

        // Pass the buffer capacity, not header->len, so the reassembler's bound guards over-reads.
        auto frame = reassembler_.push(received, data_.size());
        if (frame.has_value() and sink_)
        {
            sink_(frame->data(), frame->size(), eoe->port);
        }
        return ProcessingResult::FINALIZE_AND_KEEP;
    }


    EoEFrameMessage::EoEFrameMessage(uint16_t mailbox_size, std::vector<uint8_t>&& fragment)
        : AbstractMessage(mailbox_size, 0ns)
    {
        data_   = std::move(fragment);
        header_ = reinterpret_cast<mailbox::Header*>(data_.data());
        status_ = MessageStatus::SUCCESS;
    }


    ProcessingResult EoEFrameMessage::process(uint8_t const* /*received*/)
    {
        return ProcessingResult::NOOP;
    }
}


namespace kickcat::mailbox::request
{
    std::shared_ptr<AbstractMessage> Mailbox::createEoESetIP(EoE::IpParameters const& params, nanoseconds timeout)
    {
        if (recv_size == 0)
        {
            THROW_ERROR("This mailbox is inactive");
        }
        auto msg = std::make_shared<SetIPParameterMessage>(recv_size, params, timeout);
        msg->setCounter(nextCounter());
        to_send.push(msg);
        return msg;
    }


    std::shared_ptr<AbstractMessage> Mailbox::createEoEGetIP(EoE::IpParameters* params, nanoseconds timeout)
    {
        if (recv_size == 0)
        {
            THROW_ERROR("This mailbox is inactive");
        }
        auto msg = std::make_shared<GetIPParameterMessage>(recv_size, params, timeout);
        msg->setCounter(nextCounter());
        to_send.push(msg);
        return msg;
    }


    std::shared_ptr<AbstractMessage> Mailbox::createEoESetAddressFilter(EoE::AddressFilter const& filter, nanoseconds timeout)
    {
        if (recv_size == 0)
        {
            THROW_ERROR("This mailbox is inactive");
        }
        auto msg = std::make_shared<SetAddressFilterMessage>(recv_size, filter, timeout);
        msg->setCounter(nextCounter());
        to_send.push(msg);
        return msg;
    }


    void Mailbox::sendEoEFrame(uint8_t const* frame, size_t len, uint8_t port)
    {
        if (recv_size == 0)
        {
            THROW_ERROR("This mailbox is inactive");
        }

        uint8_t frame_number = eoe_frame_number & 0xF;
        eoe_frame_number = (eoe_frame_number + 1) & 0xF;

        auto fragments = EoE::fragmentFrame(frame, len, recv_size, frame_number, port);
        for (auto& fragment : fragments)
        {
            auto msg = std::make_shared<EoEFrameMessage>(recv_size, std::move(fragment));
            msg->setCounter(nextCounter());
            to_send.push(msg);
        }
    }
}
