#ifndef KICKCAT_FOE_MAILBOX_REQUEST_H
#define KICKCAT_FOE_MAILBOX_REQUEST_H

#include "kickcat/Mailbox.h"
#include "kickcat/FoE/protocol.h"

namespace kickcat::mailbox::request
{
    class ReadFileMessage : public AbstractMessage
    {
    public:
        ReadFileMessage(uint16_t mailbox_size, std::string_view filename, std::string_view save_path, uint16_t password = 0, nanoseconds timeout = 100ms);
        virtual ~ReadFileMessage() = default;

        ProcessingResult process(uint8_t const* received) override;

    protected:
        FoE::Header* foe_;
    };
}

#endif
