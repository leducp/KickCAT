#include <algorithm>
#include <argparse/argparse.hpp>
#include <csignal>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <numeric>

#include "kickcat/EmulatedNetwork.h"
#include "kickcat/Frame.h"
#include "kickcat/OS/Time.h"
#include "kickcat/helpers.h"
#include "kickcat/simulation/SimulatedSlave.h"
#include "kickcat/simulation/SimulatorControlServer.h"
#include "kickcat/simulation/Topology.h"

using namespace kickcat;
using json = nlohmann::json;

static volatile std::sig_atomic_t running = 1;
static void signalHandler(int) { running = 0; }

namespace
{
    struct Options
    {
        std::string              interface;
        std::string              redundancy_interface;
        std::string              control_shm;       // break/heal control channel, if any
        std::string              topology_file;
        std::vector<std::string> slave_configs;   // already expanded (see --count)
    };

    // Parse + validate the CLI. False (after printing the error + usage) on any problem.
    bool parseOptions(int argc, char** argv, Options& opts)
    {
        argparse::ArgumentParser program("network_simulator");

        std::vector<std::string> slave_configs;
        program.add_argument("-i", "--interface")
            .help("network interface name").required().store_into(opts.interface);
        program.add_argument("-r", "--redundancy")
            .help("redundancy network interface (enables cable-redundancy routing)")
            .default_value(std::string{}).store_into(opts.redundancy_interface);
        program.add_argument("--control")
            .help("shared-memory name of a break/heal control channel (see SimulatorControl.h)")
            .default_value(std::string{}).store_into(opts.control_shm);
        program.add_argument("--topology")
            .help("JSON topology file: master injection + slave-to-slave links (branching tree)")
            .default_value(std::string{}).store_into(opts.topology_file);
        program.add_argument("-s", "--slaves")
            .help("JSON configuration files for slaves").remaining().store_into(slave_configs);

        try
        {
            program.parse_args(argc, argv);
        }
        catch (const std::runtime_error& err)
        {
            std::cerr << err.what() << std::endl << program;
            return false;
        }

        if (slave_configs.empty())
        {
            std::cerr << "No slave configuration files provided" << std::endl << program;
            return false;
        }
        opts.slave_configs = std::move(slave_configs);
        return true;
    }

    // Apply the --topology file (if any) to the network. Returns true if it set a
    // redundancy injection. Throws on a bad file/index.
    bool applyTopologyFile(EmulatedNetwork& network, int node_count, std::string const& path)
    {
        if (path.empty())
        {
            return false;
        }
        std::ifstream tf(path);
        if (not tf.is_open())
        {
            throw std::runtime_error("Failed to open topology file: " + path);
        }
        json topo;
        try
        {
            tf >> topo;
        }
        catch (const json::parse_error& e)
        {
            throw std::runtime_error("Failed to parse topology " + path + ": " + e.what());
        }
        return sim::applyTopology(network, sim::parseTopology(topo, node_count));
    }

    // Cyclic frame routing until a signal stops the loop. Returns the process exit
    // code (non-zero on a fatal frame-write error).
    int runSimulation(EmulatedNetwork& network, std::vector<sim::SimulatedSlave>& slaves,
                      AbstractSocket* socket, AbstractSocket* socket_redundancy,
                      bool redundancy, sim::SimulatorControlServer& control)
    {
        int exit_code = 0;

        // Route one frame received on `in` and send the response the way the physical
        // layer would: out the opposite master port when the ring is intact, looped
        // back to the same port when the segment is broken. `redundant_path` picks the
        // tail injection order. Returns true if a frame was actually serviced.
        auto serviceFrame = [&](AbstractSocket* in, AbstractSocket* opposite, bool redundant_path)
        {
            Frame frame;
            int32_t n = in->read(frame.data(), ETH_MAX_SIZE);
            if (n <= 0)
            {
                return false;
            }
            if (not network.route(frame, redundant_path))
            {
                return true; // frame destroyed by an ESC (circulating flag): nothing to send back
            }

            AbstractSocket* out = in;            // broken segment: loop back to the same port
            if (network.ringIntact())
            {
                out = opposite;                  // intact ring: leave by the opposite port
            }
            if (out->write(frame.data(), n) < 0)
            {
                printf("Write back frame: something wrong happened. Aborting...\n");
                exit_code = -2;
                running = 0;
            }
            return true;
        };

        std::vector<nanoseconds> stats;
        stats.reserve(1000);
        uint32_t iteration_counter = 0;
        uint8_t current_value = 0x11;
        constexpr uint32_t ITER = 1000;

        while (running)
        {
            auto t1 = since_epoch();

            bool serviced = serviceFrame(socket, socket_redundancy, false);
            if (redundancy)
            {
                serviced |= serviceFrame(socket_redundancy, socket, true);
            }
            if (not serviced)
            {
                continue;  // both ports idle: re-check `running` (shutdown) and retry
            }

            control.drain();

            for (auto& sim_slave : slaves)
            {
                sim_slave.slave->routine();
                if (sim_slave.slave->state() == State::SAFE_OP)
                {
                    // Re-validate every cycle the master is delivering output data (any
                    // byte differs from the 0xFF init) - not once - so the slave recovers
                    // if it drops back from OP (e.g. a transient process-data watchdog).
                    // Input-only slaves reach OP slave-side without this.
                    bool const written = std::any_of(sim_slave.output.begin(), sim_slave.output.end(),
                        [](uint8_t b) { return b != 0xFF; });
                    if (written)
                    {
                        sim_slave.slave->validateOutputData();
                    }
                }

                // A device behaviour (e.g. a DS402 motor) drives its own TxPDO;
                // the others just echo a rolling test pattern on their inputs.
                if (sim_slave.device)
                {
                    sim_slave.device->step();
                }
                else
                {
                    std::fill(sim_slave.input.begin(), sim_slave.input.end(), current_value);
                }
            }

            // Move to next value every ITER iterations: 0x11 -> 0x22 -> ... -> 0xFF -> 0x00.
            if (++iteration_counter >= ITER)
            {
                iteration_counter = 0;
                if (current_value == 0xFF) { current_value = 0x00; }
                else                       { current_value += 0x11; }
            }

            auto t2 = since_epoch();
            stats.push_back(t2 - t1);
            if (stats.size() >= 1000)
            {
                std::sort(stats.begin(), stats.end());
                sim::SimStats s{};
                s.window = stats.size();
                s.min_ns = static_cast<uint64_t>(stats.front().count());
                s.max_ns = static_cast<uint64_t>(stats.back().count());
                s.avg_ns = static_cast<uint64_t>((std::reduce(stats.begin(), stats.end()) / stats.size()).count());

                control.publishStats(s);

                // One overwriting line (\r + trailing pad) instead of a scrolling
                // log; the GUI shows the live history when launched through KickUI.
                printf("\rframe proc: min %4llu  max %5llu  avg %4llu \xc2\xb5s  (n=%zu, t=%.0fs)   ",
                       static_cast<unsigned long long>(s.min_ns / 1000),
                       static_cast<unsigned long long>(s.max_ns / 1000),
                       static_cast<unsigned long long>(s.avg_ns / 1000),
                       s.window, seconds_f(since_start()).count());
                fflush(stdout);
                stats.clear();
            }
        }
        return exit_code;
    }
}

