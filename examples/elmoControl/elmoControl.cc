#include "kickcat/Link.h"
#include "kickcat/Bus.h"
#include "kickcat/Prints.h"
#include "kickcat/SocketNull.h"

#include "ElmoProtocol.h"
#include "CanOpenErrors.h"
#include "CanOpenStateMachine.h"

#ifdef __linux__
    #include "kickcat/OS/Linux/Socket.h"
#elif __PikeOS__
    #include "kickcat/OS/PikeOS/Socket.h"
#else
    #error "Unknown platform"
#endif

#include <iostream>

using namespace kickcat;

int main(int argc, char* argv[])
{
    if (argc != 3 and argc != 2)
    {
        printf("usage redundancy mode : ./test NIC_nominal NIC_redundancy\n");
        printf("usage no redundancy mode : ./test NIC_nominal\n");
        return 1;
    }


    std::shared_ptr<AbstractSocket> socketRedundancy;
    std::string red_interface_name = "null";
    std::string nom_interface_name = argv[1];

    if (argc == 2)
    {
        printf("No redundancy mode selected \n");
        socketRedundancy = std::make_shared<SocketNull>();
    }
    else
    {
        socketRedundancy = std::make_shared<Socket>();
        red_interface_name = argv[2];
    }

    auto socketNominal = std::make_shared<Socket>();
    try
    {
        socketNominal->open(nom_interface_name);
        socketRedundancy->open(red_interface_name);
    }
    catch (std::exception const& e)
    {
        std::cerr << e.what() << std::endl;
        return 1;
    }

    auto reportRedundancy = []()
    {
        printf("Redundancy has been activated due to loss of a cable \n");
    };

    std::shared_ptr<Link> link= std::make_shared<Link>(socketNominal, socketRedundancy, reportRedundancy);
    link->setTimeout(2ms);
    link->checkRedundancyNeeded();

    Bus bus(link);

    uint8_t io_buffer[2048];
    try
    {
        bus.init();

        // Prepare mapping for elmo
        uint32_t RxLength = sizeof(pdo::RxMapping);
        bus.writeSDO(bus.slaves().at(0), sdo::RxPDO.index, sdo::RxPDO.subindex, 
                    Bus::Access::COMPLETE, const_cast<uint8_t*>(pdo::RxMapping), RxLength);
        uint32_t TxLength = sizeof(pdo::TxMapping);
        bus.writeSDO(bus.slaves().at(0), sdo::TxPDO.index, sdo::TxPDO.subindex, 
                    Bus::Access::COMPLETE, const_cast<uint8_t*>(pdo::TxMapping), TxLength);
        
        bus.createMapping(io_buffer);

        bus.requestState(State::SAFE_OP);
        bus.waitForState(State::SAFE_OP, 1s);
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
    auto false_alarm = [](DatagramState const&){ DEBUG_PRINT("previous error was a false alarm"); };

    try
    {
        bus.processDataRead(callback_error);
        bus.processDataWrite(false_alarm);
    }
    catch (...)
    {
    }

    try
    {
        bus.requestState(State::OPERATIONAL);
        bus.waitForState(State::OPERATIONAL, 100ms);
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

    Slave& elmo = bus.slaves().at(0);
    pdo::Output* outputPDO = reinterpret_cast<pdo::Output*>(elmo.output.data);
    pdo::Input* inputPDO = reinterpret_cast<pdo::Input*>(elmo.input.data);
    CANOpenStateMachine elmoStateMachine;

    elmoStateMachine.setCommand(CANOpenCommand::ENABLE);

    // Setting a small torque
    outputPDO->modeOfOperation = 4;
    outputPDO->targetTorque = 30;
    outputPDO->maxTorque = 3990;
    outputPDO->targetPosition = 0;
    outputPDO->velocityOffset = 0;
    outputPDO->digitalOutput = 0;

    constexpr int64_t LOOP_NUMBER = 12 * 3600 * 1000; // 12h
    int64_t last_error = 0;
    for (int64_t i = 0; i < LOOP_NUMBER; ++i)
    {
        sleep(2ms);

        try
        {
            bus.sendLogicalRead(callback_error);            // Update inputPDO
            bus.sendLogicalWrite(callback_error);           // Update outputPDO
            bus.sendMailboxesReadChecks(callback_error);
            bus.sendReadMessages(callback_error);           // Get emergencies

            bus.finalizeDatagrams();
            bus.processAwaitingFrames();
        }
        catch (std::exception const& e)
        {
            int64_t delta = i - last_error;
            last_error = i;
            std::cerr << e.what() << " at " << i << " delta: " << delta << std::endl;
        }

        // Update Elmo at each step to receive emergencies in case of failure
        elmoStateMachine.update(inputPDO->statusWord);
        outputPDO->controlWord = elmoStateMachine.getControlWord();
        if (elmo.mailbox.emergencies.size() > 0)
        {
            for (auto& em : elmo.mailbox.emergencies)
            {
                std::cerr << "*~~~ Emergency received @ " << i << " ~~~*" << std::endl;
                std::cerr << registerToError(em.error_register);
                std::cerr << codeToError(em.error_code);
            }
            elmo.mailbox.emergencies.resize(0);
        }
    }

    return 0;
}
