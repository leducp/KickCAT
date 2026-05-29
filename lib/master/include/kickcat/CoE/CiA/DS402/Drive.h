#ifndef KICKCAT_COE_CiA_DS402_DRIVE_H
#define KICKCAT_COE_CiA_DS402_DRIVE_H

#include <cstdint>

#include "kickcat/CoE/CiA/DS402/StateMachine.h"

namespace kickcat
{
    class Bus;
    struct Slave;
}

namespace kickcat::CoE::CiA::DS402
{
    // Defaults are placeholders, not identity. Set realistic values before
    // calling any SI accessor. All three fields must be strictly positive;
    // setUnits() rejects zero or negative values.
    struct UnitConfig
    {
        double encoder_ticks_per_rev = 1.0;
        double gear_ratio            = 1.0;
        double rated_torque_Nm       = 1.0;
    };

    // Owns one DS402-compliant slave's PDO image and state machine.
    //
    // Lifetime: the Bus and Slave references passed to the constructor must
    // outlive the Drive instance. Drive stores raw pointers to both.
    //
    // Usage:
    //   Drive drive(bus, slave);                       // PRE_OP
    //   drive.configure(POSITION_CYCLIC);              // SDO + mapPDO
    //   bus.createMapping(iomap);                      // -> SAFE_OP
    //   drive.attach();                                // capture PDO buffer
    //   // setpoints must be set before enabling, else PDO sends zeros:
    //   drive.setTargetPositionRaw(drive.actualPositionRaw());
    //   drive.enable();
    //   // per cycle:
    //   drive.update();
    class Drive
    {
    public:
        // Padding bytes after the int8 mode fields keep all multi-byte
        // members at even offsets, avoiding unaligned access traps on
        // strict-alignment targets. The PDO mapping arrays include matching
        // dummy entries (CiA 301 index 0x0000) so the wire layout agrees.
        struct Input
        {
            uint16_t status_word;                // 0x6041, off 0
            int8_t   mode_of_operation_display;  // 0x6061, off 2
            uint8_t  _padding;                   // off 3
            int32_t  actual_position;            // 0x6064, off 4
            int32_t  actual_velocity;            // 0x606C, off 8
            int16_t  actual_torque;              // 0x6077, off 12
            uint16_t error_code;                 // 0x603F, off 14
        } __attribute__((packed));

        struct Output
        {
            uint16_t control_word;        // 0x6040, off 0
            int8_t   mode_of_operation;   // 0x6060, off 2
            uint8_t  _padding;            // off 3
            int32_t  target_position;     // 0x607A, off 4
            int32_t  target_velocity;     // 0x60FF, off 8
            int16_t  target_torque;       // 0x6071, off 12
        } __attribute__((packed));

        Drive(Bus& bus, Slave& slave);

        // Best-effort disable: writes DISABLE_VOLTAGE controlword and zeros
        // setpoints into the RxPDO so the next bus cycle takes the motor
        // out of Operation-Enabled. The destructor cannot itself drive the
        // bus, so cycling must continue at least once after destruction.
        ~Drive();

        // PRE_OP: write mode via SDO and map canonical PDOs. rx_pdo_map /
        // tx_pdo_map default to standard CiA 402 indices; override for vendors
        // that use different PDO mapping objects (e.g. marvin uses 0x1601/0x1A01).
        // The mode is stored and re-applied to the RxPDO buffer in attach().
        void configure(control::ControlMode mode,
                       uint16_t rx_pdo_map = 0x1600,
                       uint16_t tx_pdo_map = 0x1A00);

        // Optional SDO writes, between configure() and bus.createMapping().
        void setInterpolationTimePeriod(uint8_t value, int8_t index);
        void setMaxTorqueSDO(uint16_t per_mille);

        // After bus.createMapping(), capture the typed PDO pointers and
        // initialize the RxPDO buffer:
        //   * control_word = 0       (state machine will set it on update())
        //   * mode_of_operation = configured mode (so PDO doesn't override
        //     the SDO-set mode every cycle with a zero byte)
        //   * target_position = actual_position (captured once, so enabling
        //     the motor doesn't command a slam to position 0)
        //   * target_velocity, target_torque = 0
        // Throws if bus.createMapping() didn't populate the slave's PDO data.
        void attach();

        // Validates the UnitConfig (all three fields strictly positive) and
        // caches the conversion factors. Call before any SI accessor.
        void setUnits(UnitConfig const& units);

        void update();

        void enable()  { sm_.enable();  }
        void disable() { sm_.disable(); }
        bool isEnabled() const { return sm_.isEnabled(); }
        bool isFaulted() const { return sm_.isFaulted(); }

        uint16_t statusWord()             const { return in_->status_word;  }
        uint16_t controlWord()            const { return out_->control_word; }
        uint16_t errorCode()              const { return in_->error_code;   }
        int8_t   modeOfOperationDisplay() const { return in_->mode_of_operation_display; }

        void setModeOfOperationRaw(int8_t mode)        { out_->mode_of_operation = mode; }
        void setTargetPositionRaw (int32_t ticks)      { out_->target_position   = ticks; }
        void setTargetVelocityRaw (int32_t ticks_per_s){ out_->target_velocity   = ticks_per_s; }
        void setTargetTorqueRaw   (int16_t per_mille)  { out_->target_torque     = per_mille; }

        // SI setpoints. Output-shaft frame. Out-of-range values are clamped
        // to the underlying int range, not silently wrapped.
        void setTargetPosition(double rad);
        void setTargetVelocity(double rad_per_s);
        void setTargetTorque  (double nm);

        int32_t actualPositionRaw() const { return in_->actual_position; }
        int32_t actualVelocityRaw() const { return in_->actual_velocity; }
        int16_t actualTorqueRaw()   const { return in_->actual_torque;   }

        double actualPosition() const;
        double actualVelocity() const;
        double actualTorque()   const;

    private:
        Bus*         bus_;
        Slave*       slave_;
        Output*      out_ = nullptr;
        Input const* in_  = nullptr;
        StateMachine sm_{};
        UnitConfig   units_{};
        control::ControlMode mode_ = control::NO_MODE;

        // Cached conversion factors, recomputed in setUnits().
        double pos_ticks_per_rad_     = 0.0;
        double torque_per_mille_per_nm_ = 0.0;
    };
}

#endif
