#ifndef KICKCAT_CAN_INGENIA_PROTOCOL_H
#define KICKCAT_CAN_INGENIA_PROTOCOL_H

namespace kickcat
{
    namespace pdo
    {
        /// \details See CANOpen DS402 document for description of fields.
        ///          Here fields are name "Torque" but actually refer to current in amps.
        struct Input
        {
            uint16_t status_word;               // 0x2011
            int32_t actual_position;            // 0x2030 ticks
            float actual_velocity;              // 0x2031 rev/s
            float demand_torque;                // 0x2053 Nm
            float actual_torque;                // 0x2029 Nm
            float dc_voltage;                   // 0x2060 V
            int32_t demand_position;            // 0x2078 ticks
            int32_t auxiliar_position;          // 0x2033
            uint16_t mode_of_operation_display; // 0x2015
        } __attribute__((packed));
        constexpr uint32_t tx_mapping[] = {0x20110010, 0x20300020, 0x20310020, 0x20530020, 0x20290020, 0x20600020, 0x20780020, 0x20330020, 0x20150010}; // uint32_t is 0x[Address(4byte), SubIndex(2byte), typeSizeInBits(2byte)]
        constexpr uint32_t tx_mapping_count = sizeof(tx_mapping) / sizeof(uint32_t);

        struct Output
        {
            uint16_t control_word;      // 0x2010
            uint16_t mode_of_operation; // 0x2014
            float target_torque;        // 0x2022 Nm
            float max_current;          // 0x21E0 A
            int32_t target_position;    // 0x2020 ticks
        } __attribute__((packed));
        constexpr uint32_t rx_mapping[] = {0x20100010, 0x20140010, 0x20220020, 0x21E00020, 0x20200020}; // uint32_t is 0x[Address(4byte), SubIndex(2byte), typeSizeInBits(2byte)]
        constexpr uint32_t rx_mapping_count = sizeof(rx_mapping) / sizeof(uint32_t);
    }
}

#endif
