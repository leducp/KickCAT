///////////////////////////////////////////////////////////////////////////////
/// \copyright Wandercraft
//////////////////////////////////////////////////////////////////////////////

#ifndef WDC_ELMO_PROTOCOL_H
#define WDC_ELMO_PROTOCOL_H

#include "kickcat/Bus.h"
#include "CANOpenStateMachine.h"

struct SDOEntry
{
    uint16_t index;         ///< Main index in the SDO dict
    uint8_t  subindex;      ///< Subindex in the SDO dict
    char const* name{""};   ///< Name of SDO, only for print purpose
};

namespace hal
{
    typedef float           float32_t;
    
    namespace pdo
    {
        namespace elmo
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
            //TODO TorqueOffset: add torque offset field: int16_t {0x18, 0x16} and increment RxMapping count from 0x07 to 0x08
        }

    }

    namespace sdo
    {
        namespace elmo
        {
            constexpr uint32_t STOP_DECELERATION_VALUE = 2147483647;

            constexpr int32_t ENABLE_STOPS_PARAM = 1;
            constexpr int32_t ENABLE_WATCHDOG_PARAM = 1;
            constexpr int32_t SYNC_STOPS_PARAM = 1;

            // Reference value for Elmo embedded control configuration.
            constexpr float32_t EXPECTED_KI_VELOCITY = 0.0;
            constexpr float32_t EXPECTED_ACCEL_FEEDFORWARD = 0.0;
            constexpr float32_t EXPECTED_VEL_FEEDFORWARD = 1.0;

            // On elmo boards, the command to save the values to flash is to send the word
            // "save" written in ascii hex values, in little-endian order.
            constexpr uint32_t const SAVE_DATA_PARAM = 0x65766173U;

            namespace word
            {
                // Name of the registers
                constexpr uint16_t IDENTITY_OBJECT         = 0x1018;
                constexpr uint16_t USER_INTEGER            = 0x2F00;
                constexpr uint16_t RATED_TORQUE            = 0x6076;
                constexpr uint16_t RATED_CURRENT           = 0x6075;
                constexpr uint16_t MAX_MOTOR_SPEED         = 0x6080;
                constexpr uint16_t MAX_PROFILE_VELOCITY    = 0x607F;
                constexpr uint16_t QUICK_STOP_DECELERATION = 0x6085;
                constexpr uint16_t STORE_PARAMETERS        = 0x1010;
                constexpr uint16_t KP                      = 0x3113;
                constexpr uint16_t KI                      = 0x310C;
                constexpr uint16_t KV                      = 0x3119;
                constexpr uint16_t FF                      = 0x3087;
                constexpr uint16_t MAX_TRACKING_ERROR      = 0x3079;
            }

            constexpr SDOEntry LOW_STOP            { word::USER_INTEGER, 2,    "Low stop"         };
            constexpr SDOEntry HIGH_STOP           { word::USER_INTEGER, 3,    "High stop"        };
            constexpr SDOEntry SERIAL_NUMBER       { word::IDENTITY_OBJECT, 4, "Serial number"    };
            constexpr SDOEntry JSP_VERSION         { word::USER_INTEGER, 5,    "JSP version"      };

            constexpr SDOEntry CANOPEN_STATUS_READ { 0x6041, 0, "CANopen Status Read" };
            constexpr SDOEntry CANOPEN_STATUS_SET  { 0x6040, 0, "CANopen Status Set" };

            constexpr SDOEntry BOARD_TEMPERATURE   { word::USER_INTEGER, 4 , "Board temperature"  }; // Uncompensated board temperature
            constexpr SDOEntry PEAK_CURRENT        { word::USER_INTEGER, 7 , "Peak current"       }; // peak current (mA) config
            constexpr SDOEntry OVER_VOLTAGE        { word::USER_INTEGER, 8 , "Overvoltage"        }; // Overvoltage config
            constexpr SDOEntry DIRECTION           { word::USER_INTEGER, 9 , "Direction"          }; // Direction config
            constexpr SDOEntry RPM                 { word::USER_INTEGER, 10, "RPM"                }; // RPM config
            constexpr SDOEntry KP                  { word::USER_INTEGER, 11, "Kp"                 }; // KP config (exposed by JSP for Atalante)
            constexpr SDOEntry KI                  { word::USER_INTEGER, 12, "Ki"                 }; // KI config (exposed by JSP for Atalante)
            constexpr SDOEntry ULTRASOUND          { word::USER_INTEGER, 13, "Ultrasound"         }; // Ultrasound sensor config
            constexpr SDOEntry BACKLIGHT_TRANSFER  { word::USER_INTEGER, 14, "Backlight transfer" }; // Transfer button backlight config
            constexpr SDOEntry WATCHDOG_1          { word::USER_INTEGER, 15, "Watchdog 1"         }; // Watchdog config 1
            constexpr SDOEntry WATCHDOG_2          { word::USER_INTEGER, 16, "Watchdog 2"         }; // Watchdog config 2 (IL[4] = 7)
            constexpr SDOEntry TRANSFER_BUTTON     { word::USER_INTEGER, 17, "Transfer button"    }; // Transfer button (IL[6] = 7)
            constexpr SDOEntry SYNC_STOPS          { word::USER_INTEGER, 18, "Sync stops"         }; // Ask JSP to sync the stops when set to 1
            constexpr SDOEntry ENABLE_WATCHDOG     { word::USER_INTEGER, 19, "Enable watchdog"    }; // Enable JSP watchdog when set to 1 - you cannot reset it afterward!
            constexpr SDOEntry ENABLE_STOPS        { word::USER_INTEGER, 20, "Enable stops"       }; // Enable JSP stops when set to 1 - you cannot reset it afterward!
            constexpr SDOEntry NOMINAL_CURRENT     { word::RATED_CURRENT, 0, "Nominal current"    }; // mA
            constexpr SDOEntry RATED_TORQUE_FACTOR { word::RATED_TORQUE,  0, "Rated torque factor"};
            constexpr SDOEntry MAX_MOTOR_SPEED     { word::MAX_MOTOR_SPEED, 0, "Max motor speed"  };
            constexpr SDOEntry MAX_PROFILED_VELOCITY { word::MAX_PROFILE_VELOCITY, 0, "Max profiled velocity" };
            constexpr SDOEntry STOP_DECELERATION   { word::QUICK_STOP_DECELERATION, 0, "Motor stop deceleration" };
            constexpr SDOEntry STORE_PARAMETERS    { word::STORE_PARAMETERS, 1, "Store parameter" };
            constexpr SDOEntry RxPDO               { 0x1C12, 0, "RxPDO" };
            constexpr SDOEntry TxPDO               { 0x1C13, 0, "TxPDO" };
            constexpr SDOEntry POSITION_KP_GAIN    { word::KP, 3, "Elmo position KP"};
            constexpr SDOEntry VELOCITY_KP_GAIN    { word::KP, 2, "Elmo velocity KP"};
            constexpr SDOEntry VELOCITY_KI_GAIN    { word::KI, 2, "Elmo velocity KI"};
            constexpr SDOEntry VELOCITY_FILTER_FREQ{ word::KV, 1, "Elmo velocity filter frequency"};
            constexpr SDOEntry VELOCITY_FILTER_DAMP{ word::KV, 2, "Elmo velocity filter damping"};
            constexpr SDOEntry FEEDFORWARD_ACCELERATION{ word::FF, 1, "Elmo acceleration feedforward"};
            constexpr SDOEntry FEEDFORWARD_VELOCITY{ word::FF, 2, "Elmo velocity feedforward"};
            constexpr SDOEntry MAX_ERROR_VELOCITY  { word::MAX_TRACKING_ERROR, 2, "Elmo max velocity error"};
            constexpr SDOEntry MAX_ERROR_POSITION  { word::MAX_TRACKING_ERROR, 3, "Elmo max position error"};
            constexpr SDOEntry DYNAMIC_BRAKE_SPEED_LIMIT { 0x3229, 1, "Dynamic brake speed limit"    }; // Set to 0 disables dynamic brake (phase shorting)
            constexpr SDOEntry CURRENT_LIMIT       { 0x3120, 1 , "Current safety limit activated"  };
        }
    }


    constexpr int32_t TRIGGER_WATCHDOG_BIT = (1<<19); //< Bit to verify whether watchdog was triggerd.
    constexpr int32_t LEG_LIGHT_BIT = (1<<18);

    /// Mask of the watchdog counter.
    constexpr int32_t WATCHDOG_COUNTER_MASK = 0xFFFFFF;

    constexpr int32_t SUPPORTED_JSP_VERSION = 600000000;

}

#endif
