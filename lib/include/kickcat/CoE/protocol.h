#ifndef KICKCAT_COE_PROTOCOL_H
#define KICKCAT_COE_PROTOCOL_H

#include "kickcat/CoE/OD.h"

namespace kickcat::CoE
{
    constexpr uint16_t SM_COM_TYPE       = 0x1C00; // each sub-entry described SM[x] com type (mailbox in/out, PDO in/out, not used)
    constexpr uint16_t SM_CHANNEL        = 0x1C10; // each entry is associated with the mapped PDOs (if in used)

    struct Header
    {
        uint16_t number : 9,
                reserved : 3,
                service : 4; // i.e. request, response
    } __attribute__((__packed__));
    std::string toString(Header const* header); // Stringify the whole message, not just the header

    struct ServiceData      // ETG1000.6 chapter 5.6.2 SDO
    {
        uint8_t size_indicator : 1,
                transfer_type : 1, // expedited or not
                block_size : 2,
                complete_access : 1,
                command : 3; // i.e. upload
        uint16_t index;
        uint8_t subindex;
    } __attribute__((__packed__));

    struct ServiceDataInfo // ETG1000.6 chapter 5.6.3 SDO Information
    {
        uint16_t opcode : 7,
                    incomplete : 1,
                    reserved : 8;
        uint16_t fragments_left;
    } __attribute__((__packed__));

    struct Emergency        // ETG1000.6 chapter 5.6.4 Emergency
    {
        uint16_t error_code;
        uint8_t  error_register;
        uint8_t  data[5];
    } __attribute__((__packed__));
    std::string toString(Emergency const& emg);

    enum Service
    {
        EMERGENCY            = 0x01,
        SDO_REQUEST          = 0x02,
        SDO_RESPONSE         = 0x03,
        TxPDO                = 0x04,
        RxPDO                = 0x05,
        TxPDO_REMOTE_REQUEST = 0x06,
        RxPDO_REMOTE_REQUEST = 0x07,
        SDO_INFORMATION      = 0x08
    };

    namespace SDO
    {
        // Command specifiers depending on SDO request type
        namespace request
        {
            constexpr uint8_t DOWNLOAD_SEGMENTED = 0x00;
            constexpr uint8_t DOWNLOAD           = 0x01;
            constexpr uint8_t UPLOAD             = 0x02;
            constexpr uint8_t UPLOAD_SEGMENTED   = 0x03;
            constexpr uint8_t ABORT              = 0x04;

            char const* toString(uint8_t command);
        }

        namespace response
        {
            constexpr uint8_t UPLOAD_SEGMENTED    = 0x00;
            constexpr uint8_t DOWNLOAD_SEGMENTED  = 0x01;
            constexpr uint8_t UPLOAD              = 0x02;
            constexpr uint8_t DOWNLOAD            = 0x03;
        }

        namespace information
        {
            constexpr uint8_t GET_OD_LIST_REQ    = 0x01;
            constexpr uint8_t GET_OD_LIST_RESP   = 0x02;
            constexpr uint8_t GET_OD_REQ         = 0x03;
            constexpr uint8_t GET_OD_RESP        = 0x04;
            constexpr uint8_t GET_ED_REQ         = 0x05;
            constexpr uint8_t GET_ED_RESP        = 0x06;
            constexpr uint8_t SDO_INFO_ERROR_REQ = 0x07;

            // ETG1000.5 chapter 6.1.4.3.9 and ETG1000.6 chapter 5.6.3.3
            enum ListType : uint16_t
            {
                NUMBER      = 0x00,
                ALL         = 0x01,
                RxPDO       = 0x02,
                TxPDO       = 0x03,
                BACKUP      = 0x04,
                SETTINGS    = 0x05
            };

            namespace ValueInfo
            {
                constexpr uint8_t UNIT_TYPE = (1 << 3);
                constexpr uint8_t DEFAULT   = (1 << 4);
                constexpr uint8_t MINIMUM   = (1 << 5);
                constexpr uint8_t MAXIMUM   = (1 << 6);

                std::string toString(uint8_t value_info);
            }

