#ifndef KICKCAT_CAN_ELMO_PROTOCOL_H
#define KICKCAT_CAN_ELMO_PROTOCOL_H

namespace kickcat
{
    struct SDOEntry
    {
        uint16_t index;         ///< Main index in the SDO dict
        uint8_t  subindex;      ///< Subindex in the SDO dict
    };

    namespace sdo
    {
        constexpr SDOEntry RxPDO { 0x1C12, 0 };
        constexpr SDOEntry TxPDO { 0x1C13, 0 };
    }

    namespace pdo
    {
        /// \details See CANOpen DS402 document for description of fields.
        ///          Here fields are name "Torque" but actually refer to current in amps.
        struct Input
        {
            uint16_t statusWord;
            int8_t   modeOfOperationDisplay;
            int8_t   padding;
            int32_t  actualPosition;
            int32_t  actualVelocity;
            int16_t  demandTorque;
            int16_t  actualTorque;  ///< Actual torque in RTU.
            uint32_t dcVoltage;
            int32_t  digitalInput;
            int16_t  analogInput;
            int32_t  demandPosition;
        } __attribute__((packed));
        constexpr uint8_t TxMapping[] = { 0x0A, 0x00, 0x0A, 0x1A, 0x0B, 0x1A, 0x0E, 0x1A, 0x11, 0x1A, 0x12, 0x1A, 0x13, 0x1A, 0x18, 0x1A, 0x1C, 0x1A, 0x1D, 0x1A, 0x1B, 0x1A };

        struct Output
        {
            uint16_t controlWord;
            int8_t modeOfOperation;
            uint8_t padding;
            int16_t  targetTorque;   ///< Target current in RTU, 1 RTU = Motor Rate Current (amps) / 1000.
            uint16_t maxTorque;      ///< Maximum current in mAmps.
            int32_t  targetPosition;
            uint32_t velocityOffset;
            int32_t  digitalOutput;
        } __attribute__((packed));
        constexpr uint8_t RxMapping[] = { 0x07, 0x00, 0x0A, 0x16, 0x0B, 0x16, 0x0C, 0x16, 0x0D, 0x16, 0x0F, 0x16, 0x17, 0x16, 0x1D, 0x16 };
    }
}

#endif
