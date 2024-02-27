#include "kickcat/Link.h"
#include "kickcat/Bus.h"
#include "kickcat/Prints.h"
#include "kickcat/SocketNull.h"

#ifdef __linux__
    #include "kickcat/OS/Linux/Socket.h"
#elif __PikeOS__
    #include "kickcat/OS/PikeOS/Socket.h"
#else
    #error "Unknown platform"
#endif

#include <iostream>

using namespace kickcat;

namespace foot
{
    struct imu
    {
        int16_t accelerometerX; // raw data
        int16_t accelerometerY;
        int16_t accelerometerZ;

        int16_t gyroscopeX; // raw data.
        int16_t gyroscopeY;
        int16_t gyroscopeZ;

        int16_t temperature; // Celsius degrees
    }__attribute__((packed));

    struct Input
    {
        uint16_t watchdog_counter;

        imu footIMU;

        uint16_t force_sensor0;
        uint16_t force_sensor1;
        uint16_t force_sensor2;
        uint16_t force_sensor3;
        uint16_t force_sensor_Vref;

        uint16_t boardStatus;
    } __attribute__((packed));

    struct Output
    {
        uint16_t watchdog_counter;
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


    std::shared_ptr<AbstractSocket> socket_redundancy;
    std::string red_interface_name = "null";
    std::string nom_interface_name = argv[1];

    if (argc == 2)
    {
        printf("No redundancy mode selected \n");
        socket_redundancy = std::make_shared<SocketNull>();
    }
    else
    {
        socket_redundancy = std::make_shared<Socket>();
        red_interface_name = argv[2];
    }

    auto socket_nominal = std::make_shared<Socket>();
    try
    {
        socket_nominal->open(nom_interface_name);
        socket_redundancy->open(red_interface_name);
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
        bus.init();

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

        bus.requestState(State::SAFE_OP);
        bus.waitForState(State::SAFE_OP, 1s);
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

    for (auto& slave: bus.slaves())
    {
        printInfo(slave);
        printESC(slave);
    }

    auto callback_error = [](DatagramState const&){ THROW_ERROR("something bad happened"); };

    // Set valid output to exit safe op.
    auto& foot_slave = bus.slaves().at(0);
    for (int32_t i = 0; i < foot_slave.output.bsize; ++i)
    {
        foot_slave.output.data[i] = 0xBB;
    }

    try
    {
        bus.processDataRead(callback_error);
    }
    catch (...)
    {
    }
    bus.processDataWrite(callback_error);

    try
    {
        bus.requestState(State::OPERATIONAL);
        bus.waitForState(State::OPERATIONAL, 100ms);
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

    link->setTimeout(500us);

    constexpr int64_t LOOP_NUMBER = 12 * 3600 * 1000; // 12h


    for (int32_t i = 0; i < foot_slave.output.bsize; ++i)
    {
        foot_slave.output.data[i] = 0;
    }

    foot::Input* input = reinterpret_cast<foot::Input*>(foot_slave.input.data);
    foot::Output* output = reinterpret_cast<foot::Output*>(foot_slave.output.data);

    output->watchdog_counter = 0;
    int64_t last_error = 0;
    for (int64_t i = 0; i < LOOP_NUMBER; ++i)
    {
        sleep(1ms);

        try
        {
            output->watchdog_counter++;
            bus.sendLogicalRead(callback_error);
            bus.sendLogicalWrite(callback_error);
            bus.sendRefreshErrorCounters(callback_error);
            bus.sendMailboxesReadChecks(callback_error);
            bus.sendMailboxesWriteChecks(callback_error);
            bus.sendReadMessages(callback_error);
            bus.sendWriteMessages(callback_error);
            bus.finalizeDatagrams();

            bus.processAwaitingFrames();

            printf("input Acc %i %i %i, force sensor 0: %i Watchdog counter input %i, wdg cnt output %i\n",
                input->footIMU.accelerometerX, input->footIMU.accelerometerY, input->footIMU.accelerometerZ,
                input->force_sensor0, input->watchdog_counter, output->watchdog_counter);
        }
        catch (std::exception const& e)
        {
            int64_t delta = i - last_error;
            last_error = i;
            std::cerr << e.what() << " at " << i << " delta: " << delta << std::endl;
        }
    }
    return 0;
}
