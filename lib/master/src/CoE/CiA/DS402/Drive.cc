#include <algorithm>
#include <cstddef>
#include <iterator>
#include <limits>

#include "kickcat/CoE/CiA/DS402/Drive.h"
#include "kickcat/Bus.h"
#include "kickcat/Error.h"
#include "kickcat/Slave.h"
#include "kickcat/Units.h"

namespace kickcat::CoE::CiA::DS402
{
    namespace
    {
        // Two tables per Drive::PaddingStyle: _DUMMY pads the INT8 mode entry with
        // a CiA-301 dummy (0x00000008), _WIDE maps the object as a 16-bit word.
        constexpr uint32_t TX_MAPPING_DUMMY[] = {
            0x60410010, 0x60610008, 0x00000008,
            0x60640020, 0x606C0020, 0x60770010, 0x603F0010
        };
        constexpr uint32_t RX_MAPPING_DUMMY[] = {
            0x60400010, 0x60600008, 0x00000008,
            0x607A0020, 0x60FF0020, 0x60710010
        };
        constexpr uint32_t TX_MAPPING_WIDE[] = {
            0x60410010, 0x60610010,
            0x60640020, 0x606C0020, 0x60770010, 0x603F0010
        };
        constexpr uint32_t RX_MAPPING_WIDE[] = {
            0x60400010, 0x60600010,
            0x607A0020, 0x60FF0020, 0x60710010
        };

        // Clamp before cast: static_cast<int>(double) is UB when the
        // truncated value falls outside the destination type's range.
        template<typename T>
        T saturate(double v)
        {
            return static_cast<T>(std::clamp(v,
                static_cast<double>(std::numeric_limits<T>::min()),
                static_cast<double>(std::numeric_limits<T>::max())));
        }
    }

    // Cross-check: the bit-lengths declared in the PDO mapping arrays must
    // sum to the byte size of the matching struct.
    namespace
    {
        constexpr size_t mappingBytes(uint32_t const* m, size_t n)
        {
            size_t bits = 0;
            for (size_t i = 0; i < n; ++i) { bits += (m[i] & 0xFF); }
            return bits / 8;
        }
    }
    static_assert(mappingBytes(TX_MAPPING_DUMMY, std::size(TX_MAPPING_DUMMY)) == sizeof(Drive::Input));
    static_assert(mappingBytes(RX_MAPPING_DUMMY, std::size(RX_MAPPING_DUMMY)) == sizeof(Drive::Output));
    static_assert(mappingBytes(TX_MAPPING_WIDE,  std::size(TX_MAPPING_WIDE))  == sizeof(Drive::Input));
    static_assert(mappingBytes(RX_MAPPING_WIDE,  std::size(RX_MAPPING_WIDE))  == sizeof(Drive::Output));

    Drive::Drive(Bus& bus, Slave& slave)
        : bus_(&bus)
        , slave_(&slave)
    {
        setUnits(units_);
    }

    Drive::~Drive()
    {
        if (out_ != nullptr)
        {
            // target_position is held at the last measured actual_position so
            // the motor does not jump while the drive processes the disable.
            // velocity/torque setpoints of 0 are safe stops in their modes.
            out_->control_word    = control::word::DISABLE_VOLTAGE;
            out_->target_position = in_->actual_position;
            out_->target_velocity = 0;
            out_->target_torque   = 0;
        }
    }

