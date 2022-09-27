#include <iostream>
#include <fstream>
#include <math.h>
#include <algorithm>

#include "kickcat/Bus.h"
#include "kickcat/Prints.h"
#include "kickcat/Diagnostics.h"
#include "ElmoProtocol.h"
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
    for (uint16_t slave_id = 0; slave_id < (uint16_t) slaves.size(); ++slave_id)
    {
        printf(" %04x ", slave_id);
    }
    printf("|");
    for (uint16_t slave_id = 0; slave_id < (uint16_t) slaves.size(); ++slave_id)
    {
        printf(" %04x ", slave_id);
    }
    printf("|");
    for (uint16_t slave_id = 0; slave_id < (uint16_t) slaves.size(); ++slave_id)
    {
        printf(" %04x ", slave_id);
    }
    printf("|");
    for (uint16_t slave_id = 0; slave_id < (uint16_t) slaves.size(); ++slave_id)
    {
        printf(" %04x ", slave_id);
    }

    printf("\n");
    for (auto port = 0; port < 4; ++port)
    {
        printf("Port %i : ", port);

        //PHY
        for (uint16_t slave_id = 0; slave_id < (uint16_t) slaves.size(); ++slave_id)
        {
            if (slaves.at(slave_id).error_counters.rx[port].physical_layer > 0) {check = false;}
            printf("  %02x  ", slaves.at(slave_id).error_counters.rx[port].physical_layer);
        }
        printf("|");

        //Invalid
        for (uint16_t slave_id = 0; slave_id < (uint16_t) slaves.size(); ++slave_id)
        {
            if (slaves.at(slave_id).error_counters.rx[port].invalid_frame > 0) {check = false;}
            printf("  %02x  ", slaves.at(slave_id).error_counters.rx[port].invalid_frame);
        }
        printf("|");

        //Forwarded
        for (uint16_t slave_id = 0; slave_id < (uint16_t) slaves.size(); ++slave_id)
        {
            if (slaves.at(slave_id).error_counters.forwarded[port] > 0) {check = false;}
            printf("  %02x  ", slaves.at(slave_id).error_counters.forwarded[port]);
        }
        printf("|");

        for (uint16_t slave_id = 0; slave_id < (uint16_t) slaves.size(); ++slave_id)
        {
            if (slaves.at(slave_id).error_counters.lost_link[port] > 0) {check = false;}
            printf("  %02x  ", slaves.at(slave_id).error_counters.lost_link[port]);
        }
        printf("\n");
    }

    return check;
}

int main(int argc, char* argv[])
{
    auto socket = std::make_shared<Socket>();
    Bus bus(socket);

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
        socket->open(argv[1], 10ms);
        bus.init();
        
        bus.createMapping(io_buffer);

        bus.requestState(State::SAFE_OP);
        bus.waitForState(State::SAFE_OP, 1s);
        print_current_state();

        auto callback_error = [](DatagramState const&){ THROW_ERROR("something bad happened"); };
        bus.processDataRead(callback_error);
        auto itWillBeWrong = [](DatagramState const&){printf("(Previous line was a False alarm) \n");};
        bus.processDataWrite(itWillBeWrong);

        bus.requestState(State::OPERATIONAL);
        bus.waitForState(State::OPERATIONAL, 100ms);
        print_current_state();

        for (auto& slave : bus.slaves())
        {
            bus.sendGetDLStatus(slave);
            bus.finalizeDatagrams();
        }
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
    hal::pdo::elmo::Output* outputPDO {nullptr};
    outputPDO = reinterpret_cast<hal::pdo::elmo::Output*>(elmo.output.data);
    inputPDO = reinterpret_cast<hal::pdo::elmo::Input*>(elmo.input.data);
    can::CANOpenStateMachine stateMachine;


    //Turn on
    stateMachine.setCommand(can::CANOpenCommand::ENABLE);
    while(not stateMachine.isON())
    {
        bus.sendLogicalRead(callback_error);
        bus.finalizeDatagrams();
        bus.processAwaitingFrames();

        stateMachine.statusWord_ = inputPDO->statusWord;
        stateMachine.update();

        outputPDO->controlWord = stateMachine.controlWord_;
        bus.sendLogicalWrite(callback_error);

    }
    stateMachine.printState();

    // Main Loop
    int64_t last_error = 0;
    for (int64_t i = 0; i < 50000; ++i)
    {
        sleep(1ms);

        try
        {
            bus.sendLogicalRead(callback_error);
            bus.sendLogicalWrite(callback_error);

            bus.sendRefreshErrorCounters(callback_error);
            bus.finalizeDatagrams();

            stateMachine.statusWord_ = inputPDO->statusWord;
            stateMachine.update();

            outputPDO->controlWord = stateMachine.controlWord_;
            outputPDO->modeOfOperation = 4;
            outputPDO->targetTorque = 10;
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
            PortsAnalysis(bus.slaves());
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
