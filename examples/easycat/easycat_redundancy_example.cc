#include "kickcat/Bus.h"
#include "kickcat/LinkRedundancy.h"

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

#include "kickcat/Teleplot.h"
#include "kickcat/DebugHelper.h"

using namespace kickcat;

int main(int argc, char* argv[])
{
    if (argc != 3)
    {
        printf("usage: ./test NIC_nominal NIC_redundancy\n");
        return 1;
    }


    auto reportRedundancy = []()
    {
        printf("Redundancy has been activated due to loss of a cable \n");
    };

    auto socketNominal = std::make_shared<Socket>();
    auto socketRedundancy = std::make_shared<Socket>();

    try
    {
        socketNominal->open(argv[1], 2ms);
        socketRedundancy->open(argv[2], 2ms);
    }
    catch (std::exception const& e)
    {
        std::cerr << e.what() << std::endl;
        return 1;
    }

    std::unique_ptr<LinkRedundancy> link= std::make_unique<LinkRedundancy>(socketNominal, socketRedundancy, reportRedundancy);

    Bus bus(std::move(link));

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

//    uint16_t dl_status_slave0;
//    LinkRedundancy linkDebug(socketNominal, socketRedundancy, reportRedundancy);
//    sendGetRegister(linkDebug, 0, reg::ESC_DL_STATUS, dl_status_slave0);
//    printf("DL status %06x \n", dl_status_slave0);
//
//    uint16_t dl_control_slave0;
//    sendGetRegister(linkDebug, 0, reg::ESC_DL_CONTROL, dl_control_slave0);
//    printf("DL control %06x \n", dl_control_slave0);
//
//    uint16_t dl_control_write = 0xC01;
//    sendWriteRegister(linkDebug, 0, reg::ESC_DL_CONTROL, dl_control_write);
//
//    sendGetRegister(linkDebug, 0, reg::ESC_DL_CONTROL, dl_control_slave0);
//    printf("DL control %06x \n", dl_control_slave0);

//    std::abort();
    socketNominal->setTimeout(500us);
    socketRedundancy->setTimeout(500us);

    constexpr int64_t LOOP_NUMBER = 12 * 3600 * 1000; // 12h

    auto& easycat0 = bus.slaves().at(0);
    auto& easycat1 = bus.slaves().at(1);
    int64_t last_error = 0;
    for (int64_t i = 0; i < LOOP_NUMBER; ++i)
    {
        sleep(1000ms);

        try
        {
            bus.sendLogicalRead(callback_error);
//            bus.sendLogicalWrite(callback_error);
//            bus.sendRefreshErrorCounters(callback_error);
//            bus.sendMailboxesReadChecks(callback_error);
//            bus.sendMailboxesWriteChecks(callback_error);
//            bus.sendReadMessages(callback_error);
//            bus.sendWriteMessages(callback_error);
            bus.finalizeDatagrams();


            bus.processAwaitingFrames();

//            sendGetRegister(linkDebug, 0, reg::ESC_DL_CONTROL, dl_control_slave0);
//            printf("DL control %06x \n", dl_control_slave0);
//
//            sendGetRegister(linkDebug, 0, reg::ESC_DL_STATUS, dl_status_slave0);
//            printf("DL status %06x \n", dl_status_slave0);

            for (int32_t j = 0;  j < easycat0.input.bsize; ++j)
            {
                printf("%02x ", easycat0.input.data[j]);
            }
            printf("\n");
            for (int32_t j = 0;  j < easycat1.input.bsize; ++j)
            {
                printf("%02x ", easycat1.input.data[j]);
            }
            printf("\r");

//
//            if ((i % 1000) == 0)
//            {
//                easycat1.printErrorCounters();
//            }
//
//            for (auto& slave : bus.slaves())
//            {
//                bus.sendGetDLStatus(slave);
//                bus.finalizeDatagrams();
//
//                slave.printDLStatus();
//            }

        }
        catch (std::exception const& e)
        {
            int64_t delta = i - last_error;
            last_error = i;
            std::cerr << e.what() << " at " << i << " delta: " << delta << std::endl;
        }

        printf("\n ---------------------------------------------------------------------------- \n");
    }

    return 0;
}