    void Drive::configure(control::ControlMode mode,
                          uint16_t rx_pdo_map,
                          uint16_t tx_pdo_map,
                          PaddingStyle padding)
    {
        mode_ = mode;

        int8_t mode_byte = static_cast<int8_t>(mode);
        bus_->writeSDO(*slave_, 0x6060, 0, Bus::Access::PARTIAL, &mode_byte, sizeof(mode_byte));

        auto apply = [&](uint32_t const* rx, size_t rxn, uint32_t const* tx, size_t txn)
        {
            mapPDO(*bus_, *slave_, rx_pdo_map, rx, static_cast<uint8_t>(rxn), 0x1C12);
            mapPDO(*bus_, *slave_, tx_pdo_map, tx, static_cast<uint8_t>(txn), 0x1C13);
        };

        if (padding == PaddingStyle::WidenObject)
        {
            apply(RX_MAPPING_WIDE, std::size(RX_MAPPING_WIDE), TX_MAPPING_WIDE, std::size(TX_MAPPING_WIDE));
        }
        else if (padding == PaddingStyle::DummyEntry)
        {
            apply(RX_MAPPING_DUMMY, std::size(RX_MAPPING_DUMMY), TX_MAPPING_DUMMY, std::size(TX_MAPPING_DUMMY));
        }
        else   // Auto: the dummy entry is spec-conformant; on a slave that rejects
        {      // it the mapping write aborts (mapPDO throws), so retry widened.
            try
            {
                apply(RX_MAPPING_DUMMY, std::size(RX_MAPPING_DUMMY), TX_MAPPING_DUMMY, std::size(TX_MAPPING_DUMMY));
            }
            catch (std::exception const&)
            {
                apply(RX_MAPPING_WIDE, std::size(RX_MAPPING_WIDE), TX_MAPPING_WIDE, std::size(TX_MAPPING_WIDE));
            }
        }
    }

    void Drive::setInterpolationTimePeriod(uint8_t value, int8_t index)
    {
        bus_->writeSDO(*slave_, 0x60C2, 1, Bus::Access::PARTIAL, &value, sizeof(value));
        bus_->writeSDO(*slave_, 0x60C2, 2, Bus::Access::PARTIAL, &index, sizeof(index));
    }

    void Drive::setMaxTorqueSDO(uint16_t per_mille)
    {
        bus_->writeSDO(*slave_, 0x6072, 0, Bus::Access::PARTIAL, &per_mille, sizeof(per_mille));
    }

    void Drive::attach()
    {
        if (slave_->output.data == nullptr or slave_->input.data == nullptr)
        {
            THROW_ERROR("Drive::attach() called before bus.createMapping() populated the slave PDO buffers");
        }

        out_ = reinterpret_cast<Output*>(slave_->output.data);
        in_  = reinterpret_cast<Input const*>(slave_->input.data);

        out_->control_word      = 0;
        out_->mode_of_operation = static_cast<int8_t>(mode_);
        out_->target_position   = in_->actual_position;
        out_->target_velocity   = 0;
        out_->target_torque     = 0;
    }

    void Drive::setUnits(UnitConfig const& units)
    {
        if (units.encoder_ticks_per_rev <= 0.0
            or units.gear_ratio <= 0.0
            or units.rated_torque_Nm <= 0.0)
        {
            THROW_ERROR("Drive::setUnits requires strictly positive encoder_ticks_per_rev, gear_ratio, rated_torque_Nm");
        }

        units_ = units;
        pos_ticks_per_rad_       = units.encoder_ticks_per_rev * units.gear_ratio / tau;
        torque_per_mille_per_nm_ = 1000.0 / units.rated_torque_Nm;
    }

    void Drive::update()
    {
        sm_.update(in_->status_word);
        out_->control_word = sm_.controlWord();
    }

    void Drive::setTargetPosition(double rad)
    {
        out_->target_position = saturate<int32_t>(rad * pos_ticks_per_rad_);
    }

    void Drive::setTargetVelocity(double rad_per_s)
    {
        out_->target_velocity = saturate<int32_t>(rad_per_s * pos_ticks_per_rad_);
    }

    void Drive::setTargetTorque(double nm)
    {
        out_->target_torque = saturate<int16_t>(nm * torque_per_mille_per_nm_);
    }

    double Drive::actualPosition() const
    {
        return static_cast<double>(in_->actual_position) / pos_ticks_per_rad_;
    }

    double Drive::actualVelocity() const
    {
        return static_cast<double>(in_->actual_velocity) / pos_ticks_per_rad_;
    }

    double Drive::actualTorque() const
    {
        return static_cast<double>(in_->actual_torque) / torque_per_mille_per_nm_;
    }
}
