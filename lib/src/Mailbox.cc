#include <algorithm>
#include <exception>

#include "AbstractESC.h"
#include "debug.h"
#include "CoE/mailbox/request.h"
#include "CoE/mailbox/response.h"
#include "Error.h"
#include "Mailbox.h"
#include "protocol.h"

namespace kickcat::mailbox
{
    uint8_t nextCounter(uint8_t& counter)
    {
        counter++;
        if (counter > 7)
        {
            counter = 1;
        }
        return counter;
    }
}

namespace kickcat::mailbox::request
{
    uint8_t Mailbox::nextCounter()
    {
        return mailbox::nextCounter(counter);
    }


    void Mailbox::generateSMConfig(SyncManager SM[2])
    {
        // 0 is mailbox out, 1 is mailbox in - cf. default EtherCAT configuration if slave support a mailbox
        // NOTE: mailbox out -> master to slave - mailbox in -> slave to master
        SM[0].start_address = recv_offset;
        SM[0].length        = recv_size;
        SM[0].control       = 0x26; // 1 buffer - write access - PDI IRQ ON
        SM[0].status        = 0x00; // RO register
        SM[0].activate      = 0x01; // Sync Manager enable
        SM[0].pdi_control   = 0x00; // RO register
        SM[1].start_address = send_offset;
        SM[1].length        = send_size;
        SM[1].control       = 0x22; // 1 buffer - read access - PDI IRQ ON
        SM[1].status        = 0x00; // RO register
        SM[1].activate      = 0x01; // Sync Manager enable
        SM[1].pdi_control   = 0x00; // RO register
    }


    std::shared_ptr<AbstractMessage> Mailbox::createSDO(uint16_t index, uint8_t subindex, bool CA, uint8_t request, void* data, uint32_t* data_size, nanoseconds timeout)
    {
        if (recv_size == 0)
        {
            THROW_ERROR("This mailbox is inactive");
        }
        auto sdo = std::make_shared<SDOMessage>(recv_size, index, subindex, CA, request, data, data_size, timeout);
        sdo->setCounter(nextCounter());
        to_send.push(sdo);
        return sdo;
    }


    std::shared_ptr<GatewayMessage> Mailbox::createGatewayMessage(uint8_t const* raw_message, int32_t raw_message_size, uint16_t gateway_index, nanoseconds timeout)
    {
        if (raw_message_size > recv_size)
        {
            gateway_error("Message size is bigger than mailbox size\n");
            return nullptr;
        }
        auto msg = std::make_shared<GatewayMessage>(recv_size, raw_message, gateway_index, timeout);
        msg->setCounter(nextCounter());
        to_send.push(msg);
        return msg;
    }


    std::shared_ptr<AbstractMessage> Mailbox::createSDOInfoGetODList(CoE::SDO::information::ListType type, void* data, uint32_t* data_size, nanoseconds timeout)
    {
        if (recv_size == 0)
        {
            THROW_ERROR("This mailbox is inactive");
        }

        uint32_t request_payload_size = sizeof(type);
        std::memcpy(data, &type, request_payload_size);

        auto sdo = std::make_shared<SDOInformationMessage>(recv_size, CoE::SDO::information::GET_OD_LIST_REQ, data, data_size, request_payload_size, timeout);
        sdo->setCounter(nextCounter());
        to_send.push(sdo);
        return sdo;
    }


    std::shared_ptr<AbstractMessage> Mailbox::createSDOInfoGetOD(uint16_t index, void* data, uint32_t* data_size, nanoseconds timeout)
    {
        if (recv_size == 0)
        {
            THROW_ERROR("This mailbox is inactive");
        }

        uint32_t request_payload_size = sizeof(index);
        std::memcpy(data, &index, request_payload_size);
        auto sdo = std::make_shared<SDOInformationMessage>(recv_size, CoE::SDO::information::GET_OD_REQ, data, data_size, request_payload_size, timeout);
        sdo->setCounter(nextCounter());
        to_send.push(sdo);
        return sdo;
    }




