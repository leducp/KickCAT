#include <iostream>
#include <fstream>
#include <argparse/argparse.hpp>

#include "kickcat/Link.h"
#include "kickcat/Bus.h"
#include "kickcat/Prints.h"
#include "kickcat/helpers.h"

using namespace kickcat;

int main(int argc, char* argv[])
{
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
    link->setTimeout(2ms);
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
        bus.init(100ms);  // to adapt to your use case

        printf("Init done\n");
        printf("Detected slaves: %zu\n", bus.slaves().size());
        for (auto& slave : bus.slaves())
        {
            printf(" - Slave %d\n", slave.address);
        }

        bus.createMapping(io_buffer);

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
        bus.waitForState(State::SAFE_OP, 1s);
        print_current_state();

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
    link->setTimeout(10ms); // Adapt to your use case (RT loop)

    printf("Running loop...\n");

    while (true)
    {
        sleep(1ms);

        try
        {
            bus.sendLogicalRead(callback_error);
            bus.sendLogicalWrite(callback_error);
            bus.sendRefreshErrorCounters(callback_error);
            bus.sendMailboxesReadChecks(callback_error);
            bus.sendMailboxesWriteChecks(callback_error);
            bus.sendReadMessages(callback_error);
            bus.sendWriteMessages(callback_error);
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
