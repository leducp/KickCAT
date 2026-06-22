#include <cstring>

#include "Mailbox.h"
#include "kickcat/EoE/mailbox/response.h"
#include "protocol.h"

namespace kickcat::mailbox::response
{
    std::shared_ptr<AbstractMessage> createEoEMessage(Mailbox* mbx, std::vector<uint8_t>&& raw_message)
    {
        auto const* header = pointData<mailbox::Header>(raw_message.data());
        if (header->type != mailbox::Type::EoE)
        {
            return nullptr;
        }
        return std::make_shared<EoEMessage>(mbx, std::move(raw_message));
    }


    EoEMessage::EoEMessage(Mailbox* mbx, std::vector<uint8_t>&& raw_message)
        : AbstractMessage{mbx}
    {
        data_   = std::move(raw_message);
        header_ = pointData<mailbox::Header>(data_.data());
        eoe_    = pointData<EoE::Header>(header_);
    }


    void EoEMessage::replyResult(uint8_t frame_type, uint16_t result)
    {
        std::vector<uint8_t> answer(sizeof(mailbox::Header) + sizeof(EoE::ParameterResponse), 0);
        auto* header = pointData<mailbox::Header>(answer.data());
        auto* resp   = pointData<EoE::ParameterResponse>(header);

        header->len      = sizeof(EoE::ParameterResponse);
        header->address  = header_->address;
        header->channel  = 0;
        header->priority = 0;
        header->type     = mailbox::Type::EoE;
        header->count    = header_->count;

        resp->type          = frame_type;
        resp->port          = eoe_->port;
        resp->last_fragment = 1;
        resp->time_appended = 0;
        resp->time_request  = 0;
        resp->reserved      = 0;
        resp->result        = result;

        reply(std::move(answer));
    }


    ProcessingResult EoEMessage::setIpParameter()
    {
        auto* config = mailbox_->eoeConfig();
        if (config == nullptr)
        {
            replyResult(EoE::frame_type::SET_IP_RESP, EoE::result::NO_IP_SUPPORT);
            return ProcessingResult::FINALIZE;
        }

        if (header_->len < sizeof(EoE::Header) + sizeof(EoE::Parameter))
        {
            replyResult(EoE::frame_type::SET_IP_RESP, EoE::result::UNSPECIFIED_ERROR);
            return ProcessingResult::FINALIZE;
        }

        auto const* parameter = pointData<EoE::Parameter>(eoe_);
        auto const* payload   = pointData<uint8_t>(parameter);
        size_t available = header_->len - sizeof(EoE::Header) - sizeof(EoE::Parameter);

        EoE::IpParameters params{};
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
        extract(parameter->mac_address,     params.mac,      sizeof(EoE::MAC),             params.mac_included);
        extract(parameter->ip_address,      params.ip,       sizeof(EoE::IP),              params.ip_included);
        extract(parameter->subnet_mask,     params.subnet,   sizeof(EoE::SUBNET_MASK),     params.subnet_included);
        extract(parameter->default_gateway, params.gateway,  sizeof(EoE::DEFAULT_GATEWAY), params.gateway_included);
        extract(parameter->dns_server_ip,   params.dns_ip,   sizeof(EoE::DNS_SERVER_IP),   params.dns_ip_included);
        extract(parameter->dns_name,        params.dns_name, sizeof(EoE::DNS_NAME),        params.dns_name_included);

        if (overflow)
        {
            replyResult(EoE::frame_type::SET_IP_RESP, EoE::result::UNSPECIFIED_ERROR);
            return ProcessingResult::FINALIZE;
        }

        uint16_t result = config->setIpParameter(params);
        replyResult(EoE::frame_type::SET_IP_RESP, result);
        return ProcessingResult::FINALIZE;
    }


