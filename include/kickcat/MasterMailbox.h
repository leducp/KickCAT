#ifndef KICKCAT_MASTER_MAILBOX
#define KICKCAT_MASTER_MAILBOX

#include "Mailbox.h"
#include "protocol.h"
#include <cstring>
#include <unordered_map>
#include <vector>


namespace kickcat
{
    struct SDOFrame
    {
        SDOFrame(int32_t payload_size)
        {
            data_.resize(sizeof(mailbox::Header) + sizeof(mailbox::ServiceData) + payload_size);
            header_ = reinterpret_cast<mailbox::Header*>(data_.data());
            coe_ = reinterpret_cast<mailbox::ServiceData*>(data_.data() + sizeof(mailbox::Header));
            payload_ = data_.data() + sizeof(mailbox::Header) + sizeof(mailbox::ServiceData);
        }

        std::vector<uint8_t> data_;
        mailbox::Header* header_;
        mailbox::ServiceData* coe_;
        void* payload_;
    };


    struct SDOField
    {
        void* payload;
        uint32_t size;  // byte
    };

    struct SDOObject
    {
        std::unordered_map<uint8_t, SDOField> fields; /// key:subindex, value: associated field.
        bool complete_access_enable;
        void* payload_complete_access; /// ETG 1006: subindex of zero implies subindex 0 included.
        uint32_t size_complete_access;
    };

    ///< key: index of sdo, value: SDO object.
    typedef std::unordered_map<uint16_t, SDOObject> MasterObjectDictionary;

    ///< Brief
    class MasterMailbox
    {
    public:
        MasterMailbox();
        ~MasterMailbox() = default;

        void init(CoE::MasterDeviceDescription& master_description);

        /// Brief Process a canOpen message (SDO) aimed at the master. Other protocols are not supported.
        std::shared_ptr<GatewayMessage> replyGatewayMessage(uint8_t const* raw_message, int32_t raw_message_size, uint16_t gateway_index);

    private:
        std::vector<uint8_t> replyUploadSDO(uint16_t address, uint16_t index, uint8_t subindex, bool complete_access);

        std::vector<uint8_t> createAbortSDO(uint16_t address, uint16_t index, uint8_t subindex, uint32_t abort_code);

        std::vector<uint8_t> createCompleteAccessUploadSDO(uint16_t address, uint16_t index, uint8_t subindex);

        MasterObjectDictionary objectDictionary_;
    };
}



#endif
