#ifndef MARVIN_PROTOCOL_H
#define MARVIN_PROTOCOL_H

#include <cstdint>

namespace kickcat
{
    namespace pdo
    {
        /// \details See CANOpen DS402 document for description of fields.
        ///          Here fields are name "Torque" but actually refer to current in amps.
        struct Input
        {
            uint16_t status_word;               // 0x6041
            int32_t actual_position;            // 0x6064 ticks
            int32_t actual_velocity;            // 0x606c
            int16_t actual_torque;              // 0x6077
            int16_t motor_temperature;          // 0x3107
            int32_t RECD;                       // 0x3109
            int32_t LTor_feedback;              // 0x310b
            uint16_t error_code;                // 0x603F
        } __attribute__((packed));
        // TxPDO mapping: slave sends, master receives (Input) - 0x[Index:16][SubIndex:8][SizeBits:8]
        constexpr uint32_t tx_mapping[] = {
            0x60410010,  // status_word        (0x6041:00, 16 bits)
            0x60640020,  // actual_position    (0x6064:00, 32 bits)
            0x606C0020,  // actual_velocity    (0x606C:00, 32 bits)
            0x60770010,  // actual_torque      (0x6077:00, 16 bits)
            0x31070010,  // motor_temperature  (0x3107:00, 16 bits)
            0x31090020,  // RECD              (0x3109:00, 32 bits)
            0x310B0020,  // LTor_feedback     (0x310B:00, 32 bits)
            0x603F0010,  // error_code        (0x603F:00, 16 bits)
        };
        constexpr uint32_t tx_mapping_count = sizeof(tx_mapping) / sizeof(uint32_t);

        struct Output
        {
            uint16_t control_word;      // 0x6040
            int32_t  target_position;   // 0x607A ticks
            int32_t  LTor_target;       // 0x310a
            int16_t  torque_offset;     // 0x60b2
        } __attribute__((packed));
        // RxPDO mapping: master sends, slave receives (Output)
        constexpr uint32_t rx_mapping[] = {
            0x60400010,  // control_word      (0x6040:00, 16 bits)
            0x607A0020,  // target_position   (0x607A:00, 32 bits)
            0x310A0020,  // LTor_target       (0x310A:00, 32 bits)
            0x60B20010,  // torque_offset     (0x60B2:00, 16 bits)
        };
        constexpr uint32_t rx_mapping_count = sizeof(rx_mapping) / sizeof(uint32_t);
    }
}

#endif
