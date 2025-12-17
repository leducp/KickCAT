#include "debug.h"
#include "kickcat/FoE/mailbox/request.h"

namespace kickcat::mailbox::request
{
    ReadFileMessage::ReadFileMessage(uint16_t mailbox_size, std::string_view filename, std::string_view save_path, uint16_t password, nanoseconds timeout)
        : AbstractMessage(mailbox_size, timeout)
    {
        foe_ = pointData<FoE::Header>(header_);
        auto header  = pointData<FoE::read::Header>(foe_);
        auto payload = pointData<uint8_t>(header);

        header_->len = 6 + request_payload_size;
        header_->priority = 0; // unused
        header_->channel  = 0;
        header_->type     = mailbox::Type::FoE;
        header_->reserved = 0;
    }

}
