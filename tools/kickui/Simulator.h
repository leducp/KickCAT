#ifndef KICKCAT_TOOLS_KICKUI_SIMULATOR_H
#define KICKCAT_TOOLS_KICKUI_SIMULATOR_H

#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "kickcat/SimulatorControl.h"

namespace kickcat::kickui
{
    // One row of the sim-scene editor: the slave's config file + its parent in
    // the tree (-1 = master/root; else an EARLIER slave index, keeping it acyclic).
    struct SimSlave
    {
        char config[256] = "simulation/slave_configs/ecat402-drive.json";
        int  parent      = -1;
    };

    // The simulator editor scene: slave rows + ring flag, scene persistence, the
    // network_simulator topology file, and the editor<->scan order mapping.
    class SimScene
    {
    public:
        std::vector<SimSlave> slaves{SimSlave{}};
        bool redundancy = false;   // ring: -r + redundancy_injection

        // Per-slave downstream port on its parent; -1 for the root or when the
        // parent has no free port left (more than 3 children).
        std::vector<int> assignedPorts() const;

        // Save/load the editor scene as a small text file, so a topology is easy
        // to reproduce and share. Return false (and set message) on failure.
        bool save(char const* path, std::string& message) const;
        bool load(char const* path, std::string& message);

        // Serialize into a network_simulator --topology file: a master injection
        // point on the root + one connect() edge per non-root slave. False (and
        // sets error) if the tree is not a single segment.
        bool writeTopologyFile(std::string const& path, std::string& error) const;

        // scan position -> editor S#, from the configured tree (the master scans
        // in EtherCAT processing order, not add order). Empty if the editor is
        // not a single-rooted valid tree.
        std::vector<int> scanToEditorOrder() const;
    };

#ifdef __linux__
    // The network_simulator child process: launch/stop/reap plus the out-of-band
    // control bus used to break/heal links at runtime.
    class SimulatorProcess
    {
    public:
        ~SimulatorProcess() { stop(); }

        bool running() const { return pid_ > 0; }
        int  pid() const     { return pid_; }

        // Launch with the given scene (topology file path is per-PID). The caller
        // must have released any live session on the tap first.
        bool launch(SimScene const& scene, std::string& error);
        void stop();    // SIGTERM + waitpid; no-op when not running
        bool reap();    // true when the child exited on its own since the last call

        // Break/heal the wire between two SIM slave indices and remember it so
        // the link draws broken until healed. No-op without a running simulator.
        void setLinkBroken(int a, int b, bool broken);
        std::set<std::pair<int, int>> const& brokenLinks() const { return broken_links_; }

        // Inject zero-mean DC-clock jitter (ns amplitude) on a SIM slave index; 0 disables.
        // No-op without a running simulator.
        void setClockJitter(int node, int64_t amplitude_ns);

        // Pop one message from the simulator's return stream (acks + events).
        // False when nothing is pending (or when not running). The caller drains
        // in a loop each frame and dispatches on `out.type`.
        bool nextEvent(sim::ControlEvent& out);

    private:
        std::string simulatorPath() const;
        void releaseControl();   // tear down the client + unlink the segment

        int         pid_ = -1;
        std::string topo_path_;     // temp topology file, written on launch
        std::string control_shm_;   // control-bus segment name (per-PID)
        std::unique_ptr<sim::SimulatorControlClient> control_;   // created per launch
        std::set<std::pair<int, int>> broken_links_;   // sim-index pairs (a<b) currently broken
    };
#endif
}

#endif
