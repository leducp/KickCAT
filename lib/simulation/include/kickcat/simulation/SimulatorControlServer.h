#ifndef KICKCAT_SIMULATION_SIMULATOR_CONTROL_SERVER_H
#define KICKCAT_SIMULATION_SIMULATOR_CONTROL_SERVER_H

#include <string>

#include "kickcat/EmulatedNetwork.h"
#include "kickcat/SimulatorControl.h"

namespace kickcat::sim
{
    // Simulator-side listener: drain() applies every pending ControlCommand onto
    // the emulated network and acks each. New command types extend apply().
    class SimulatorControlServer
    {
    public:
        SimulatorControlServer(EmulatedNetwork& network, size_t node_count);

        void attach(std::string const& name);
        void drain();   // no-op until attached

        // Publish the latest frame-timing window for the host to display.
        // No-op until attached.
        void publishStats(SimStats const& s);

    private:
        ControlEvent apply(ControlCommand const& cmd);

        ControlChannel   channel_;
        EmulatedNetwork& network_;
        size_t           node_count_;
        bool             attached_{false};
    };
}

#endif
