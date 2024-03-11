#ifndef KICKCAT_COE_MAILBOX_H
#define KICKCAT_COE_MAILBOX_H

#include "kickcat/Mailbox.h"
#include "kickcat/CoE/OD.h"

namespace kickcat::CoE
{
    std::shared_ptr<mailbox::response::AbstractMessage> createSDOMessage(
            mailbox::response::Mailbox *mbx,
            std::vector<uint8_t>&& raw_message);

    class SDOMessage final : public mailbox::response::AbstractMessage
    {
    public:
        SDOMessage(mailbox::response::Mailbox* mbx, std::vector<uint8_t>&& raw_message, Dictionary& dictionary_);
        virtual ~SDOMessage() = default;

        mailbox::ProcessingResult process() override;
        mailbox::ProcessingResult process(std::vector<uint8_t> const& raw_message) override;

    private:
        mailbox::ProcessingResult upload(Entry* entry);
        mailbox::ProcessingResult uploadComplete(Object* object);

        mailbox::ProcessingResult download(Entry* entry);
        mailbox::ProcessingResult downloadComplete(Object* object);

        bool isUploadAuthorized(Entry* entry);
        bool isDownloadAuthorized(Entry* entry);

        void abort(uint32_t code);

        Dictionary& dictionary_;

        // Pointer on data_
        mailbox::Header* header_;
        CoE::Header* coe_;
        CoE::ServiceData* sdo_;
        uint8_t* payload_;
    };
}

#endif