            struct EntryDescriptionRequest
            {
                uint16_t index;
                uint8_t  subindex;
                uint8_t  value_info;
            } __attribute__((__packed__));

            struct ObjectDescription
            {
                uint16_t index;
                DataType data_type;
                uint8_t  max_subindex;
                ObjectCode object_code;
            } __attribute__((__packed__));
            std::string toString(ObjectDescription const& object_description);

            struct EntryDescription
            {
                uint16_t  index;
                uint8_t   subindex;
                uint8_t   value_info;
                DataType  data_type;
                uint16_t  bit_length;
                uint16_t  access;
            } __attribute__((__packed__));
            std::string toString(EntryDescription const& entry_description);
        }

        namespace abort
        {
            constexpr uint32_t TOGGLE_BIT_NOT_ALTERNATED    = 0x05030000;
            constexpr uint32_t SDO_PROTOCOL_TIMEOUT         = 0x05040000;
            constexpr uint32_t COMMAND_SPECIFIER_INVALID    = 0x05040001;
            constexpr uint32_t INVALID_BLOCK_SIZE           = 0x05040002;
            constexpr uint32_t INVALID_SEQUENCE_NUMBER      = 0x05040003;
            constexpr uint32_t CRC_ERROR                    = 0x05040004;
            constexpr uint32_t OUT_OF_MEMORY                = 0x05040005;
            constexpr uint32_t UNSUPPORTED_ACCESS           = 0x06010000;
            constexpr uint32_t READ_WRITE_ONLY_ACCESS       = 0x06010001;
            constexpr uint32_t WRITE_READ_ONLY_ACCESS       = 0x06010002;
            constexpr uint32_t SUBINDEX0_CANNOT_BE_WRITTEN  = 0x06010003;
            constexpr uint32_t COMPLETE_ACCESS_UNSUPPORTED  = 0x06010004;
            constexpr uint32_t OBJECT_TOO_BIG               = 0x06010005;
            constexpr uint32_t OBJECT_MAPPED                = 0x06010006;
            constexpr uint32_t OBJECT_DOES_NOT_EXIST        = 0x06020000;
            constexpr uint32_t OBJECT_CANNOT_BE_MAPPED      = 0x06040041;
            constexpr uint32_t PDO_LENGTH_EXCEEDED          = 0x06040042;
            constexpr uint32_t PARAMETER_INCOMPATIBILITY    = 0x06040043;
            constexpr uint32_t INTERNAL_INCOMPATIBILITY     = 0x06040047;
            constexpr uint32_t HARDWARE_ERROR               = 0x06060000;
            constexpr uint32_t DATA_TYPE_LENGTH_MISMATCH    = 0x06070010;
            constexpr uint32_t DATA_TYPE_LENGTH_TOO_HIGH    = 0x06070012;
            constexpr uint32_t DATA_TYPE_LENGTH_TOO_LOW     = 0x06070013;
            constexpr uint32_t SUBINDEX_DOES_NOT_EXIST      = 0x06090011;
            constexpr uint32_t VALUE_RANGE_EXCEEDED         = 0x06090030;
            constexpr uint32_t VALUE_TOO_HIGH               = 0x06090031;
            constexpr uint32_t VALUE_TOO_LOW                = 0x06090032;
            constexpr uint32_t MODULE_LIST_MISMATCH         = 0x06090033;
            constexpr uint32_t MAX_LESS_THAN_MIN            = 0x06090036;
            constexpr uint32_t RESSOURCE_UNAVAILABLE        = 0x060A0023;
            constexpr uint32_t GENERAL_ERROR                = 0x08000000;
            constexpr uint32_t TRANSFER_ABORTED_GENERIC     = 0x08000020;
            constexpr uint32_t TRANSFER_ABORTED_LOCAL_CTRL  = 0x08000021;
            constexpr uint32_t TRANSFER_ABORTED_ESM_STATE   = 0x08000022;
            constexpr uint32_t DICTIONARY_GENERTION_FAILURE = 0x08000023;
            constexpr uint32_t NO_DATA_AVAILABLE            = 0x08000024;
        }

        char const* abort_to_str(uint32_t abort_code);
    }
}

#endif
