#include <iostream>

#include "kickcat/Link.h"
#include "kickcat/Bus.h"
#include "kickcat/Prints.h"
#include "kickcat/helpers.h"

using namespace kickcat;

namespace freedom
{
    struct fxos8700cq
    {
        int16_t accelerometerX; // mapped 0x6000
        int16_t accelerometerY; // mapped 0x6001
        int16_t accelerometerZ; // mapped 0x6002

        int16_t magnetometerX;  // mapped 0x6003
        int16_t magnetometerY;  // mapped 0x6004
        int16_t magnetometerZ;  // mapped 0x6005
    } __attribute__((packed));

    struct Input
    {
        fxos8700cq sensor;
    } __attribute__((packed));

    struct Output
    {
        // currently no mapped outputs
    } __attribute__((packed));
}


int main(int argc, char* argv[])
{
    if (argc != 3 and argc != 2)
    {
        printf("usage redundancy mode : ./test NIC_nominal NIC_redundancy\n");
        printf("usage no redundancy mode : ./test NIC_nominal\n");
        return 1;
    }

    std::string red_interface_name = "";
    std::string nom_interface_name = argv[1];
    if (argc == 3)
    {
        red_interface_name = argv[2];
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

    std::shared_ptr<Link> link= std::make_shared<Link>(socket_nominal, socket_redundancy, report_redundancy);
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
        bus.init(100ms);  // to adapt to your use case

        printf("Init done \n");
        print_current_state();

        bus.createMapping(io_buffer);

        bus.enableIRQ(EcatEvent::DL_STATUS,
        [&]()
        {
            printf("DL_STATUS IRQ triggered!\n");
            bus.sendGetDLStatus(bus.slaves().at(0), [](DatagramState const& state){ printf("IRQ reset error: %s\n", toString(state));});
            bus.processAwaitingFrames();

            printf("Slave DL status: %s", toString(bus.slaves().at(0).dl_status).c_str());
        });
    }
    catch (ErrorCode const& e)
    {
        std::cerr << e.what() << ": " << ALStatus_to_string(e.code()) << std::endl;
        return 1;
    }
    catch (std::exception const& e)
    {
        std::cerr << e.what() << std::endl;
        return 1;
    }

    for (auto& slave: bus.slaves())
    {
        printInfo(slave);
        printESC(slave);
    }

    // Get ref on the first slave to work with it
    auto& easycat = bus.slaves().at(0);

    try
    {
        auto cyclic_process_data = [&]()
        {
            auto noop =[](DatagramState const&){};
            bus.processDataRead (noop);
            bus.processDataWrite(noop);
        };

        printf("Going to SAFE OP\n");
        bus.requestState(State::SAFE_OP);
        bus.waitForState(State::SAFE_OP, 1s);
        print_current_state();

        // Set valid output to exit safe op.
        for (int32_t i = 0; i < easycat.output.bsize; ++i)
        {
            easycat.output.data[i] = 0xBB;
        }
        cyclic_process_data();

        printf("Going to OPERATIONAL\n");
        bus.requestState(kickcat::State::OPERATIONAL);
        bus.waitForState(kickcat::State::OPERATIONAL, 1s, cyclic_process_data);
        print_current_state();
    }
    catch (ErrorCode const& e)
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
    link->setTimeout(10ms);  // Adapt to your use case (RT loop)

    // Map PDO memory to structs
    freedom::Input*  input  = reinterpret_cast<freedom::Input*>(easycat.input.data);
    freedom::Output* output = reinterpret_cast<freedom::Output*>(easycat.output.data);


    constexpr int64_t LOOP_NUMBER = 12 * 3600 * 1000; // 12h

    for (int32_t i = 0; i < easycat.output.bsize; ++i)
    {
        easycat.output.data[i] = 0xAA;
    }

    for (int64_t i = 0; i < LOOP_NUMBER; ++i)
    {
        sleep(4ms);

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

            printf("Accel [X:%d Y:%d Z:%d] | Mag [X:%d Y:%d Z:%d]\n",
                input->sensor.accelerometerX,
                input->sensor.accelerometerY,
                input->sensor.accelerometerZ,
                input->sensor.magnetometerX,
                input->sensor.magnetometerY,
                input->sensor.magnetometerZ
            );
            
        }
        catch (std::exception const& e)
        {
            std::cerr << "Error: " << e.what() << std::endl;
        }
    }

    return 0;
}
