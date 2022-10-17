#include "diagnosticsBus.h"
#include "diagnosticsServer.h"
#include "diagnosticsAPI.h"

#include <thread>
#include <future>
#include <queue>

#ifdef __linux__
    #include "kickcat/OS/Linux/Socket.h"
#elif __PikeOS__
    #include "kickcat/OS/PikeOS/Socket.h"
#else
    #error "Unknown platform"
#endif

API api;

using namespace kickcat;

std::queue<std::function<void(void)>> generate_instructions(DiagnosticsServer& server, API& api)
{
    auto basic_callback = [](const APIMessage& msg) {if (msg.state == APIRequestState::ACK_ISSUE) {std::cout << msg.content << "\n";};};

    std::queue<std::function<void(void)>> inst;
    inst.push([&server, &api, &basic_callback]{server.sendBusRequest(api, APIHeader::COMMAND, std::string("DETECT_SLAVES"),    basic_callback);});
    inst.push([&server, &api, &basic_callback]{server.sendBusRequest(api, APIHeader::COMMAND, std::string("RESET_SLAVES"),     basic_callback);});
    inst.push([&server, &api, &basic_callback]{server.sendBusRequest(api, APIHeader::COMMAND, std::string("INJECT_TYPE-1::ElmoMotor"), basic_callback);});   
    //inst.push([&server, &api, &basic_callback]{server.sendBusRequest(api, APIHeader::COMMAND, std::string("INJECT_MAPPING-1::os:8/osm:2/is:356/ism:3/"), basic_callback);});
    
    inst.push([&server, &api, &basic_callback]{server.sendBusRequest(api, APIHeader::COMMAND, std::string("GO_INIT-0"),        basic_callback);});
    inst.push([&server, &api, &basic_callback]{server.sendBusRequest(api, APIHeader::COMMAND, std::string("GO_INIT-1"),        basic_callback);});
    inst.push([&server, &api, &basic_callback]{server.sendBusRequest(api, APIHeader::COMMAND, std::string("GO_PREOP-0"),       basic_callback);});
    inst.push([&server, &api, &basic_callback]{server.sendBusRequest(api, APIHeader::COMMAND, std::string("GO_PREOP-1"),       basic_callback);});

    //inst.push([&server, &api, &basic_callback]{server.sendBusRequest(api, APIHeader::COMMAND, std::string("SETUP_IO-0"),       basic_callback);});
    //inst.push([&server, &api, &basic_callback]{server.sendBusRequest(api, APIHeader::COMMAND, std::string("SETUP_IO-1"),       basic_callback);});
    inst.push([&server, &api, &basic_callback]{server.sendBusRequest(api, APIHeader::COMMAND, std::string("ALL_SETUP_IO"),       basic_callback);});

    inst.push([&server, &api, &basic_callback]{server.sendBusRequest(api, APIHeader::COMMAND, std::string("GO_SAFEOP-0"),      basic_callback);});
    inst.push([&server, &api, &basic_callback]{server.sendBusRequest(api, APIHeader::COMMAND, std::string("GO_SAFEOP-1"),      basic_callback);});
    inst.push([&server, &api, &basic_callback]{server.sendBusRequest(api, APIHeader::COMMAND, std::string("GO_OP-0"),          basic_callback);});
    inst.push([&server, &api, &basic_callback]{server.sendBusRequest(api, APIHeader::COMMAND, std::string("GO_OP-1"),          basic_callback);});

    inst.push([&server]{server.wait(300ms);});
    inst.push([&server, &api, &basic_callback]{server.sendBusRequest(api, APIHeader::CONTROL, std::string("HARDSTOP"),         basic_callback);});


    return inst;
}

void diagBus_thread(DiagnosticsBus& diagBus, API& api)
{
    while (not diagBus.isHardStopped())
    {
        diagBus.run(api);
    }
}

void diagServ_thread(DiagnosticsServer& server, API& api)
{
    std::queue<std::function<void(void)>> instruction_queue = generate_instructions(server, api);
    int i = 0;
    while(not instruction_queue.empty())
    {
        if (i%10 == 0)
        {
            std::function<void(void)> to_execute = instruction_queue.front();
            instruction_queue.pop();
            static_cast<void>(std::async(std::launch::async, to_execute));
        }
        sleep(5ms);
        static_cast<void>(std::async(std::launch::async, [&server, &api]{
            server.checkReceivedMail(api);
        }));
        i++;
    }
    printf("--- Slave 0 State\n");
    server.printCurrentState(api, 0);
    printf("--- Slave 0 errored frames\n");
    server.printErroredFrames(api, 0);
    printf("--- Process errors\n");
    server.printErrorProcess(api);
    printf("--- General errors\n");
    server.printErrorGeneral(api);
}

int main(int argc, char* argv[])
{
    auto socket = std::make_shared<Socket>();
    DiagnosticsBus diagBus(socket);
    DiagnosticsServer diagServ;
    nanoseconds timestamp = since_epoch();
    try
    {
        socket->open(argv[1], 2ms);
    }
    catch(const std::system_error& e)
    {
        api.addProcessError({timestamp, processState::SOCKET_ERROR});
        api.addGeneralError({timestamp, "Cannot open socket"});
    }

    std::thread busTh(diagBus_thread, std::ref(diagBus), std::ref(api));
    std::thread servTh(diagServ_thread, std::ref(diagServ), std::ref(api));
    busTh.join();
    servTh.join();

    return 0;
}