    std::shared_ptr<AbstractMessage> Mailbox::createSDOInfoGetED(uint16_t index, uint8_t subindex, uint8_t value_info,
                                                                 void* data, uint32_t* data_size, nanoseconds timeout)
    {
        if (recv_size == 0)
        {
            THROW_ERROR("This mailbox is inactive");
        }

        std::memcpy(data, &index, sizeof(index));
        std::memcpy(static_cast<uint8_t*>(data) + sizeof(index), &subindex, sizeof(subindex));
        std::memcpy(static_cast<uint8_t*>(data) + sizeof(index) + sizeof(subindex), &value_info, sizeof(value_info));
        uint32_t request_payload_size = sizeof(index) + sizeof(subindex) + sizeof(value_info);
        auto sdo = std::make_shared<SDOInformationMessage>(recv_size, CoE::SDO::information::GET_ED_REQ, data, data_size, request_payload_size, timeout);
        sdo->setCounter(nextCounter());
        to_send.push(sdo);
        return sdo;
    }


    std::shared_ptr<AbstractMessage> Mailbox::send()
    {
        auto message = to_send.front();
        to_send.pop();

        // add message to processing queue if needed
        if (message->status() == MessageStatus::RUNNING)
        {
            to_process.push_back(message);
        }
        return message;
    }


    bool Mailbox::receive(uint8_t const* raw_message, nanoseconds current_time)
    {
        // remove timedout messages
        to_process.erase(std::remove_if(to_process.begin(), to_process.end(),
            [&](auto message)
            {
                return (message->status(current_time) == MessageStatus::TIMEDOUT);
            }
        ), to_process.end());

        for (auto it = to_process.begin(); it != to_process.end(); ++it)
        {
            ProcessingResult state = (*it)->process(raw_message);
            switch (state)
            {
                case ProcessingResult::NOOP:
                {
                    continue;
                }
                case ProcessingResult::CONTINUE:
                {
                    (*it)->setCounter(nextCounter());
                    to_send.push(*it);
                    it = to_process.erase(it);
                    return true;
                }
                case ProcessingResult::FINALIZE:
                {
                    it = to_process.erase(it);
                    return true;
                }
                case ProcessingResult::FINALIZE_AND_KEEP:
                {
                    return true;
                }
                default: { }
            }
        }

        return false;
    }


    AbstractMessage::AbstractMessage(uint16_t mailbox_size, nanoseconds timeout)
        : timeout_{timeout}
    {
        data_.resize(mailbox_size);
        header_ = reinterpret_cast<mailbox::Header*>(data_.data());
        header_->address  = 0;            // Default: local processing address
        status_ = MessageStatus::RUNNING; // Default mode is running to send the msg on the bus

        if (timeout_ != 0ns)
        {
            timeout_ += since_epoch();
        }
    }


    uint32_t AbstractMessage::status(nanoseconds current_time)
    {
        if (status_ == MessageStatus::RUNNING)
        {
            if ((timeout_ != 0ns) and (current_time > timeout_))
            {
                status_ = MessageStatus::TIMEDOUT;
            }
        }
        return status_;
    }


    GatewayMessage::GatewayMessage(uint16_t mailbox_size, uint8_t const* raw_message, uint16_t gateway_index, nanoseconds timeout)
        : AbstractMessage(mailbox_size, timeout)
    {
        auto const* header = pointData<mailbox::Header>(raw_message);

        // Copy raw message in internal data field
        int32_t size = sizeof(mailbox::Header) + header->len;
        std::memcpy(data_.data(), raw_message, size);

        // Store gateway index to associate the reply with the request
        gateway_index_ = gateway_index;

        // Switch address field with gateway index and identifier
        address_ = header_->address;
        header_->address = mailbox::GATEWAY_MESSAGE_MASK | gateway_index;
    }


