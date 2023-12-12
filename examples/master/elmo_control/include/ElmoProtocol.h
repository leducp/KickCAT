#ifndef KICKCAT_CAN_ELMO_PROTOCOL_H
#define KICKCAT_CAN_ELMO_PROTOCOL_H

namespace kickcat
{
    namespace pdo
    {
        /// \details See CANOpen DS402 document for description of fields.
        ///          Here fields are name "Torque" but actually refer to current in amps.
        struct Input
        {
            uint16_t status_word;
            int8_t   mode_of_operation_display;
            int8_t   padding;
            int32_t  actual_position;
            int32_t  actual_velocity;
            int16_t  demand_torque;
            int16_t  actual_torque;  ///< Actual torque in RTU.
            uint32_t dc_voltage;
            int32_t  digital_input;
            int16_t  analog_input;
            int32_t  demand_position;
        } __attribute__((packed));
        constexpr uint8_t tx_mapping[] = { 0x0A, 0x00, 0x0A, 0x1A, 0x0B, 0x1A, 0x0E, 0x1A, 0x11, 0x1A, 0x12, 0x1A, 0x13, 0x1A, 0x18, 0x1A, 0x1C, 0x1A, 0x1D, 0x1A, 0x1B, 0x1A };

        struct Output
        {
            uint16_t control_word;
            int8_t mode_of_operation;
            uint8_t padding;
            int16_t  target_torque;   ///< Target current in RTU, 1 RTU = Motor Rate Current (amps) / 1000.
            uint16_t max_torque;      ///< Maximum current in mAmps.
            int32_t  target_position;
            uint32_t velocity_offset;
            int32_t  digital_output;
        } __attribute__((packed));
        constexpr uint8_t rx_mapping[] = { 0x07, 0x00, 0x0A, 0x16, 0x0B, 0x16, 0x0C, 0x16, 0x0D, 0x16, 0x0F, 0x16, 0x17, 0x16, 0x1D, 0x16 };
    }
}

#endif
