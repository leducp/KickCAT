#include <csignal>
#include <iostream>
#include <fstream>
#include <argparse/argparse.hpp>

#include "kickcat/Link.h"
#include "kickcat/Bus.h"
#include "kickcat/Prints.h"
#include "kickcat/helpers.h"
#include "kickcat/MailboxSequencer.h"
#include "kickcat/OS/Timer.h"

using namespace kickcat;

// Clean shutdown on Ctrl-C: break the loop so main returns and the socket
// destructor runs, releasing the TAP slot (otherwise the next run sees
// "Socket is full" until the server is restarted).
namespace { volatile std::sig_atomic_t g_running = 1; }
static void onSignal(int) { g_running = 0; }

int main(int argc, char* argv[])
{
    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);

    argparse::ArgumentParser program("easycat_example");

    std::string nom_interface_name;
    program.add_argument("-i", "--interface")
        .help("network interface name")
        .required()
        .store_into(nom_interface_name);

    std::string red_interface_name;
    program.add_argument("-r", "--redundancy")
        .help("redundancy network interface name")
        .default_value(std::string{""})
        .store_into(red_interface_name);

    // Timeouts default to hardware values; raise them for a slow software bus
    // over a real NIC (network_simulator / Windows pcap).
    int read_timeout_ms = 2;
    program.add_argument("--timeout")
        .help("Link read timeout for enumeration/init, in milliseconds")
        .default_value(2)
        .scan<'i', int>()
        .store_into(read_timeout_ms);

    int rt_timeout_ms = 10;
    program.add_argument("--rt-timeout")
        .help("Link read timeout in the cyclic loop, in milliseconds")
        .default_value(10)
        .scan<'i', int>()
        .store_into(rt_timeout_ms);

    int watchdog_ms = 100;
    program.add_argument("--watchdog")
        .help("PDIO watchdog passed to Bus::init, in milliseconds (0 disables)")
        .default_value(100)
        .scan<'i', int>()
        .store_into(watchdog_ms);

    try
    {
        program.parse_args(argc, argv);
    }
    catch (const std::runtime_error& err)
    {
        std::cerr << err.what() << std::endl;
        std::cerr << program;
        return 1;
    }

    std::shared_ptr<AbstractSocket> socket_nominal;
    std::shared_ptr<AbstractSocket> socket_redundancy;
    try
    {
        auto [nominal, redundancy] = createSockets(nom_interface_name, red_interface_name);
        socket_nominal = nominal;
        socket_redundancy = redundancy;
    }
    catch (std::exception const& e)
    {
        std::cerr << e.what() << std::endl;
        return 1;
    }

    auto report_redundancy = []()
    {
        printf("Redundancy has been activated due to loss of a cable \n");
    };

    std::shared_ptr<Link> link = std::make_shared<Link>(socket_nominal, socket_redundancy, report_redundancy);
    link->setTimeout(std::chrono::milliseconds(read_timeout_ms));
    link->checkRedundancyNeeded();

    Bus bus(link);

    auto print_current_state = [&]()
    {
        for (auto& slave : bus.slaves())
        {
            State state = bus.getCurrentState(slave);
            printf("Slave %d state is %s\n", slave.address, toString(state));
        }
    };

    uint8_t io_buffer[2048];
    try
    {
        printf("Initializing Bus...\n");
        // PDIO watchdog (--watchdog). 100 ms is too tight for a software bus over a
        // real NIC: one PDO cycle across many slaves can exceed it and the slaves trip
        // "Sync manager watchdog" at the OP transition. Raise it (e.g. --watchdog 1000)
        // for that case; 0 disables.
        bus.init(std::chrono::milliseconds(watchdog_ms));

        printf("Init done\n");
        printf("Detected slaves: %zu\n", bus.slaves().size());
        for (auto& slave : bus.slaves())
        {
            printf(" - Slave %d\n", slave.address);
        }

        bus.createMapping(io_buffer, sizeof(io_buffer));

        // Optional: Enable IRQ if needed
        bus.enableIRQ(EcatEvent::DL_STATUS,
        [&]()
        {
            printf("DL_STATUS IRQ triggered!\n");
            bus.sendGetDLStatus(bus.slaves().at(0), [](DatagramState const& state){ printf("IRQ reset error: %s\n", toString(state));});
            bus.processAwaitingFrames();

            printf("Slave DL status: %s\n", toString(bus.slaves().at(0).dl_status).c_str());
        });
    }
    catch (ErrorAL const& e)
    {
        std::cerr << e.what() << ": " << ALStatus_to_string(e.code()) << std::endl;
        return 1;
    }
    catch (std::exception const& e)
    {
        std::cerr << e.what() << std::endl;
        return 1;
    }

    auto cyclic_process_data = [&]()
    {
        auto noop = [](DatagramState const&){};
        bus.processDataRead(noop);
        bus.processDataWrite(noop);
    };

    try
    {
        printf("Switching to SAFE_OP...\n");
        bus.requestState(State::SAFE_OP);
        // Feed process data while waiting. On a slow software bus (network_simulator
        // over a real NIC) the slaves' SyncManager watchdog expires unless a process-
        // data frame keeps arriving through the transition; otherwise the slave drops
        // out and OP is rejected with "Sync manager watchdog". Likewise, don't insert
        // slow per-slave state reads (print_current_state) between SAFE_OP and the
        // first cyclic exchange - that gap alone tripped the watchdog over Windows pcap.
        bus.waitForState(State::SAFE_OP, 1s, cyclic_process_data);

        // Set valid output for all slaves to exit safe op
        for (auto& slave : bus.slaves())
        {
            for (int32_t i = 0; i < slave.output.bsize; ++i)
            {
                slave.output.data[i] = 0xBB;
            }
        }
        cyclic_process_data();

        printf("Switching to OPERATIONAL...\n");
        bus.requestState(State::OPERATIONAL);
        bus.waitForState(State::OPERATIONAL, 1s, cyclic_process_data);
        print_current_state();
        
        printf("After OPERATIONAL - Slave info:\n");
        for (auto& slave : bus.slaves())
        {
            printf(" - Slave %d input: %d output: %d\n", 
                   slave.address, slave.input.bsize, slave.output.bsize);
        }
    }
    catch (ErrorAL const& e)
    {
        std::cerr << e.what() << ": " << ALStatus_to_string(e.code()) << std::endl;
        return 1;
    }
    catch (std::exception const& e)
    {
        std::cerr << e.what() << std::endl;
        return 1;
    }

    auto callback_error = [](DatagramState const&){ THROW_ERROR("something bad happened"); };
    link->setTimeout(std::chrono::milliseconds(rt_timeout_ms));
    MailboxSequencer mailbox_sequencer(bus);

    printf("Running loop...\n");

    Timer timer{1ms};
    timer.start();

    while (g_running)
    {
        timer.wait_next_tick();

        try
        {
            bus.sendLogicalRead(callback_error);
            bus.sendLogicalWrite(callback_error);
            bus.sendRefreshErrorCounters(callback_error);
            mailbox_sequencer.step(callback_error);
            bus.finalizeDatagrams();

            bus.processAwaitingFrames();

            // Clear line and print all slave inputs
            printf("\033[K");  // Clear line
            for (size_t idx = 0; idx < bus.slaves().size(); ++idx)
            {
                auto& slave = bus.slaves().at(idx);
                printf("input_slave_%zu: ", idx);
                for (int32_t j = 0; j < slave.input.bsize; ++j)
                {
                    printf("%02x", slave.input.data[j]);
                }
                printf("\n");
            }
            // Move cursor up
            printf("\033[%zuA", bus.slaves().size());
            fflush(stdout);
        }
        catch (std::exception const& e)
        {
            printf("\nError in loop iteration: %s\n\n", e.what());
        }
    }

    return 0;
}