    ProcessingResult EoEMessage::getIpParameter()
    {
        auto* config = mailbox_->eoeConfig();
        if (config == nullptr)
        {
            replyResult(EoE::frame_type::SET_IP_RESP, EoE::result::NO_IP_SUPPORT);
            return ProcessingResult::FINALIZE;
        }

        EoE::IpParameters params{};
        uint16_t result = config->getIpParameter(params);
        if (result != EoE::result::SUCCESS)
        {
            replyResult(EoE::frame_type::SET_IP_RESP, result);
            return ProcessingResult::FINALIZE;
        }

        std::vector<uint8_t> answer(sizeof(mailbox::Header) + sizeof(EoE::Header) + sizeof(EoE::Parameter)
                                    + sizeof(EoE::MAC) + sizeof(EoE::IP) + sizeof(EoE::SUBNET_MASK)
                                    + sizeof(EoE::DEFAULT_GATEWAY) + sizeof(EoE::DNS_SERVER_IP)
                                    + sizeof(EoE::DNS_NAME), 0);
        auto* header    = pointData<mailbox::Header>(answer.data());
        auto* eoe       = pointData<EoE::Header>(header);
        auto* parameter = pointData<EoE::Parameter>(eoe);
        auto* payload   = pointData<uint8_t>(parameter);

        header->address  = header_->address;
        header->channel  = 0;
        header->priority = 0;
        header->type     = mailbox::Type::EoE;
        header->count    = header_->count;

        std::memset(eoe, 0, sizeof(EoE::Header));
        eoe->type          = EoE::frame_type::GET_IP_RESP;
        eoe->port          = eoe_->port;
        eoe->last_fragment = 1;

        std::memset(parameter, 0, sizeof(EoE::Parameter));
        parameter->mac_address     = params.mac_included;
        parameter->ip_address      = params.ip_included;
        parameter->subnet_mask     = params.subnet_included;
        parameter->default_gateway = params.gateway_included;
        parameter->dns_server_ip   = params.dns_ip_included;
        parameter->dns_name        = params.dns_name_included;

        size_t offset = 0;
        auto append = [&](bool included, void const* src, size_t size)
        {
            if (not included)
            {
                return;
            }
            std::memcpy(payload + offset, src, size);
            offset += size;
        };
        append(params.mac_included,     params.mac,      sizeof(EoE::MAC));
        append(params.ip_included,      params.ip,       sizeof(EoE::IP));
        append(params.subnet_included,  params.subnet,   sizeof(EoE::SUBNET_MASK));
        append(params.gateway_included, params.gateway,  sizeof(EoE::DEFAULT_GATEWAY));
        append(params.dns_ip_included,  params.dns_ip,   sizeof(EoE::DNS_SERVER_IP));
        append(params.dns_name_included,params.dns_name, sizeof(EoE::DNS_NAME));

        header->len = static_cast<uint16_t>(sizeof(EoE::Header) + sizeof(EoE::Parameter) + offset);
        answer.resize(sizeof(mailbox::Header) + header->len);
        reply(std::move(answer));
        return ProcessingResult::FINALIZE;
    }


    ProcessingResult EoEMessage::setAddressFilter()
    {
        auto* config = mailbox_->eoeConfig();
        if (config == nullptr)
        {
            replyResult(EoE::frame_type::SET_FILTER_RESP, EoE::result::NO_FILTER_SUPPORT);
            return ProcessingResult::FINALIZE;
        }

        if (header_->len < sizeof(EoE::Header) + sizeof(EoE::AddressFilterHeader))
        {
            replyResult(EoE::frame_type::SET_FILTER_RESP, EoE::result::UNSPECIFIED_ERROR);
            return ProcessingResult::FINALIZE;
        }

        // Pass the full payload from the AddressFilterHeader onward (header + address/mask lists).
        auto const* payload = reinterpret_cast<uint8_t const*>(pointData<EoE::AddressFilterHeader>(eoe_));
        size_t payload_len = header_->len - sizeof(EoE::Header);

        uint16_t result = config->setAddressFilter(payload, payload_len);
        replyResult(EoE::frame_type::SET_FILTER_RESP, result);
        return ProcessingResult::FINALIZE;
    }


    ProcessingResult EoEMessage::process()
    {
        if (header_->len < sizeof(EoE::Header))
        {
            replyError(std::move(data_), mailbox::Error::SIZE_TOO_SHORT);
            return ProcessingResult::FINALIZE;
        }
        // header_->len is master-supplied: bound it before the handlers derive payload lengths.
        if (sizeof(mailbox::Header) + header_->len > data_.size())
        {
            replyError(std::move(data_), mailbox::Error::INVALID_SIZE);
            return ProcessingResult::FINALIZE;
        }

        switch (eoe_->type)
        {
            case EoE::frame_type::FRAGMENT:
            {
                mailbox_->onEoEFrame(data_.data(), data_.size());
                return ProcessingResult::FINALIZE;
            }
            case EoE::frame_type::SET_IP_REQ:     { return setIpParameter();   }
            case EoE::frame_type::GET_IP_REQ:     { return getIpParameter();   }
            case EoE::frame_type::SET_FILTER_REQ: { return setAddressFilter(); }
            default:
            {
                replyError(std::move(data_), mailbox::Error::UNSUPPORTED_PROTOCOL);
                return ProcessingResult::FINALIZE;
            }
        }
    }


    ProcessingResult EoEMessage::process(std::vector<uint8_t> const& /*raw_message*/)
    {
        return ProcessingResult::NOOP;
    }
}
