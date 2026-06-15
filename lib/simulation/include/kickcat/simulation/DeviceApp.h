#ifndef KICKCAT_SIMULATION_DEVICE_APP_H
#define KICKCAT_SIMULATION_DEVICE_APP_H

#include <memory>

#include <nlohmann/json_fwd.hpp>

namespace kickcat::slave { class Slave; }

namespace kickcat::sim
{
    // A slave-side device behaviour, advanced once per cyclic frame. Concrete
    // devices live behind makeDeviceApp(), so the simulator core never names a profile.
    class DeviceApp
    {
    public:
        virtual ~DeviceApp() = default;
        virtual void step() = 0;
    };

    // The device the config asks for (today: "ds402_motor"), or nullptr for none.
    // A new device type is one case here + a DeviceApp implementation.
    std::unique_ptr<DeviceApp> makeDeviceApp(slave::Slave& slave, nlohmann::json const& config);
}

#endif
