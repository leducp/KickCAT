/// @file hello_pubsub.cc
/// @brief KickMsg publish/subscribe via the Node API.
///
/// Two nodes communicate over a named topic:
///   - "sensor" advertises "temperature" and publishes readings
///   - "display" subscribes to "temperature" and prints them
///
/// Single-process for simplicity; in production, each node lives in
/// its own process sharing the same prefix.

#include <cstdint>
#include <cstring>
#include <iostream>
#include <thread>

#include <kickmsg/Node.h>

using namespace kickcat;

struct TemperatureReading
{
    uint32_t sensor_id;
    float    celsius;
};

int main()
{
    // Clean up leftovers
    kickmsg::SharedMemory::unlink("/demo_temperature");

    // Sensor node: advertises "temperature"
    kickmsg::Node sensor("sensor", "demo");
    auto pub = sensor.advertise("temperature");

    // Display node: subscribes to "temperature"
    kickmsg::Node display("display", "demo");
    auto sub = display.subscribe("temperature");

    // Publish a few readings
    TemperatureReading readings[] = {
        {1, 22.5f}, {2, 19.8f}, {1, 23.1f}, {3, 31.4f}, {2, 20.0f}
    };

    for (auto const& r : readings)
    {
        if (pub.send(&r, sizeof(r)) < 0)
        {
            std::cerr << "Failed to send reading from sensor " << r.sensor_id << "\n";
        }
    }

    // Receive and display
    while (auto sample = sub.try_receive())
    {
        TemperatureReading r;
        std::memcpy(&r, sample->data(), sizeof(r));
        std::cout << "Sensor " << r.sensor_id << ": " << r.celsius << " C\n";
    }

    kickmsg::SharedMemory::unlink("/demo_temperature");
    std::cout << "Done.\n";
    return 0;
}
