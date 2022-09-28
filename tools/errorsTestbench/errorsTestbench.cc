#include <iostream>
#include <fstream>
#include <math.h>
#include <algorithm>

#include "kickcat/Bus.h"
#include "kickcat/Prints.h"
#include "kickcat/Diagnostics.h"
#include "ElmoProtocol.h"
#include "CANOpenStateMachine.h"
#include "Error.h"

#ifdef __linux__
    #include "kickcat/OS/Linux/Socket.h"
#elif __PikeOS__
    #include "kickcat/OS/PikeOS/Socket.h"
#else
    #error "Unknown platform"
#endif


using namespace kickcat;

auto callback_error = [](DatagramState const&){THROW_ERROR("Something bad happened");};

// return child topology, organized by ports
// 0x0000 is supposed to be first child - it means "no child" if its value appears in ports 1, 2 or 3 (it is no one's child)
std::unordered_map<uint16_t, std::vector<uint16_t>> completeTopologyMap(std::vector<Slave>& slaves)
{
    std::unordered_map<uint16_t, std::vector<uint16_t>> completeTopology;

    std::unordered_map<uint16_t, uint16_t> topology = getTopology(slaves);
    std::unordered_map<uint16_t, std::vector<int>> slaves_ports;
    std::unordered_map<uint16_t, std::vector<uint16_t>> links;

    for (auto& slave : slaves)
    {
        std::vector<int> ports;
        switch (slave.countOpenPorts())
        {
            case 1:
            {
                ports.push_back(0);
                break;
            }
            case 2:
            {
                ports.push_back(0);
                ports.push_back(1);
                break;
            }
            case 3:
            {
                ports.push_back(0);
                if (slave.dl_status.PL_port3) {ports.push_back(3);};
                if (slave.dl_status.PL_port1) {ports.push_back(1);};
                if (slave.dl_status.PL_port2) {ports.push_back(2);};
                break;
            }
            case 4:
            {
                ports.push_back(0);
                ports.push_back(3);
                ports.push_back(1);
                ports.push_back(2);
                break;
            }
            default:
            {
                THROW_ERROR("Error in ports order\n");
            }
        }
        slaves_ports[slave.address] = ports;

        links[topology[slave.address]].push_back(slave.address);
        if (slave.address != topology[slave.address]) {links[slave.address].push_back(topology[slave.address]);}
    }

    for (auto& slave : slaves)
    {
        completeTopology[slave.address] = {0, 0, 0, 0};
        std::sort(links[slave.address].begin(), links[slave.address].end());
        for (size_t i = 0; i < links[slave.address].size(); ++i)
        {
            completeTopology[slave.address].at(slaves_ports[slave.address][i]) = links[slave.address].at(i); 
        }
    }

    return completeTopology;
}

bool PortsAnalysis(std::vector<Slave>& slaves)
{
    std::unordered_map<uint16_t, std::vector<uint16_t>> completeTopology = completeTopologyMap(slaves);
    uint16_t first_error = 0;
    uint16_t error_port = 0xFF;
    bool check = true;

    printf("\nPHY Layer Errors/Invalid Frames/Forwarded/Lost Links\n");
    printf("  Slave  ");
    for (uint16_t slave_id = 0; slave_id < (uint16_t) slaves.size(); ++slave_id) {printf(" %04x ", slave_id);}
    printf("|");
    for (uint16_t slave_id = 0; slave_id < (uint16_t) slaves.size(); ++slave_id) {printf(" %04x ", slave_id);}
    printf("|");
    for (uint16_t slave_id = 0; slave_id < (uint16_t) slaves.size(); ++slave_id) {printf(" %04x ", slave_id);}
    printf("|");
    for (uint16_t slave_id = 0; slave_id < (uint16_t) slaves.size(); ++slave_id) {printf(" %04x ", slave_id);}

    printf("\n");
    for (auto port = 0; port < 4; ++port)
    {
        printf("Port %i : ", port);

        //PHY
        for (uint16_t slave_id = 0; slave_id < (uint16_t) slaves.size(); ++slave_id)
        {
            if (slaves.at(slave_id).error_counters.rx[port].physical_layer > 0) {check = false;}
            printf("  %03d ", slaves.at(slave_id).error_counters.rx[port].physical_layer);
        }
        printf("|");

        //Invalid
        for (uint16_t slave_id = 0; slave_id < (uint16_t) slaves.size(); ++slave_id)
        {
            if (slaves.at(slave_id).error_counters.rx[port].invalid_frame > 0) {check = false;}
            printf("  %03d ", slaves.at(slave_id).error_counters.rx[port].invalid_frame);
        }
        printf("|");

        //Forwarded
        for (uint16_t slave_id = 0; slave_id < (uint16_t) slaves.size(); ++slave_id)
        {
            if (slaves.at(slave_id).error_counters.forwarded[port] > 0) {check = false;}
            printf("  %03d ", slaves.at(slave_id).error_counters.forwarded[port]);
        }
        printf("|");

        for (uint16_t slave_id = 0; slave_id < (uint16_t) slaves.size(); ++slave_id)
        {
            if (slaves.at(slave_id).error_counters.lost_link[port] > 0) {check = false;}
            printf("  %03d ", slaves.at(slave_id).error_counters.lost_link[port]);
        }
        printf("\n");
    }

    return check;
}