int main(int argc, char* argv[])
{
    std::signal(SIGINT,  signalHandler);
    std::signal(SIGTERM, signalHandler);

    Options opts;
    if (not parseOptions(argc, argv, opts))
    {
        return 1;
    }

    // --- build the slaves (see kickcat::sim) ---
    std::vector<sim::SimulatedSlave> slaves;
    try
    {
        slaves.reserve(opts.slave_configs.size());
        for (auto const& config_path : opts.slave_configs)
        {
            slaves.push_back(sim::buildSlave(config_path));
        }
    }
    catch (std::exception const& e)
    {
        std::cerr << e.what() << std::endl;
        return 1;
    }

    // --- physical layer: route frames in real EtherCAT order (default daisy
    // chain, or the branching tree from --topology) ---
    std::vector<EmulatedESC*> esc_ptrs;
    esc_ptrs.reserve(slaves.size());
    for (auto& sim_slave : slaves)
    {
        esc_ptrs.push_back(sim_slave.esc.get());
    }
    EmulatedNetwork network(std::move(esc_ptrs));

    bool topology_set_redundancy = false;
    try
    {
        topology_set_redundancy = applyTopologyFile(network, static_cast<int>(slaves.size()), opts.topology_file);
    }
    catch (std::exception const& e)
    {
        std::cerr << e.what() << std::endl;
        return 1;
    }

    bool redundancy = not opts.redundancy_interface.empty();
    if (redundancy and not slaves.empty() and not topology_set_redundancy)
    {
        // The redundant master port closes the ring on the tail slave's open port.
        network.setRedundancyInjection(slaves.size() - 1, 1);
    }

    // Optional control bus: the host creates the segment, the sim attaches.
    sim::SimulatorControlServer control(network, slaves.size());
    if (not opts.control_shm.empty())
    {
        try
        {
            control.attach(opts.control_shm);
        }
        catch (std::exception const& e)
        {
            std::cerr << "Failed to attach control channel " << opts.control_shm << ": " << e.what() << std::endl;
            return 1;
        }
    }

    printf("Start EtherCAT network simulator on %s with %zu slaves\n", opts.interface.c_str(), slaves.size());
    if (redundancy)
    {
        printf("Cable redundancy enabled on %s\n", opts.redundancy_interface.c_str());
    }

    auto [socket, socket_redundancy] = createSockets(opts.interface, opts.redundancy_interface);
    // Idle wake-up so SIGINT/SIGTERM is honored. With redundancy both sockets are
    // polled every loop, so keep the timeout short to stay responsive to the master
    // (which reads the cross-over port within its own timeout).
    nanoseconds read_timeout = 100ms;
    if (redundancy)
    {
        read_timeout = 1ms;
    }
    socket->setTimeout(read_timeout);
    if (redundancy)
    {
        socket_redundancy->setTimeout(read_timeout);
    }

    for (auto& sim_slave : slaves)
    {
        sim_slave.slave->start();
    }

    return runSimulation(network, slaves, socket.get(), socket_redundancy.get(),
                         redundancy, control);
}