    ProcessingResult GatewayMessage::process(uint8_t const* received)
    {
        auto const* header = pointData<mailbox::Header>(received);

        // Check if the message is associated to our gateway request
        if (header->address != header_->address)
        {
            return ProcessingResult::NOOP;
        }

        // It is the reply to this request: store the result and set back the address field
        int32_t size = header->len + sizeof(mailbox::Header);
        data_.resize(size);
        std::memcpy(data_.data(), received, size);

        header_->address = address_;

        status_ = MessageStatus::SUCCESS;
        return ProcessingResult::FINALIZE;
    }
}


namespace kickcat::mailbox::response
{
    Mailbox::Mailbox(AbstractESC* esc, uint16_t max_allocated_ram_by_msg, uint16_t max_msgs)
        : esc_{esc}
        , max_allocated_ram_by_msg_{max_allocated_ram_by_msg}
        , max_msgs_{max_msgs}
    {
    }


    hresult Mailbox::configure()
    {
        try
        {
            auto [indexIn, mailboxIn]   = esc_->findSm(SM_CONTROL_MODE_MAILBOX | SM_CONTROL_DIRECTION_READ);
            auto [indexOut, mailboxOut] = esc_->findSm(SM_CONTROL_MODE_MAILBOX | SM_CONTROL_DIRECTION_WRITE);

            if (mailboxIn.length != mailboxOut.length or mailboxIn.length > max_allocated_ram_by_msg_)
            {
                return hresult::E_EOVERFLOW;
            }

            mbx_in_  = SYNC_MANAGER_MBX_IN(indexIn, mailboxIn.start_address, mailboxIn.length);
            mbx_out_ = SYNC_MANAGER_MBX_OUT(indexOut, mailboxOut.start_address, mailboxOut.length);
        }
        catch (std::exception const& e)
        {
            return hresult::E_EAGAIN;
        }


        return hresult::OK;
    }

    bool Mailbox::isConfigOk()
    {
        if (mbx_in_.type == SyncManagerType::Unused  or mbx_out_.type == SyncManagerType::Unused)
        {
            return false;
        }

        bool valid = true;
        for (auto& sm : {mbx_in_, mbx_out_})
        {
            valid &= esc_->isSmValid(sm);
        }
        return valid;
    }

    void Mailbox::activate(bool is_activated)
    {
        if (mbx_in_.type != SyncManagerType::Unused and mbx_out_.type != SyncManagerType::Unused )
        {
            esc_->setSmActivate({mbx_in_, mbx_out_}, is_activated);
        }
    }


    void Mailbox::receive()
    {
        SyncManager sync;
        esc_->read(addressSM(mbx_out_.index), &sync, sizeof(SyncManager));
        if (not (sync.status & SM_STATUS_MAILBOX))
        {
            return;
        }

        std::vector<uint8_t> raw_message;
        raw_message.resize(mbx_out_.length);
        int32_t read_bytes = esc_->read(mbx_out_.start_address, raw_message.data(), mbx_out_.length);
        if (read_bytes != mbx_out_.length)
        {
            return;
        }

        for (auto it = to_process_.begin(); it != to_process_.end(); ++it)
        {
            ProcessingResult state = (*it)->process(raw_message);
            switch (state)
            {
                case ProcessingResult::NOOP:
                {
                    continue;
                }
                case ProcessingResult::CONTINUE:
                case ProcessingResult::FINALIZE_AND_KEEP:
                {
                    return;
                }
                case ProcessingResult::FINALIZE:
                {
                    it = to_process_.erase(it);
                    return;
                }
                default: { }
            }
        }

        if (to_process_.size() >= max_msgs_)
        {
            // Queue is full and no one process it: drop the message
            replyError(std::move(raw_message), mailbox::Error::NO_MORE_MEMORY);
            return;
        }

        // No message handle it: let's try to build a new one
        for (auto& factory : factories_)
        {
            auto msg = factory(this, std::move(raw_message));
            if (msg != nullptr)
            {
                to_process_.push_back(msg);
                return;
            }
        }

        // Nothing can be done with this message
        replyError(std::move(raw_message), mailbox::Error::UNSUPPORTED_PROTOCOL);
    }