void printInputPDO(hal::pdo::elmo::Input* inputPDO)
{
    printf("statusWord :        %04x            \n", inputPDO->statusWord);
    printf("modeOfOperation :   %i              \n", inputPDO->modeOfOperationDisplay);
    printf("actualPosition:     %i              \n", inputPDO->actualPosition);
    printf("actualVelocity :    %i              \n", inputPDO->actualVelocity);
    printf("demandTorque :      %i              \n", inputPDO->demandTorque);
    printf("actualTorque :      %i              \n", inputPDO->actualTorque);  ///< Actual torque in RTU.
    printf("dcVoltage:          %i              \n", inputPDO->dcVoltage);
    printf("digitalInput :      %i              \n",  inputPDO->digitalInput);
    printf("analogInput :       %i              \n",  inputPDO->analogInput);
    printf("demandPosition :    %i              \n", inputPDO->demandPosition);
    printf("\033[F\033[F\033[F\033[F\033[F\033[F\033[F\033[F\033[F\033[F");
}



int scramble(Bus& bus)
{
    printf("Mode of operation : SCRAMBLE\n");
    DEBUG_PRINT("Ignore DEBUG_PRINTS that indicate invalid working counters, they are thrown by process messages, to keep slave state operational\n\n\n\n")
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
        for (auto& slave : bus.slaves())
        {
            bus.sendGetDLStatus(slave);
            bus.finalizeDatagrams();
        }

        //Configure Mapping
        Slave& elmo = bus.slaves().at(1);
        uint32_t Rx_length = sizeof(hal::pdo::elmo::RxMapping);
        bus.writeSDO(elmo, hal::sdo::elmo::RxPDO.index, hal::sdo::elmo::RxPDO.subindex, Bus::Access::COMPLETE, (void*) hal::pdo::elmo::RxMapping, Rx_length);
        uint32_t Tx_length = sizeof(hal::pdo::elmo::TxMapping);
        bus.writeSDO(elmo, hal::sdo::elmo::TxPDO.index, hal::sdo::elmo::TxPDO.subindex, Bus::Access::COMPLETE, (void*) hal::pdo::elmo::TxMapping, Tx_length);
        
        bus.createMapping(io_buffer);

        bus.requestState(State::SAFE_OP);
        bus.waitForState(State::SAFE_OP, 1s);
        print_current_state();

        auto callback_error = [](DatagramState const&){ THROW_ERROR("something bad happened"); };
        bus.processDataRead(callback_error);
        auto itWillBeWrong = [](DatagramState const&){DEBUG_PRINT("(Previous line was a False alarm) \n");};
        bus.processDataWrite(itWillBeWrong);

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
    printf("Init done successfully, you can proceed to tests\n\n\n");

    // Main Loop
    int64_t last_error = 0;
    for (int64_t i = 0; i < 50000; ++i)
    {
        sleep(1ms);

        try
        {

            bus.sendRefreshErrorCounters(callback_error);
            bus.sendLogicalWrite([](DatagramState const&){});

            bus.finalizeDatagrams();

            bus.processAwaitingFrames();
        }
        catch (std::exception const& e)
        {
            int64_t delta = i - last_error;
            last_error = i;
            std::cerr << e.what() << " at " << i << " delta: " << delta << std::endl;
        }

        if (i%100 == 0)
        {
            PortsAnalysis(bus.slaves());
        }
    }
    return 0;
}

