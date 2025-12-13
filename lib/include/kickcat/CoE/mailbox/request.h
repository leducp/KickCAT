#ifndef KICKCAT_COE_MAILBOX_REQUEST_H
#define KICKCAT_COE_MAILBOX_REQUEST_H

#include "kickcat/Mailbox.h"
#include "kickcat/CoE/OD.h"

namespace kickcat::mailbox::request
{
    class SDOMessage : public AbstractMessage
    {
    public:
        SDOMessage(uint16_t mailbox_size, uint16_t index, uint8_t subindex, bool CA, uint8_t request, void* data, uint32_t* data_size, nanoseconds timeout);
        virtual ~SDOMessage() = default;

        ProcessingResult process(uint8_t const* received) override;

    protected:
        ProcessingResult processUpload           (mailbox::Header const* header, CoE::ServiceData const* sdo, uint8_t const* payload);
        ProcessingResult processUploadSegmented  (mailbox::Header const* header, CoE::ServiceData const* sdo, uint8_t const* payload);
        ProcessingResult processDownload         (mailbox::Header const* header, CoE::ServiceData const* sdo, uint8_t const* payload);
        ProcessingResult processDownloadSegmented(mailbox::Header const* header, CoE::ServiceData const* sdo, uint8_t const* payload);

        CoE::Header* coe_;
        CoE::ServiceData* sdo_;
        uint8_t* payload_;
        uint8_t* client_data_;
        uint32_t* client_data_size_;
    };

    class SDOInformationMessage : public AbstractMessage
    {
    public:
        SDOInformationMessage(uint16_t mailbox_size, uint8_t request, void* data, uint32_t* data_size, uint32_t request_payload_size, nanoseconds timeout);
        virtual ~SDOInformationMessage() = default;

        ProcessingResult process(uint8_t const* received) override;

    protected:
        ProcessingResult processResponse(mailbox::Header const* header, CoE::ServiceDataInfo const* sdo,
                                         uint8_t const* payload, uint8_t expected_opcode);

        CoE::Header* coe_;
        CoE::ServiceDataInfo* sdo_;
        uint8_t* payload_;
        uint8_t* client_data_;
        uint32_t* client_data_size_;
        uint32_t already_received_size_{0};
    };

    class EmergencyMessage : public AbstractMessage
    {
    public:
        EmergencyMessage(Mailbox& mailbox);
        virtual ~EmergencyMessage() = default;

        ProcessingResult process(uint8_t const* received) override;

    private:
        request::Mailbox& mailbox_;
    };

    class CheckMessage : public AbstractMessage
    {
    public:
        CheckMessage(Mailbox& mailbox);
        virtual ~CheckMessage() = default;

        ProcessingResult process(uint8_t const* received) override;

    private:
        request::Mailbox& mailbox_;
    };
}

#endif