    void Mailbox::process()
    {
        for (auto it = to_process_.begin(); it != to_process_.end(); ++it)
        {
            ProcessingResult state = (*it)->process();
            switch (state)
            {
                case ProcessingResult::NOOP:
                {
                    continue;
                }
                case ProcessingResult::CONTINUE:
                case ProcessingResult::FINALIZE_AND_KEEP:
                {
                    return;
                }
                case ProcessingResult::FINALIZE:
                {
                    it = to_process_.erase(it);
                    return;
                }
                default: { }
            }
        }
    }

    void Mailbox::send()
    {
        SyncManager sync;
        esc_->read(addressSM(mbx_in_.index), &sync, sizeof(SyncManager));

        // Save last fetched message for repeat procedure
        if (sync.status & SM_STATUS_IRQ_READ)
        {
            repeat_ = last_sent_;

            // reset IRQ by writing to the buffer
            uint8_t dummy = 0;
            esc_->write(mbx_in_.start_address, &dummy, 1);
        }

        // Repeat procedure handling
        if ((sync.activate & SM_ACTIVATE_REPEAT_REQ) != (sync.pdi_control & SM_ACTIVATE_REPEAT_REQ))
        {
            // Write the last sent message - we need to reset the SM to empty it if full
            if (sync.status & SM_STATUS_MAILBOX)
            {
                esc_->deactivateSm(mbx_in_);
                esc_->activateSm(mbx_in_);
            }
            esc_->write(mbx_in_.start_address, repeat_.data(), repeat_.size());

            // Ack the repeat requested
            uint8_t ack = sync.activate & SM_ACTIVATE_REPEAT_REQ;
            esc_->write(addressSM(mbx_in_.index) + 7, &ack, sizeof(uint8_t));

            // We just fill the mailbox: do not continue for now
            return;
        }

        if (sync.status & SM_STATUS_MAILBOX)
        {
            // Mailbox is full
            return;
        }

        if (to_send_.empty())
        {
            return;
        }

        auto& msg = to_send_.front();
        int32_t written_bytes = esc_->write(mbx_in_.start_address, msg.data(), msg.size());
        if (written_bytes > 0)
        {
            last_sent_ = std::move(msg);
            to_send_.pop();
        }
    }

    void Mailbox::enableCoE(CoE::Dictionary&& dictionary)
    {
        //TODO: take the factory instead to be able to reset the dictionary when going back to init
        dictionary_ = std::move(dictionary);
        factories_.push_back(&createSDOMessage);
    }

    void Mailbox::replyError(std::vector<uint8_t>&& raw_message, uint16_t code)
    {
        auto* header = pointData<mailbox::Header>(raw_message.data());
        auto* err    = pointData<mailbox::Error::ServiceData>(header);

        header->type = mailbox::ERR;
        header->len  = sizeof(mailbox::Error::ServiceData);
        err->type    = 0x1;
        err->detail  = code;

        to_send_.push(std::move(raw_message));
    }

    AbstractMessage::AbstractMessage(Mailbox* mbx)
        : mailbox_{mbx}
    {

    }

    uint16_t AbstractMessage::replyExpectedSize()
    {
        return mailbox_->mbx_out_.length;
    }

    void AbstractMessage::reply(std::vector<uint8_t>&& reply)
    {
        mailbox_->to_send_.push(std::move(reply));
    }

    void AbstractMessage::replyError(std::vector<uint8_t>&& raw_message, uint16_t code)
    {
        mailbox_->replyError(std::move(raw_message), code);
    }
}
