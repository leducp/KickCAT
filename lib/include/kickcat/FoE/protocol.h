#ifndef KICKCAT_FOE_PROTOCOL_H
#define KICKCAT_FOE_PROTOCOL_H

#include "kickcat/protocol.h"

namespace kickcat::FoE
{
    struct Header   // ETG1000.6 chapter 5.8
    {
        uint8_t  opcode;
        uint8_t  reserved;
        uint32_t value;     // READ, WRITE: password (0: unused). DATA, ACK: packet number (ACK 0: write accepted).
                            // ERROR: error code. BUSY: done (low 16 bits) and entire (high 16 bits).
    } __attribute__((__packed__));

    // A mailbox of N bytes carries N - OVERHEAD bytes of file
    constexpr uint16_t OVERHEAD = sizeof(mailbox::Header) + sizeof(Header);

    namespace opcode
    {
        constexpr uint8_t READ  = 0x01;
        constexpr uint8_t WRITE = 0x02;
        constexpr uint8_t DATA  = 0x03;
        constexpr uint8_t ACK   = 0x04;
        constexpr uint8_t ERROR = 0x05;
        constexpr uint8_t BUSY  = 0x06;
    }

    namespace result    // ETG1000.6 Table 93 and ETG.1020 Table 61
    {
        constexpr uint32_t NOT_DEFINED            = 0x8000;
        constexpr uint32_t NOT_FOUND              = 0x8001;
        constexpr uint32_t ACCESS_DENIED          = 0x8002;
        constexpr uint32_t DISK_FULL              = 0x8003;
        constexpr uint32_t ILLEGAL                = 0x8004;
        constexpr uint32_t PACKET_NUMBER_WRONG    = 0x8005;
        constexpr uint32_t ALREADY_EXISTS         = 0x8006;
        constexpr uint32_t NO_USER                = 0x8007;
        constexpr uint32_t BOOTSTRAP_ONLY         = 0x8008;
        constexpr uint32_t NOT_BOOTSTRAP          = 0x8009;
        constexpr uint32_t NO_RIGHTS              = 0x800A;
        constexpr uint32_t PROGRAM_ERROR          = 0x800B;
        constexpr uint32_t CHECKSUM_WRONG         = 0x800C;
        constexpr uint32_t FIRMWARE_DOES_NOT_FIT  = 0x800D;
        constexpr uint32_t NO_FILE_TO_READ        = 0x800F;
        constexpr uint32_t NO_FILE_HEADER         = 0x8010;
        constexpr uint32_t FLASH_PROBLEM          = 0x8011;
        constexpr uint32_t FILE_INCOMPATIBLE      = 0x8012;
    }

    /// \brief Map TFTP style error codes (sent without the 0x8000 offset by some devices, cf. ETG.1020 18.3)
    ///        to their FoE equivalent. Other codes are returned untouched.
    uint32_t normalizeError(uint32_t code);

    /// \brief Describe an FoE error code, or a local FoE message status (mailbox::request::MessageStatus::FOE_*)
    char const* errorToString(uint32_t code);
}

#endif