int encoderFault(Bus& bus)
{
    printf("Mode of operation : ENCODER FAULT\n");
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
        for (auto& slave : bus.slaves())
        {
            bus.sendGetDLStatus(slave);
            bus.finalizeDatagrams();
        }

        //Configure Mapping
        Slave& elmo = bus.slaves().at(1);
        uint32_t Rx_length = sizeof(hal::pdo::elmo::RxMapping);
        bus.writeSDO(elmo, hal::sdo::elmo::RxPDO.index, hal::sdo::elmo::RxPDO.subindex, Bus::Access::COMPLETE, (void*) hal::pdo::elmo::RxMapping, Rx_length);
        uint32_t Tx_length = sizeof(hal::pdo::elmo::TxMapping);
        bus.writeSDO(elmo, hal::sdo::elmo::TxPDO.index, hal::sdo::elmo::TxPDO.subindex, Bus::Access::COMPLETE, (void*) hal::pdo::elmo::TxMapping, Tx_length);
        
        bus.createMapping(io_buffer);

        bus.requestState(State::SAFE_OP);
        bus.waitForState(State::SAFE_OP, 1s);
        print_current_state();

        auto callback_error = [](DatagramState const&){ THROW_ERROR("something bad happened"); };
        bus.processDataRead(callback_error);
        auto itWillBeWrong = [](DatagramState const&){DEBUG_PRINT("(Previous line was a False alarm) \n");};
        bus.processDataWrite(itWillBeWrong);

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

    Slave& elmo = bus.slaves().at(1);
    hal::pdo::elmo::Input* inputPDO {nullptr};
    inputPDO = reinterpret_cast<hal::pdo::elmo::Input*>(elmo.input.data);
    hal::pdo::elmo::Output* outputPDO {nullptr};
    outputPDO = reinterpret_cast<hal::pdo::elmo::Output*>(elmo.output.data);
    can::CANOpenStateMachine stateMachine;


    //Turn on
    stateMachine.setCommand(can::CANOpenCommand::ENABLE);
    nanoseconds startTime = since_epoch();
    do
    {
        sleep(1ms);
        bus.sendLogicalRead(callback_error);
        bus.finalizeDatagrams();
        bus.processAwaitingFrames();

        stateMachine.statusWord_ = inputPDO->statusWord;
        stateMachine.update();

        outputPDO->controlWord = stateMachine.controlWord_;
        bus.sendLogicalWrite(callback_error);

    }
    while((not stateMachine.isON()) & (elapsed_time(startTime) < 5s));

    stateMachine.printState();
    if (not stateMachine.isON())
    {
        printf("Timeout : motor failed to start\n");
        return 1;
    }
    printf("Init done successfully, you can proceed to tests\n\n\n");

    // Main Loop
    int64_t last_error = 0;
    for (int64_t i = 0; i < 50000; ++i)
    {
        sleep(1ms);

        try
        {
            bus.sendLogicalRead(callback_error);
            bus.sendLogicalWrite(callback_error);

            bus.checkMailboxes(callback_error);
            bus.sendReadMessages(callback_error);

            bus.finalizeDatagrams();

            stateMachine.statusWord_ = inputPDO->statusWord;
            stateMachine.update();

            outputPDO->controlWord = stateMachine.controlWord_;
            outputPDO->modeOfOperation = 4;
            outputPDO->targetTorque = 0;
            outputPDO->maxTorque = 3990;
            outputPDO->targetPosition = 0;
            outputPDO->velocityOffset = 0;
            outputPDO->digitalOutput = 0;

            bus.processAwaitingFrames();
        }
        catch (std::exception const& e)
        {
            int64_t delta = i - last_error;
            last_error = i;
            std::cerr << e.what() << " at " << i << " delta: " << delta << std::endl;
        }

        if (i%100 == 0)
        {
            for (auto& em : elmo.mailbox.emergencies)
            {
                printf("Emergency sent : %04x - %s", em.error_code, can::emergency::errorCode::codeToError(em.error_code));
            }
            if (elmo.mailbox.emergencies.size() > 0)
            {
                stateMachine.printState();
                print_current_state();
                elmo.mailbox.emergencies.resize(0);
            }
        }
    }

    //Turn off
    stateMachine.setCommand(can::CANOpenCommand::DISABLE);
    while(stateMachine.isON())
    {
        
        bus.sendLogicalRead(callback_error);
        bus.finalizeDatagrams();

        stateMachine.statusWord_ = inputPDO->statusWord;
        stateMachine.update();

        outputPDO->controlWord = stateMachine.controlWord_;
        bus.sendLogicalWrite(callback_error);
        bus.processAwaitingFrames();
    }
    stateMachine.printState();


    return 0;
}

