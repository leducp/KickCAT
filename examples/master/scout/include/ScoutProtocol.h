#ifndef SCOUT_PROTOCOL_H
#define SCOUT_PROTOCOL_H

#include <cstdint>

namespace kickcat
{
    namespace pdo
    {
        /// \details See CANOpen DS402 document for description of fields.
        ///          Here fields are name "Torque" but actually refer to current in amps.
        struct Input
        {
            uint16_t status_word;       // 0x6041
            int32_t actual_position;    // 0x6064 ticks
            int32_t actual_velocity;    // 0x606c
            int16_t actual_torque;      // 0x6077
        } __attribute__((packed));
        constexpr uint32_t tx_mapping[] = {0x60410010, 0x60640020, 0x606C0020, 0x60770010}; // uint32_t is 0x[Address(4byte), SubIndex(2byte), typeSizeInBits(2byte)]
        constexpr uint32_t tx_mapping_count = sizeof(tx_mapping) / sizeof(uint32_t);

        struct Output
        {
            uint16_t control_word;      // 0x6040
            uint16_t mode_of_operation; // 0x6060
            int32_t  target_position;   // 0x607A ticks
            int16_t  target_torque;     // 0x6071
            int16_t  min_torque;      // 0x60E0
            int16_t  max_torque;      // 0x60E1
        } __attribute__((packed));
        constexpr uint32_t rx_mapping[] = {0x60400010, 0x60600010, 0x607A0020, 0x60710010, 0x60E00010, 0x60E10010}; // uint32_t is 0x[Address(4byte), SubIndex(2byte), typeSizeInBits(2byte)]
        constexpr uint32_t rx_mapping_count = sizeof(rx_mapping) / sizeof(uint32_t);
    }
}

#endif
