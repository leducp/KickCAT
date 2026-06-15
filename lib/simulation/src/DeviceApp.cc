#include "kickcat/simulation/DeviceApp.h"

#include <nlohmann/json.hpp>

#include "kickcat/simulation/devices/Ds402Motor.h"

namespace kickcat::sim
{
    using json = nlohmann::json;

    std::unique_ptr<DeviceApp> makeDeviceApp(slave::Slave& slave, json const& config)
    {
        if (config.value("ds402_motor", false))
        {
            Ds402Motor::Params mp;
            mp.cycle_s       = config.value("motor_cycle_ms",     1.0) / 1000.0;
            mp.pos_bandwidth = config.value("motor_bandwidth",    mp.pos_bandwidth);
            mp.damping       = config.value("motor_damping",      mp.damping);
            mp.vel_tau       = config.value("motor_vel_tau",      mp.vel_tau);
            mp.inertia       = config.value("motor_inertia",      mp.inertia);
            mp.friction      = config.value("motor_friction",     mp.friction);
            mp.torque_scale  = config.value("motor_torque_scale", mp.torque_scale);
            mp.max_velocity  = config.value("motor_max_velocity", mp.max_velocity);
            return std::make_unique<Ds402Motor>(slave, mp);
        }
        return nullptr;
    }
}