int cutBefore(Bus& bus)
{
    printf("Mode of operation : CUT CABLES BEFORE PROCESS\n");
    uint8_t io_buffer[2048];
    try
    {
        for (auto& slave : bus.slaves())
        {
            bus.sendGetDLStatus(slave);
            bus.finalizeDatagrams();
        }

        std::unordered_map<uint16_t, uint16_t> topology = getTopology(bus.slaves());
        std::unordered_map<uint16_t, uint16_t> expected = {{0, 0}, {1, 0}};
        if (topology == expected)
        {
            printf("Topology seem correct, no cut cable detected\n");
        }
        else
        {
            printf("Topology does not match expected, a slave is either unresponsive, or a link is missing\n");
        }
        return 1;
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
}

int cut(Bus& bus)
{
    printf("Mode of operation : CUT CABLES DURING PROCESS\n");

    DEBUG_PRINT("Ignore DEBUG_PRINTS that indicate invalid working counters, they are thrown by process messages, to keep slave state operational\n\n\n\n")
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
        for (auto& slave : bus.slaves())
        {
            bus.sendGetDLStatus(slave);
            bus.finalizeDatagrams();
        }

        //Configure Mapping
        Slave& elmo = bus.slaves().at(1);
        uint32_t Rx_length = sizeof(hal::pdo::elmo::RxMapping);
        bus.writeSDO(elmo, hal::sdo::elmo::RxPDO.index, hal::sdo::elmo::RxPDO.subindex, Bus::Access::COMPLETE, (void*) hal::pdo::elmo::RxMapping, Rx_length);
        uint32_t Tx_length = sizeof(hal::pdo::elmo::TxMapping);
        bus.writeSDO(elmo, hal::sdo::elmo::TxPDO.index, hal::sdo::elmo::TxPDO.subindex, Bus::Access::COMPLETE, (void*) hal::pdo::elmo::TxMapping, Tx_length);
        
        bus.createMapping(io_buffer);

        bus.requestState(State::SAFE_OP);
        bus.waitForState(State::SAFE_OP, 1s);
        print_current_state();

        auto callback_error = [](DatagramState const&){ THROW_ERROR("something bad happened"); };
        bus.processDataRead(callback_error);
        auto itWillBeWrong = [](DatagramState const&){DEBUG_PRINT("(Previous line was a False alarm) \n");};
        bus.processDataWrite(itWillBeWrong);

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
    printf("Init done successfully, you can proceed to tests\n\n\n");

    for (int64_t i = 0; i < 50000; ++i)
    {
        sleep(1ms);

        try
        {
            bus.sendLogicalWrite([](DatagramState const&){});
            bus.sendRefreshErrorCounters([](DatagramState const&){});

            for (auto& slave : bus.slaves())
            {
                bus.ping(slave, [slave](DatagramState const& datagramStatus)
                                    {
                                        if (datagramStatus == DatagramState::LOST) {printf("Lost frame initially sent to %04x\n", slave.address);} 
                                        else {printf("Wrong WKC for slave %04x\n", slave.address);}
                                    } );
            }

            if (i%300 == 0)
            {
                PortsAnalysis(bus.slaves());
                print_current_state();
            }

            bus.finalizeDatagrams();
            bus.processAwaitingFrames();
        }
        catch(const std::exception& e)
        {
            DEBUG_PRINT(e.what() << '\n');
        }

        
    }
    return 1;
}

int realtimeLoss(Bus& bus)
{
    printf("Mode of operation : REALTIME LOSS\n");
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
        for (auto& slave : bus.slaves())
        {
            bus.sendGetDLStatus(slave);
            bus.finalizeDatagrams();
        }

        //Configure Mapping
        Slave& elmo = bus.slaves().at(1);
        uint32_t Rx_length = sizeof(hal::pdo::elmo::RxMapping);
        bus.writeSDO(elmo, hal::sdo::elmo::RxPDO.index, hal::sdo::elmo::RxPDO.subindex, Bus::Access::COMPLETE, (void*) hal::pdo::elmo::RxMapping, Rx_length);
        uint32_t Tx_length = sizeof(hal::pdo::elmo::TxMapping);
        bus.writeSDO(elmo, hal::sdo::elmo::TxPDO.index, hal::sdo::elmo::TxPDO.subindex, Bus::Access::COMPLETE, (void*) hal::pdo::elmo::TxMapping, Tx_length);
        
        bus.createMapping(io_buffer);

        bus.requestState(State::SAFE_OP);
        bus.waitForState(State::SAFE_OP, 1s);
        print_current_state();

        auto callback_error = [](DatagramState const&){ THROW_ERROR("something bad happened"); };
        bus.processDataRead(callback_error);
        auto itWillBeWrong = [](DatagramState const&){DEBUG_PRINT("(Previous line was a False alarm) \n");};
        bus.processDataWrite(itWillBeWrong);

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

    Slave& elmo = bus.slaves().at(1);
    hal::pdo::elmo::Input* inputPDO {nullptr};
    inputPDO = reinterpret_cast<hal::pdo::elmo::Input*>(elmo.input.data);
    hal::pdo::elmo::Output* outputPDO {nullptr};
    outputPDO = reinterpret_cast<hal::pdo::elmo::Output*>(elmo.output.data);
    can::CANOpenStateMachine stateMachine;


    //Turn on
    stateMachine.setCommand(can::CANOpenCommand::ENABLE);
    nanoseconds startTime = since_epoch();
    do
    {
        sleep(1ms);
        bus.sendLogicalRead(callback_error);
        bus.finalizeDatagrams();
        bus.processAwaitingFrames();

        stateMachine.statusWord_ = inputPDO->statusWord;
        stateMachine.update();

        outputPDO->controlWord = stateMachine.controlWord_;
        bus.sendLogicalWrite(callback_error);

    }
    while((not stateMachine.isON()) && (elapsed_time(startTime) < 5s));

    stateMachine.printState();
    if (not stateMachine.isON())
    {
        printf("Timeout : motor failed to start\n");
        return 1;
    }

    // Main Loop
    for (int64_t i = 0; i < 1500; ++i)
    {
        sleep(1ms);

        try
        {
            bus.checkMailboxes(callback_error);
            bus.sendReadMessages(callback_error);
            bus.sendLogicalRead(callback_error);
            if (i<1001)
            {
                bus.sendLogicalWrite(callback_error);
            }
            if (i == 1001)
            {
                printf("\n\n\nStopping LogicalWrite operations\n");
            }

            bus.finalizeDatagrams();

            stateMachine.statusWord_ = inputPDO->statusWord;
            stateMachine.update();

            outputPDO->controlWord = stateMachine.controlWord_;
            outputPDO->modeOfOperation = 4;
            outputPDO->targetTorque = 0;
            outputPDO->maxTorque = 3990;
            outputPDO->targetPosition = 0;
            outputPDO->velocityOffset = 0;
            outputPDO->digitalOutput = 0;

            bus.processAwaitingFrames();

            //Flexin
            if (i == 250) {printf("3\n");}
            if (i == 500) {printf("2\n");}
            if (i == 750) {printf("1\n");}
        }
        catch (std::exception const& e)
        {
            DEBUG_PRINT(e.what() << " at " << i << "\n");
        }

        if (i%100 == 0)
        {
            for (auto& em : elmo.mailbox.emergencies)
            {
                printf("Emergency sent : %04x - %s", em.error_code, can::emergency::errorCode::codeToError(em.error_code));
            }
            if (elmo.mailbox.emergencies.size() > 0)
            {
                stateMachine.printState();
                try
                {
                    print_current_state();
                }
                catch(const std::exception& e)
                {
                    DEBUG_PRINT(e.what() << '\n');
                }
                
                elmo.mailbox.emergencies.resize(0);
            }
        }
    }

    //Turn off
    stateMachine.setCommand(can::CANOpenCommand::DISABLE);
    nanoseconds endTime = since_epoch();
    while(stateMachine.isON() && (elapsed_time(endTime) < 5s))
    {
        try
        {
            bus.sendLogicalRead(callback_error);
            bus.finalizeDatagrams();

            stateMachine.statusWord_ = inputPDO->statusWord;
            stateMachine.update();

            outputPDO->controlWord = stateMachine.controlWord_;
            bus.sendLogicalWrite(callback_error);
            bus.processAwaitingFrames();
        }
        catch(const std::exception& e)
        {
            DEBUG_PRINT(e.what() << '\n');
        }
    }
    stateMachine.printState();
    if (stateMachine.isON())
    {
        printf("Timeout : motor failed to stop properly\n");
        return 1;
    }


    return 0;
}

int invalidConfigEtherCAT1(Bus& bus)
{
    printf("Mode of operation : INVALID CONFIG ETHERCAT 1\n");
    auto print_current_state = [&]()
    {
        for (auto& slave : bus.slaves())
        {
            State state = bus.getCurrentState(slave);
            printf("Slave %d state is %s\n", slave.address, toString(state));
        }
    };

    printf("\n\n----Purposely configuring wrong SYNC MANAGERS for Elmo\n");
    uint8_t io_buffer[2048];
    try
    {
        for (auto& slave : bus.slaves())
        {
            bus.sendGetDLStatus(slave);
            bus.finalizeDatagrams();
        }

        //Configure Mapping
        Slave& elmo = bus.slaves().at(1);

        elmo.is_static_mapping = true;
        elmo.input.bsize = 356;
        elmo.input.sync_manager = 3;
        elmo.output.bsize = 8;
        elmo.output.sync_manager = 2;
        
        bus.createMapping(io_buffer);

        bus.requestState(State::SAFE_OP);
        bus.waitForState(State::SAFE_OP, 1s);
        print_current_state();
    }
    catch (ErrorCode const& e)
    {
        std::cerr << e.what() << ": " << ALStatus_to_string(e.code()) << std::endl;
    }
    catch (std::exception const& e)
    {
        std::cerr << e.what() << std::endl;
    }
    return 1;
}

int invalidConfigEtherCAT2(Bus& bus)
{
    printf("Mode of operation : INVALID CONFIG ETHERCAT 2\n");
    auto print_current_state = [&]()
    {
        for (auto& slave : bus.slaves())
        {
            State state = bus.getCurrentState(slave);
            printf("Slave %d state is %s\n", slave.address, toString(state));
        }
    };

    printf("\n\n----Requesting OPERATIONAL state before SAFEOP\n");

    uint8_t io_buffer[2048];
    try
    {
        bus.init();
        print_current_state();

        for (auto& slave : bus.slaves())
        {
            bus.sendGetDLStatus(slave);
            bus.finalizeDatagrams();
        }

        Slave& elmo = bus.slaves().at(1);
        uint32_t Rx_length = sizeof(hal::pdo::elmo::RxMapping);
        bus.writeSDO(elmo, hal::sdo::elmo::RxPDO.index, hal::sdo::elmo::RxPDO.subindex, Bus::Access::COMPLETE, (void*) hal::pdo::elmo::RxMapping, Rx_length);
        uint32_t Tx_length = sizeof(hal::pdo::elmo::TxMapping);
        bus.writeSDO(elmo, hal::sdo::elmo::TxPDO.index, hal::sdo::elmo::TxPDO.subindex, Bus::Access::COMPLETE, (void*) hal::pdo::elmo::TxMapping, Tx_length);
        

        //Configure Mapping
        bus.createMapping(io_buffer);

        bus.requestState(State::OPERATIONAL);
        bus.waitForState(State::OPERATIONAL, 1s);
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
    return 1;
}

int invalidConfigEtherCAT3(Bus& bus)
{
    printf("Mode of operation : INVALID CONFIG ETHERCAT 3\n");
    auto print_current_state = [&]()
    {
        for (auto& slave : bus.slaves())
        {
            State state = bus.getCurrentState(slave);
            printf("Slave %d state is %s\n", slave.address, toString(state));
        }
    };

    printf("\n\n----Requesting OPERATIONAL without sending valid Read/Write\n");

    uint8_t io_buffer[2048];
    try
    {
        bus.init();
        print_current_state();

        for (auto& slave : bus.slaves())
        {
            bus.sendGetDLStatus(slave);
            bus.finalizeDatagrams();
        }

        Slave& elmo = bus.slaves().at(1);
        uint32_t Rx_length = sizeof(hal::pdo::elmo::RxMapping);
        bus.writeSDO(elmo, hal::sdo::elmo::RxPDO.index, hal::sdo::elmo::RxPDO.subindex, Bus::Access::COMPLETE, (void*) hal::pdo::elmo::RxMapping, Rx_length);
        uint32_t Tx_length = sizeof(hal::pdo::elmo::TxMapping);
        bus.writeSDO(elmo, hal::sdo::elmo::TxPDO.index, hal::sdo::elmo::TxPDO.subindex, Bus::Access::COMPLETE, (void*) hal::pdo::elmo::TxMapping, Tx_length);
        

        //Configure Mapping
        bus.createMapping(io_buffer);

        bus.requestState(State::SAFE_OP);
        bus.waitForState(State::SAFE_OP, 1s);
        print_current_state();

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
    return 1;
}

int invalidConfigCANOpen(Bus& bus)
{
    printf("Mode of operation : INVALID CONFIG CANOPEN 1\n");

    printf("\n\n----SM should be invalid\n");
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
        for (auto& slave : bus.slaves())
        {
            bus.sendGetDLStatus(slave);
            bus.finalizeDatagrams();
        }

        //Configure Mapping
        Slave& elmo = bus.slaves().at(1);
        uint32_t Rx_length = sizeof(hal::pdo::elmo::RxMapping);
        bus.writeSDO(elmo, 0x0000, hal::sdo::elmo::RxPDO.subindex, Bus::Access::COMPLETE, (void*) hal::pdo::elmo::RxMapping, Rx_length);
        uint32_t Tx_length = sizeof(hal::pdo::elmo::TxMapping);
        bus.writeSDO(elmo, 0x0000, hal::sdo::elmo::TxPDO.subindex, Bus::Access::COMPLETE, (void*) hal::pdo::elmo::TxMapping, Tx_length);
        
        bus.createMapping(io_buffer);

        bus.requestState(State::SAFE_OP);
        bus.waitForState(State::SAFE_OP, 1s);
        print_current_state();

        auto callback_error = [](DatagramState const&){ THROW_ERROR("something bad happened"); };
        bus.processDataRead(callback_error);
        auto itWillBeWrong = [](DatagramState const&){DEBUG_PRINT("(Previous line was a False alarm) \n");};
        bus.processDataWrite(itWillBeWrong);

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
    return 1;
}


int main(int argc, char* argv[])
{
    int count = 0; 
    while(argv[++count]);
    std::vector<std::string> args = {"", "", "", ""};
    for (int i = 1; i < std::min(count, 4); ++i) {args[i] = std::string(argv[i]);}

    auto socket = std::make_shared<Socket>();
    Bus bus(socket);

    try
    {
        socket->open(argv[1], 5ms);
        bus.init();
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


    if      (args[2] == "--scramble")       { scramble(bus); }
    else if (args[2] == "--encoder-fault")  { encoderFault(bus); }
    else if (args[2] == "--cut-before")     { cutBefore(bus); }
    else if (args[2] == "--cut")            { cut(bus); }
    else if (args[2] == "--realtime-loss")  { realtimeLoss(bus); }
    else if (args[2] == "--conf-can")       { invalidConfigCANOpen(bus); }
    else if (args[2] == "--conf-eth")
    {
        if      (args[3] == "1")        { invalidConfigEtherCAT1(bus); }
        else if (args[3] == "2")        { invalidConfigEtherCAT2(bus); }
        else if (args[3] == "3")        { invalidConfigEtherCAT3(bus); }
        else                            { invalidConfigEtherCAT1(bus); }
    }

    else                                    { printf("No mode of operation\n"); }


    return 0;
}
