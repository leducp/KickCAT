#ifndef KICKCAT_COE_MAILBOX_REQUEST_H
#define KICKCAT_COE_MAILBOX_REQUEST_H

#include "kickcat/Mailbox.h"
#include "kickcat/CoE/OD.h"

namespace kickcat::mailbox::request
{
    class SDOMessage final : public AbstractMessage
    {
    public:
        SDOMessage(uint16_t mbx_recv_size, uint16_t mbx_send_size, uint16_t index, uint8_t subindex, bool CA, uint8_t request, void* data, uint32_t* data_size, nanoseconds timeout);

        /// \brief Upload into data, resized to the object size the server announces.
        SDOMessage(uint16_t mbx_recv_size, uint16_t mbx_send_size, uint16_t index, uint8_t subindex, bool CA, std::vector<uint8_t>& data, nanoseconds timeout);
        virtual ~SDOMessage() = default;

        ProcessingResult process(uint8_t const* received) override;

    protected:
        ProcessingResult processUpload           (mailbox::Header const* header, CoE::ServiceData const* sdo, uint8_t const* payload);
        ProcessingResult processUploadSegmented  (mailbox::Header const* header, CoE::ServiceData const* sdo, uint8_t const* payload);
        ProcessingResult processDownload         (mailbox::Header const* header, CoE::ServiceData const* sdo, uint8_t const* payload);
        ProcessingResult processDownloadSegmented(mailbox::Header const* header, CoE::ServiceData const* sdo, uint8_t const* payload);
        void prepareDownloadSegment();
        void fitGrowable(uint32_t size);

        CoE::Header* coe_;
        CoE::ServiceData* sdo_;
        uint8_t* payload_;
        std::vector<uint8_t>* growable_{nullptr};  // upload destination sized by the server, if any
        uint32_t growable_size_{0};                // data size of growable_, before client_buffer_size_ reads it
        uint8_t* client_data_;
        uint32_t* client_data_size_;
        uint32_t client_buffer_size_{0};   // client buffer capacity: fixed at construction, or announced for growable_
        uint32_t download_remaining_{0};   // bytes still to send for a segmented download
    };

    class SDOInformationMessage final : public AbstractMessage
    {
    public:
        SDOInformationMessage(uint16_t mbx_recv_size, uint16_t mbx_send_size, uint8_t request, void* data, uint32_t* data_size, uint32_t request_payload_size, nanoseconds timeout);
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

    class EmergencyMessage final : public AbstractMessage
    {
    public:
        EmergencyMessage(Mailbox& mailbox);
        virtual ~EmergencyMessage() = default;

        ProcessingResult process(uint8_t const* received) override;

    private:
        request::Mailbox& mailbox_;
    };

    class CheckMessage final : public AbstractMessage
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
