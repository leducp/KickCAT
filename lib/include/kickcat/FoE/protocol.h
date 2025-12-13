#ifndef KICKCAT_FOE_PROTOCOL_H
#define KICKCAT_FOE_PROTOCOL_H

#include "kickcat/protocol.h"

namespace kickcat::FoE
{
    struct Header   // ETG1000.6 chapter 5.8.x
    {
        uint8_t opcode;
        uint8_t reserved;
    } __attribute__((__packed__));

    namespace read
    {
        struct Header   // ETG1000.6 chapter 5.8.1
        {
            uint16_t password;  // 0 == password unused
        } __attribute__((__packed__));
        // Followed by the file name in the data section
    }

    namespace write
    {
        struct Header   // ETG1000.6 chapter 5.8.2
        {
            uint16_t password;  // 0 == password unused
        } __attribute__((__packed__));
        // Followed by the file name in the data section
    }

    namespace data
    {
        struct Header   // ETG1000.6 chapter 5.8.3
        {
            uint16_t packet_number;  // 1 - 0xFFFFFFFF
        } __attribute__((__packed__));
        // Followed by a file chunk in the data section
    }

    namespace ack
    {
        struct Header   // ETG1000.6 chapter 5.8.4
        {
            uint16_t packet_number;  // 1 - 0xFFFFFFFF
        } __attribute__((__packed__));
    }

    namespace error
    {
        struct Header   // ETG1000.6 chapter 5.8.5
        {
            uint16_t error_code;
        } __attribute__((__packed__));
        // Followed by an optional error string in the data section
    }

    namespace buys
    {
        struct Header   // ETG1000.6 chapter 5.8.6
        {
            uint16_t done;
            uint16_t entire;
        } __attribute__((__packed__));
        // Followed by an optional busy string in the data section
    }

    namespace opcode
    {
        constexpr uint8_t READ  = 0x01;
        constexpr uint8_t WRITE = 0x02;
        constexpr uint8_t DATA  = 0x03;
        constexpr uint8_t ACK   = 0x04;
        constexpr uint8_t ERROR = 0x05;
        constexpr uint8_t BUSY  = 0x06;
    }

    namespace result
    {
        constexpr uint16_t NOT_DEFINED         = 0x8000;
        constexpr uint16_t NOT_FOUND           = 0x8001;
        constexpr uint16_t ACCESS_DENIED       = 0x8002;
        constexpr uint16_t DISK_FULL           = 0x8003;
        constexpr uint16_t ILLEGAL             = 0x8004;
        constexpr uint16_t PACKET_NUMBER_WRONG = 0x8005;
        constexpr uint16_t ALREADY_EXISTS      = 0x8006;
        constexpr uint16_t NO_USER             = 0x8007;
        constexpr uint16_t BOOTSTRAP_ONLY      = 0x8008;
        constexpr uint16_t NOT_BOOTSTRAP       = 0x8009;
        constexpr uint16_t NO_RIGHTS           = 0x800A;
        constexpr uint16_t PROGRAM_ERROR       = 0x800B;

        char const* toString(uint16_t result);
    }
}

#endif
