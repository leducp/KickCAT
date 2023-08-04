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
#include <fstream>
#include <algorithm>

using namespace kickcat;

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

    auto callback_error = [](DatagramState const&){ THROW_ERROR("something bad happened"); };

    try
    {
        bus.processDataRead(callback_error);
    }
    catch (...)
    {
        //TODO: need a way to check expected working counter depending on state
        // -> in safe op write is not working
    }

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
    FILE* stat_file = fopen("stats.csv", "w");
    fwrite("latency\n", 1, 8, stat_file);

    auto& easycat = bus.slaves().at(0);

    for (int32_t i = 0; i < easycat.output.bsize; ++i)
    {
        easycat.output.data[i] = 0xAA;
    }

    int64_t last_error = 0;
    for (int64_t i = 0; i < LOOP_NUMBER; ++i)
    {
        sleep(1ms);

        try
        {
            nanoseconds t1 = since_epoch();
            bus.sendLogicalRead(callback_error);
            bus.sendLogicalWrite(callback_error);
            bus.sendRefreshErrorCounters(callback_error);
            bus.sendMailboxesReadChecks(callback_error);
            bus.sendMailboxesWriteChecks(callback_error);
            bus.sendReadMessages(callback_error);
            bus.sendWriteMessages(callback_error);
            bus.finalizeDatagrams();
            nanoseconds t2 = since_epoch();


            nanoseconds t3 = since_epoch();
            bus.processAwaitingFrames();
            nanoseconds t4 = since_epoch();

            for (int32_t j = 0;  j < easycat.input.bsize; ++j)
            {
                printf("%02x ", easycat.input.data[j]);
            }
            printf("\r");

            // blink a led - EasyCAT example for Arduino
            if ((i % 50) < 25)
            {
                easycat.output.data[0] = 1;
            }
            else
            {
                easycat.output.data[0] = 0;
            }

            if ((i % 1000) == 0)
            {
                printf("\n -*-*-*-*- slave %u -*-*-*-*-\n %s", easycat.address, toString(easycat.error_counters).c_str());
            }

            microseconds sample = duration_cast<microseconds>(t4 - t3 + t2 - t1);
            std::string sample_str = std::to_string(sample.count());
            fwrite(sample_str.data(), 1, sample_str.size(), stat_file);
            fwrite("\n", 1, 1, stat_file);
        }
        catch (std::exception const& e)
        {
            int64_t delta = i - last_error;
            last_error = i;
            std::cerr << e.what() << " at " << i << " delta: " << delta << std::endl;
        }
    }

    fclose(stat_file);
    return 0;
}
