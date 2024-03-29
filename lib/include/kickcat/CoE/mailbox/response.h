#ifndef KICKCAT_COE_MAILBOX_RESPONSE_H
#define KICKCAT_COE_MAILBOX_RESPONSE_H

#include "kickcat/Mailbox.h"
#include "kickcat/CoE/OD.h"

namespace kickcat::mailbox::response
{
    std::shared_ptr<AbstractMessage> createSDOMessage(
            Mailbox *mbx,
            std::vector<uint8_t>&& raw_message);

    class SDOMessage final : public AbstractMessage
    {
    public:
        SDOMessage(Mailbox* mbx, std::vector<uint8_t>&& raw_message, CoE::Dictionary& dictionary_);
        virtual ~SDOMessage() = default;

        ProcessingResult process() override;
        ProcessingResult process(std::vector<uint8_t> const& raw_message) override;

    private:
        ProcessingResult upload(CoE::Entry* entry);
        ProcessingResult uploadComplete(CoE::Object* object);

        ProcessingResult download(CoE::Entry* entry);
        ProcessingResult downloadComplete(CoE::Object* object);

        bool isUploadAuthorized(CoE::Entry* entry);
        bool isDownloadAuthorized(CoE::Entry* entry);

        void abort(uint32_t code);

        CoE::Dictionary& dictionary_;

        // Pointer on data_
        mailbox::Header* header_;
        CoE::Header* coe_;
        CoE::ServiceData* sdo_;
        uint8_t* payload_;
    };
}

#endif
