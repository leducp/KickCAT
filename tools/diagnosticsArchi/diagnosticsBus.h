#ifndef DIAGNOSTICS_BUS
#define DIAGNOSTICS_BUS

#include <iostream>
#include <fstream>
#include <math.h>
#include <algorithm>
#include <string>
#include <unordered_map>
#include <cassert>
#include <future>
#include <thread>

#include "kickcat/Bus.h"
#include "kickcat/Diagnostics.h"
#include "diagnosticsAPI.h"
#include "kickcat/DebugHelpers.h"

#include "ElmoProtocol.h"
#include "CANErrors.h"

using namespace kickcat;

class DiagnosticsBus : public Bus
{
    enum class DiagBusStateMachine {IDLE, ROUTINE, HARDSTOP};
    class config
    {
    public:
        std::unordered_map<const char*, int> mapping; 
        std::string type;
        std::string name;

        bool isStaticMapping() { if (mapping.find("static_mapping") != mapping.end()) { return (mapping["static_mapping"] == 1);} else { return false;} }
        int getOutputSize() {return mapping["output_size"];}
        int getOutputSM() {return mapping["output_sync_manager"];}
        int getInputSize() {return mapping["input_size"];}
        int getInputSM() {return mapping["input_sync_manager"];}
        void importMapping(std::string& map_string)  //ex  "os:345/osm:2/is:8/ism:3/"
        {
            mapping.clear();
            mapping.emplace("static_mapping", 1);
            std::string temp_os = map_string.substr(map_string.find("os:"));
            mapping.emplace("output_size", std::stoi( temp_os.substr(3, temp_os.find("/")), 0, 10 ));
            std::string temp_osm = map_string.substr(map_string.find("osm:"));
            mapping.emplace("output_sync_manager", std::stoi( temp_osm.substr(4, temp_osm.find("/")), 0, 10 ));
            std::string temp_is = map_string.substr(map_string.find("is:"));
            mapping.emplace("input_size", std::stoi( temp_is.substr(3, temp_is.find("/")), 0, 10 ));
            std::string temp_ism = map_string.substr(map_string.find("ism:"));
            mapping.emplace("input_sync_manager", std::stoi( temp_ism.substr(4, temp_ism.find("/")), 0, 10 ));
        }
        void importType(std::string type_str) {type = std::move(type_str);}
        void importName(std::string name_str) {name = std::move(name_str);}
    };


public:
    
    DiagnosticsBus(std::shared_ptr<AbstractSocket> socket) : Bus(socket) {}

    //Small stuff
    uint8_t memory_io_buffer[2048];
    uint32_t address;
    std::queue<APIMessage> to_process;
    Slave* slaveAdressToSlave(uint16_t const& slave_address)
    {
        for (auto& slave_temp : slaves_){if (slave_temp.address == slave_address) {return &slave_temp;}}
        THROW_ERROR("Invalid slave address");
    }
    bool isHardStopped()
    {
        return (stateMachine == DiagBusStateMachine::HARDSTOP);
    }

    //Communications
    void reply(API& api, APIMessage const& msg, APIRequestState const& state, std::string content)
    {
        static_cast<void>(std::async(std::launch::async, [&api, &msg, &state, &content]{
            api.sendMail({APIDest::SERVER, msg.header, state, msg.id, content});
        }));
    }

    void sendAddGeneralError(API& api, ErrorGeneral const& err)
    {
        static_cast<void>(std::async(std::launch::async, [&api, &err]{
            api.addGeneralError(err);
        }));
    }

    void sendAddProcessError(API& api, ErrorProcess const& err)
    {
        static_cast<void>(std::async(std::launch::async, [&api, &err]{
            api.addProcessError(err);
        }));
    }

    void sendAddIndividualErroredFrame(API& api, uint16_t const& slave_address, std::tuple<std::chrono::nanoseconds, ecatCommand, kickcat::DatagramState> const& err)
    {
        static_cast<void>(std::async(std::launch::async, [&api, &slave_address, &err]{
            api.addIndividualErroredFrame(slave_address, err);
        }));
    }

    void sendAddBroadcastErroredFrame(API& api, std::tuple<std::chrono::nanoseconds, ecatCommand, kickcat::DatagramState> const& err)
    {
        static_cast<void>(std::async(std::launch::async, [&api, &err]{
            api.addBroadcastErroredFrame(err);
        }));
    }

    void checkMailbox(API& api, std::queue<APIMessage>& to_process)
    {
        static_cast<void>(std::async(std::launch::async, [&api, &to_process]{
            APIMessage msg = api.checkMail(APIDest::BUS);
            if (msg.header != APIHeader::NOP)
            {
                to_process.push(msg);
            };
        }));
    }


    //Main function
    void run(API& api)
    {
        checkMailbox(api, to_process);

        if (not to_process.empty())
        {
            APIMessage msg = to_process.front();
            to_process.pop();

            switch (msg.header)
            {
                case APIHeader::CONTROL :
                {
                    if (msg.content == std::string("IDLE"))
                    {
                        if (stateMachine == DiagBusStateMachine::ROUTINE)
                        {
                            stateMachine = DiagBusStateMachine::IDLE;
                            reply(api, msg, APIRequestState::ACK_OK, std::string(""));
                        }
                        else
                        {
                            reply(api, msg, APIRequestState::ACK_ISSUE, std::string("Already in IDLE mode"));
                        }
                    }
                    else if (msg.content == std::string("ROUTINE"))
                    {
                        if (stateMachine == DiagBusStateMachine::IDLE)
                        {
                            stateMachine = DiagBusStateMachine::ROUTINE;
                            reply(api, msg, APIRequestState::ACK_OK, std::string(""));
                        }
                        else
                        {
                            reply(api, msg, APIRequestState::ACK_ISSUE, std::string("Already in ROUTINE mode"));
                        }
                    }
                    else if (msg.content == std::string("HARDSTOP"))
                    {
                        stateMachine = DiagBusStateMachine::HARDSTOP;
                        reply(api, msg, APIRequestState::ACK_OK, std::string(""));
                    }
                }; break;

                case APIHeader::COMMAND :
                {
                    //Actions available in any mode

                    //Read reg: "GET_REGISTER-1@0013::1"
                    //Single slave commands : "GET_CURRENT_STATE-0"
                    if      (msg.content.substr(0, msg.content.find("-")) == std::string("GET_CURRENT_STATE"))
                    {
                        uint16_t address = 0;
                        try { address = std::stoi(msg.content.substr(msg.content.find("-")+1), 0, 10); }
                        catch( ... ) { reply(api, msg, APIRequestState::ACK_ISSUE, std::string("MALFORMED_REQUEST")); break; }
                        wrapperGetCurrentState_single(api, address);
                        reply(api, msg, APIRequestState::ACK_OK, std::string(""));
                        break;
                    }
                    else if (msg.content.substr(0, msg.content.find("-")) == std::string("GET_DL_STATUS"))
                    {
                        uint16_t address = 0;
                        try { address = std::stoi(msg.content.substr(msg.content.find("-")+1), 0, 10); }
                        catch( ... ) { reply(api, msg, APIRequestState::ACK_ISSUE, std::string("MALFORMED_REQUEST")); break; }
                        wrapperSendGetDLStatus_single(api, address);
                        reply(api, msg, APIRequestState::ACK_OK, std::string(""));
                        break;
                    }
                    else if (msg.content.substr(0, msg.content.find("-")) == std::string("REFRESH_ERRORCOUNTERS"))
                    {
                        uint16_t address = 0;
                        try { address = std::stoi(msg.content.substr(msg.content.find("-")+1), 0, 10); }
                        catch( ... ) { reply(api, msg, APIRequestState::ACK_ISSUE, std::string("MALFORMED_REQUEST")); break; }
                        wrapperRefreshErrorCounters_single(api, address);
                        reply(api, msg, APIRequestState::ACK_OK, std::string(""));
                        break;
                    }
                    else if (msg.content.substr(0, msg.content.find("-")) == std::string("GET_REGISTER"))
                    {
                        uint16_t reg = 0;
                        int size = 0;
                        uint16_t address = 0;
                        try
                        {
                            size_t del_first = msg.content.find("-")+1;
                            size_t del_second = msg.content.find("@")+1;
                            size_t del_third = msg.content.find("::")+2;
                            assert((del_first != del_second) & (del_second != del_third));

                            address = std::stoi(msg.content.substr(del_first, del_second - del_first - 1), 0, 10);
                            reg = std::stoi(msg.content.substr(del_second, del_third - del_second - 2), 0, 10);
                            size = std::stoi(msg.content.substr(del_third), 0, 10);
                        }
                        catch( ... )
                        {
                            reply(api, msg, APIRequestState::ACK_ISSUE, std::string("MALFORMED_REQUEST"));
                            break;
                        }

                        switch(size)
                        {
                            case 1 :
                            {
                                std::pair<APIRequestState, uint8_t> res = wrapperSendGetRegister<uint8_t>(api, address, reg);
                                reply(api, msg, res.first, std::to_string(res.second));
                                break;
                            }
                            case 2 :
                            {
                                std::pair<APIRequestState, uint16_t> res = wrapperSendGetRegister<uint16_t>(api, address, reg);
                                reply(api, msg, res.first, std::to_string(res.second));
                                break;
                            }
                            case 4 :
                            {
                                std::pair<APIRequestState, uint32_t> res = wrapperSendGetRegister<uint32_t>(api, address, reg);
                                reply(api, msg, res.first, std::to_string(res.second));
                                break;
                            }
                            default :
                            {
                                reply(api, msg, APIRequestState::ACK_ISSUE, std::string("INVALID_SIZE"));
                            }; break;
                        }
                        break;

                    }
                    //Write reg: "WRITE_REGISTER-1@0013::1/153"
                    else if (msg.content.substr(0, msg.content.find("-")) == std::string("WRITE_REGISTER"))
                    {
                        uint16_t reg = 0;
                        int size = 0;
                        uint16_t address = 0;
                        int value = 0;
                        try
                        {
                            size_t del_first = msg.content.find("-")+1;
                            size_t del_second = msg.content.find("@")+1;
                            size_t del_third = msg.content.find("::")+2;
                            size_t del_fourth = msg.content.find("/")+1;
                            assert((del_first != del_second) & (del_second != del_third) & (del_third != del_third));

                            address = std::stoi(msg.content.substr(del_first, del_second - del_first - 1));
                            reg = std::stoi(msg.content.substr(del_second, del_third - del_second - 2));
                            size = std::stoi(msg.content.substr(del_third, del_fourth - del_third - 1));
                            value = std::stoi(msg.content.substr(del_fourth));
                        }
                        catch( ... )
                        {
                            reply(api, msg, APIRequestState::ACK_ISSUE, std::string("MALFORMED_REQUEST"));
                            break;
                        }

                        switch(size)
                        {
                            case 1 :
                            {
                                uint8_t value_write = (uint8_t) value;
                                APIRequestState res = wrapperSendWriteRegister<uint8_t>(api, address, reg, value_write);
                                reply(api, msg, res, std::string(""));
                                break;
                            }
                            case 2 :
                            {
                                uint16_t value_write = (uint16_t) value;
                                APIRequestState res = wrapperSendWriteRegister<uint16_t>(api, address, reg, value_write);
                                reply(api, msg, res, std::string(""));
                                break;
                            }
                            case 4 :
                            {
                                uint32_t value_write = (uint32_t) value;
                                APIRequestState res = wrapperSendWriteRegister<uint32_t>(api, address, reg, value_write);
                                reply(api, msg, res, std::string(""));
                                break;
                            }
                            default :
                            {
                                reply(api, msg, APIRequestState::ACK_ISSUE, std::string("INVALID_SIZE"));
                            }
                        }
                        break;
                    }
                    //Broadcast commands (no args)
                    else if (msg.content == std::string("ALL_GET_CURRENT_STATE"))
                        {
                            wrapperGetCurrentState(api);
                            reply(api, msg, APIRequestState::ACK_OK, std::string(""));
                            break;
                        }
                    else if (msg.content == std::string("ALL_GET_DLSTATUS"))
                    {
                        wrapperSendGetDLStatus(api);
                        reply(api, msg, APIRequestState::ACK_OK, std::string(""));
                        break;
                    }
                    else if (msg.content == std::string("ALL_REFRESH_ERRORCOUNTERS"))
                    {
                        wrapperRefreshErrorCounters(api);
                        reply(api, msg, APIRequestState::ACK_OK, std::string(""));
                        break;
                    }

                    if (stateMachine == DiagBusStateMachine::IDLE)
                    {
                        //Broadcast commands (no args)
                        if      (msg.content == std::string("DETECT_SLAVES"))
                        {
                            wrapperDetectSlaves(api);
                            reply(api, msg, APIRequestState::ACK_OK, std::string(""));
                        }
                        else if (msg.content == std::string("RESET_SLAVES"))
                        {
                            wrapperReset(api);
                            reply(api, msg, APIRequestState::ACK_OK, std::string(""));
                        }
                        else if (msg.content == std::string("ALL_GO_INIT"))
                        {
                            wrapperGoInit(api);
                            reply(api, msg, APIRequestState::ACK_OK, std::string(""));
                        }
                        else if (msg.content == std::string("ALL_GO_PREOP"))
                        {
                            wrapperGoPreOp(api);
                            reply(api, msg, APIRequestState::ACK_OK, std::string(""));
                        }
                        else if (msg.content == std::string("ALL_SETUP_IO"))
                        {
                            wrapperSetupIO(api);
                            reply(api, msg, APIRequestState::ACK_OK, std::string(""));
                        }
                        else if (msg.content == std::string("ALL_GO_SAFEOP"))
                        {
                            wrapperGoSafeOp(api);
                            reply(api, msg, APIRequestState::ACK_OK, std::string(""));
                        }
                        else if (msg.content == std::string("ALL_GO_OP"))
                        {
                            wrapperGoOp(api);
                            reply(api, msg, APIRequestState::ACK_OK, std::string(""));
                        }
                        else if (msg.content == std::string("ALL_TURN_ON"))
                        {
                            wrapperCANTurnAllOn(api);
                            reply(api, msg, APIRequestState::ACK_OK, std::string(""));
                        }
                        else if (msg.content == std::string("ALL_TURN_OFF"))
                        {
                            wrapperCANTurnAllOff(api);
                            reply(api, msg, APIRequestState::ACK_OK, std::string(""));
                        }
                        else if (msg.content == std::string("ALL_UNIT_ROUTINE"))
                        {
                            wrapperUnitRoutine(api);
                            reply(api, msg, APIRequestState::ACK_OK, std::string(""));
                        }
                        
                        //Single slave commands : "GO_INIT-10"
                        else if (msg.content.substr(0, msg.content.find("-")) == std::string("GO_INIT"))
                        {
                            uint16_t address = 0;
                            try { address = std::stoi(msg.content.substr(msg.content.find("-")+1), 0, 10); }
                            catch( ... ) { reply(api, msg, APIRequestState::ACK_ISSUE, std::string("MALFORMED_REQUEST")); break; }
                            wrapperGoInit_single(api, address);
                            reply(api, msg, APIRequestState::ACK_OK, std::string(""));
                        }
                        else if (msg.content.substr(0, msg.content.find("-")) == std::string("GO_PREOP"))
                        {
                            uint16_t address = 0;
                            try { address = std::stoi(msg.content.substr(msg.content.find("-")+1), 0, 10); }
                            catch( ... ) { reply(api, msg, APIRequestState::ACK_ISSUE, std::string("MALFORMED_REQUEST")); break; }
                            wrapperGoPreOp_single(api, address);
                            reply(api, msg, APIRequestState::ACK_OK, std::string(""));
                        }
                        else if (msg.content.substr(0, msg.content.find("-")) == std::string("SETUP_IO"))
                        {
                            uint16_t address = 0;
                            try { address = std::stoi(msg.content.substr(msg.content.find("-")+1), 0, 10); }
                            catch( ... ) { reply(api, msg, APIRequestState::ACK_ISSUE, std::string("MALFORMED_REQUEST")); break; }
                            wrapperSetupIO_single(api, address);
                            reply(api, msg, APIRequestState::ACK_OK, std::string(""));
                        }
                        else if (msg.content.substr(0, msg.content.find("-")) == std::string("GO_SAFEOP"))
                        {
                            uint16_t address = 0;
                            try { address = std::stoi(msg.content.substr(msg.content.find("-")+1), 0, 10); }
                            catch( ... ) { reply(api, msg, APIRequestState::ACK_ISSUE, std::string("MALFORMED_REQUEST")); break; }
                            wrapperGoSafeOp_single(api, address);
                            reply(api, msg, APIRequestState::ACK_OK, std::string(""));
                        }
                        else if (msg.content.substr(0, msg.content.find("-")) == std::string("GO_OP"))
                        {
                            uint16_t address = 0;
                            try { address = std::stoi(msg.content.substr(msg.content.find("-")+1), 0, 10); }
                            catch( ... ) { reply(api, msg, APIRequestState::ACK_ISSUE, std::string("MALFORMED_REQUEST")); break; }
                            wrapperGoOp_single(api, address);
                            reply(api, msg, APIRequestState::ACK_OK, std::string(""));
                        }
                        
                        //Config : "INJECT_MAPPING-10::os:345/osm:2/is:8/ism:3/"
                        else if (msg.content.substr(0, msg.content.find("-")) == std::string("INJECT_MAPPING"))
                        {
                            uint16_t address = 0;
                            try { address = std::stoi(msg.content.substr(msg.content.find("-")+1), 0, 10); }
                            catch( ... ) { reply(api, msg, APIRequestState::ACK_ISSUE, std::string("MALFORMED_REQUEST")); break; }
                            try
                            {
                                std::string map_string = msg.content.substr(msg.content.find("::")+2);
                                injectSingleMapping(address, map_string);
                                reply(api, msg, APIRequestState::ACK_OK, std::string(""));
                            }
                            catch(const std::out_of_range& e)
                            {
                                reply(api, msg, APIRequestState::ACK_ISSUE, std::string("INVALID_SLAVE_ADDRESS"));
                                break;
                            }
                            catch( ... )
                            {
                                reply(api, msg, APIRequestState::ACK_ISSUE, std::string("MALFORMED_REQUEST"));
                                break;
                            }
                            
                        }
                        //Type : "INJECT_TYPE-0::ElmoMotor"
                        else if (msg.content.substr(0, msg.content.find("-")) == std::string("INJECT_TYPE"))
                        {
                            uint16_t address = 0;
                            try { address = std::stoi(msg.content.substr(msg.content.find("-")+1), 0, 10); }
                            catch( ... ) { reply(api, msg, APIRequestState::ACK_ISSUE, std::string("MALFORMED_REQUEST")); break; }
                            try
                            {
                                std::string type_string = msg.content.substr(msg.content.find("::")+2);
                                injectSingleType(address, type_string);
                                reply(api, msg, APIRequestState::ACK_OK, std::string(""));
                            }
                            catch(const std::out_of_range& e)
                            {
                                reply(api, msg, APIRequestState::ACK_ISSUE, std::string("INVALID_SLAVE_ADDRESS"));
                                break;
                            }
                            catch( ... )
                            {
                                reply(api, msg, APIRequestState::ACK_ISSUE, std::string("MALFORMED_REQUEST"));
                                break;
                            }
                            
                        }
                        //Name : "INJECT_NAME-0::LeftRightAnkle"
                        else if (msg.content.substr(0, msg.content.find("-")) == std::string("INJECT_NAME"))
                        {
                            uint16_t address = 0;
                            try { address = std::stoi(msg.content.substr(msg.content.find("-")+1), 0, 10); }
                            catch( ... ) { reply(api, msg, APIRequestState::ACK_ISSUE, std::string("MALFORMED_REQUEST")); break; }
                            try
                            {
                                std::string name_string = msg.content.substr(msg.content.find("::")+2);
                                injectSingleName(address, name_string);
                                reply(api, msg, APIRequestState::ACK_OK, std::string(""));
                            }
                            catch(const std::out_of_range& e)
                            {
                                reply(api, msg, APIRequestState::ACK_ISSUE, std::string("INVALID_SLAVE_ADDRESS"));
                                break;
                            }
                            catch( ... )
                            {
                                reply(api, msg, APIRequestState::ACK_ISSUE, std::string("MALFORMED_REQUEST"));
                                break;
                            }
                            
                        }
                        //Topology : "COMPARE_TOPOLOGY-0:0/1:0/2:1/"
                        else if (msg.content.substr(0, msg.content.find("-")) == std::string("COMPARE_TOPOLOGY"))
                        {
                            std::unordered_map<uint16_t, uint16_t> expected_topology;
                            try
                            {
                                expected_topology = expectedTopology(msg.content.substr(msg.content.find("-")+1));
                            }
                            catch( ... )
                            {
                                reply(api, msg, APIRequestState::ACK_ISSUE, std::string("INVALID_ARGUMENT"));
                                break;
                            }

                            wrapperSendGetDLStatus(api);

                            if (getTopology(slaves_) == expected_topology)
                            {
                                reply(api, msg, APIRequestState::ACK_OK, std::string(""));
                            }
                            else
                            {
                                std::string response;
                                for (auto& it : expected_topology)
                                {
                                    response = response + std::to_string(it.first) + std::string(":") + std::to_string(it.second) + std::string("/");
                                }
                                reply(api, msg, APIRequestState::ACK_ISSUE, response);
                            }
                            
                        }
                    }
                    else if (stateMachine == DiagBusStateMachine::ROUTINE)
                    {
                        //Need to add PDO modifiers
                    }

                }; break;

                default: break;
            }
        }

        sleep(1ms);
        if (stateMachine == DiagBusStateMachine::ROUTINE)
        {
            wrapperUnitRoutine(api);
        }
    }

    //wrappers
    void wrapperDetectSlaves(API& api)
    {
        bool check = true;
        nanoseconds timestamp = since_epoch();
        int32_t detected = 0;
        try
        {
            detected = detectSlaves();
        }
        catch(const std::system_error& e)
        {
            if (e.code().value() == 110) { sendAddGeneralError(api, {timestamp, "Cannot read socket"}); }
            else { sendAddGeneralError(api, {timestamp, e.what()}); }
            sendAddProcessError(api, {timestamp, processState::NO_SLAVE_DETECTED});
            check = false;
        }
        catch( ... )
        {
            sendAddGeneralError(api, {timestamp, "Unknown error"});
            sendAddProcessError(api, {timestamp, processState::NO_SLAVE_DETECTED});
            check = false;
        }
        
        if (detected == 0)
        {
            sendAddProcessError(api, {timestamp, processState::NO_SLAVE_DETECTED});
            check = false;
        }
        else
        {
            api.resizeSlavesErrorContainer(detected);
            slavesConfig.resize(detected);
        }
        if (check) {sendAddProcessError(api, {timestamp, processState::SLAVE_DETECTION_OK});}
    }

    void wrapperReset(API& api)
    {
        std::fill(std::begin(memory_io_buffer), std::end(memory_io_buffer), 0);
        bool check = true;
        nanoseconds timestamp = since_epoch();
        try
        {
            resetSlaves(100ms);
        }
        catch(const ErrorDatagram& e)
        {
            sendAddBroadcastErroredFrame(api, {timestamp, ecatCommand::RESET_SLAVES, e.state()});
            sendAddProcessError(api, {timestamp, processState::RESET_SLAVES_FAILED});
            check = false;
        }
        catch(const std::system_error& e)
        {
            if (e.code().value() == 110) { sendAddGeneralError(api, {timestamp, "Cannot read socket"}); }
            else { sendAddGeneralError(api, {timestamp, e.what()}); }
            sendAddBroadcastErroredFrame(api, {timestamp, ecatCommand::RESET_SLAVES, DatagramState::LOST});
            sendAddProcessError(api, {timestamp, processState::RESET_SLAVES_FAILED});
            check = false;
        }
        catch( ... )
        {
            sendAddGeneralError(api, {timestamp, "Unknown error"});
            sendAddProcessError(api, {timestamp, processState::RESET_SLAVES_FAILED});
            check = false;
        }
        

        timestamp = since_epoch();
        try
        {
            setAddresses();
        }
        catch(const ErrorDatagram& e)
        {
            sendAddBroadcastErroredFrame(api, {timestamp, ecatCommand::SET_ADDRESSES, e.state()});
            sendAddProcessError(api, {timestamp, processState::RESET_SLAVES_FAILED});
            check = false;
        }
        catch(const Error& e)
        {
            sendAddProcessError(api, {timestamp, processState::RESET_SLAVES_FAILED});
            sendAddGeneralError(api, {timestamp, e.what()});
            check = false;
        }
        catch(const std::system_error& e)
        {
            if (e.code().value() == 110) { sendAddGeneralError(api, {timestamp, "Cannot read socket"}); }
            else { sendAddGeneralError(api, {timestamp, e.what()}); }
            sendAddBroadcastErroredFrame(api, {timestamp, ecatCommand::SET_ADDRESSES, DatagramState::LOST});
            sendAddProcessError(api, {timestamp, processState::RESET_SLAVES_FAILED});
            check = false;
        }
        catch( ... )
        {
            sendAddGeneralError(api, {timestamp, "Unknown error"});
            sendAddProcessError(api, {timestamp, processState::RESET_SLAVES_FAILED});
            check = false;
        }

        if (check) {sendAddProcessError(api, {timestamp, processState::RESET_SLAVES_OK});}
        
    }

    void wrapperGoInit(API& api)
    {
        bool check = true;
        nanoseconds timestamp = since_epoch();
        try
        {
            requestState(State::INIT);
        }
        catch(const Error& e)
        {
            if (std::string(e.msg()) == "Invalid working counter") //Function is used in different contexts
            {
                sendAddBroadcastErroredFrame(api, {timestamp, ecatCommand::REQUEST_ECAT_INIT, DatagramState::INVALID_WKC});
                sendAddProcessError(api, {timestamp, processState::ECAT_INIT_FAILED});
            }
            check = false;
        }
        catch(const std::system_error& e)
        {
            if (e.code().value() == 110) { sendAddGeneralError(api, {timestamp, "Cannot read socket"}); }
            else { sendAddGeneralError(api, {timestamp, e.what()}); }
            sendAddProcessError(api, {timestamp, processState::ECAT_INIT_FAILED});
            sendAddBroadcastErroredFrame(api, {timestamp, ecatCommand::REQUEST_ECAT_INIT, DatagramState::LOST});
            check = false;
        }
        catch( ... )
        {
            sendAddGeneralError(api, {timestamp, "Unknown error"});
            sendAddProcessError(api, {timestamp, processState::ECAT_INIT_FAILED});
            check = false;
        }
        

        timestamp = since_epoch();
        try
        {
            waitForState(State::INIT, 5000ms);
        }
        catch (ErrorDatagram const& e)
        {
            sendAddBroadcastErroredFrame(api, {timestamp, ecatCommand::WAIT_ECAT_INIT, e.state()});
            sendAddProcessError(api, {timestamp, processState::ECAT_INIT_FAILED});
            check = false;
        }
        catch (ErrorCode const& e)
        {
            sendAddGeneralError(api, {timestamp, ALStatus_to_string(e.code())});
            sendAddProcessError(api, {timestamp, processState::ECAT_INIT_FAILED});
            check = false;
        }
        catch(const Error& e)
        {
            if (std::string(e.msg()) == "Timeout") {sendAddProcessError(api, {timestamp, processState::ECAT_INIT_TIMEOUT});}
            if (std::string(e.msg()) == "Invalid working counter") {sendAddProcessError(api, {timestamp, processState::ECAT_INIT_FAILED}); sendAddBroadcastErroredFrame(api, {timestamp, ecatCommand::WAIT_ECAT_INIT, DatagramState::INVALID_WKC});}
            if (std::string(e.msg()) == "State transition error") {sendAddProcessError(api, {timestamp, processState::ECAT_INIT_INVALID_STATE_TRANSITION});}
            check = false;
        }
        catch(const std::system_error& e)
        {
            if (e.code().value() == 110) { sendAddGeneralError(api, {timestamp, "Cannot read socket"}); }
            else { sendAddGeneralError(api, {timestamp, e.what()}); }
            sendAddProcessError(api, {timestamp, processState::ECAT_INIT_FAILED});
            sendAddBroadcastErroredFrame(api, {timestamp, ecatCommand::WAIT_ECAT_INIT, DatagramState::LOST});
            check = false;
        }
        catch( ... )
        {
            sendAddGeneralError(api, {timestamp, "Unknown error"});
            sendAddProcessError(api, {timestamp, processState::ECAT_INIT_FAILED});
            check = false;
        }

        for (auto& slave : slaves_)
        {
            api.slaveUpdateALStatus(slave.address, slave.al_status);
            api.slaveUpdateALStatusCode(slave.address, slave.al_status_code);
        }
        if (check) {sendAddProcessError(api, {timestamp, processState::ECAT_INIT_OK});}
        
    }

    void wrapperGoInit_single(API& api, uint16_t const& slave_address)
    {   
        bool check = true;
        nanoseconds timestamp = since_epoch();
        Slave* slave;
        try
        {
            slave = slaveAdressToSlave(slave_address);
        }
        catch(const Error& e)
        {
            sendAddGeneralError(api, {timestamp, e.what()});
            return;
        }

        try
        {
            requestState_single(*slave, State::INIT);
        }
        catch(const Error& e)
        {
            if (std::string(e.msg()) == "Invalid working counter") //Function is used in different contexts
            {
                sendAddIndividualErroredFrame(api,  slave_address, {timestamp, ecatCommand::REQUEST_ECAT_INIT, DatagramState::INVALID_WKC});
                sendAddProcessError(api, {timestamp, processState::ECAT_INIT_FAILED});
            }
            check = false;
        }
        catch(const std::system_error& e)
        {
            if (e.code().value() == 110) { sendAddGeneralError(api, {timestamp, "Cannot read socket"}); }
            else { sendAddGeneralError(api, {timestamp, e.what()}); }
            sendAddProcessError(api, {timestamp, processState::ECAT_INIT_FAILED});
            sendAddIndividualErroredFrame(api,  slave_address, {timestamp, ecatCommand::REQUEST_ECAT_INIT, DatagramState::LOST});
            check = false;
        }
        catch( ... )
        {
            sendAddGeneralError(api, {timestamp, "Unknown error"});
            sendAddProcessError(api, {timestamp, processState::ECAT_INIT_FAILED});
            check = false;
        }
        

        timestamp = since_epoch();
        try
        {
            waitForState_single(*slave, State::INIT, 5000ms);
        }
        catch (ErrorDatagram const& e)
        {
            sendAddIndividualErroredFrame(api,  slave_address, {timestamp, ecatCommand::WAIT_ECAT_INIT, e.state()});
            sendAddProcessError(api, {timestamp, processState::ECAT_INIT_FAILED});
            check = false;
        }
        catch (ErrorCode const& e)
        {
            sendAddGeneralError(api, {timestamp, ALStatus_to_string(e.code())});
            sendAddProcessError(api, {timestamp, processState::ECAT_INIT_FAILED});
            check = false;
        }
        catch(const Error& e)
        {
            if (std::string(e.msg()) == "Timeout") {sendAddProcessError(api, {timestamp, processState::ECAT_INIT_TIMEOUT});}
            if (std::string(e.msg()) == "Invalid working counter") {sendAddProcessError(api, {timestamp, processState::ECAT_INIT_FAILED}); sendAddBroadcastErroredFrame(api, {timestamp, ecatCommand::WAIT_ECAT_INIT, DatagramState::INVALID_WKC});}
            if (std::string(e.msg()) == "State transition error") {sendAddProcessError(api, {timestamp, processState::ECAT_INIT_INVALID_STATE_TRANSITION});}
            check = false;
        }
        catch(const std::system_error& e)
        {
            if (e.code().value() == 110) { sendAddGeneralError(api, {timestamp, "Cannot read socket"}); }
            else { sendAddGeneralError(api, {timestamp, e.what()}); }
            sendAddProcessError(api, {timestamp, processState::ECAT_INIT_FAILED});
            sendAddIndividualErroredFrame(api,  slave_address, {timestamp, ecatCommand::WAIT_ECAT_INIT, DatagramState::LOST});
            check = false;
        }
        catch( ... )
        {
            sendAddGeneralError(api, {timestamp, "Unknown error"});
            sendAddProcessError(api, {timestamp, processState::ECAT_INIT_FAILED});
            check = false;
        }

        api.slaveUpdateALStatus(slave_address, slave->al_status);
        api.slaveUpdateALStatusCode(slave_address, slave->al_status_code);
        if (check) {sendAddProcessError(api, {timestamp, processState::ECAT_INIT_OK});}
        
    }

    void wrapperGoPreOp(API& api)
    {
        bool check = true;
        nanoseconds timestamp = since_epoch();
        try
        {
            fetchEeprom();
        }
        catch(const ErrorDatagram& e)
        {
            sendAddProcessError(api, {timestamp, processState::ECAT_FETCH_EEPROM_FAILED});
            sendAddBroadcastErroredFrame(api, {timestamp, ecatCommand::READ_EEPROM, e.state()});
            check = false;
        }
        catch(const Error& e)
        {
            if (std::string(e.msg()) == "Timeout") {sendAddProcessError(api, {timestamp, processState::ECAT_FETCH_EEPROM_TIMEOUT});}
            else if (std::string(e.msg()) == "Invalid working counter") {sendAddProcessError(api, {timestamp, processState::ECAT_FETCH_EEPROM_FAILED});}
            else { sendAddGeneralError(api, {timestamp, e.what()}); }
            check = false;
        }
        catch(const std::system_error& e)
        {
            if (e.code().value() == 110) { sendAddGeneralError(api, {timestamp, "Cannot read socket"}); }
            else { sendAddGeneralError(api, {timestamp, e.what()}); }
            sendAddProcessError(api, {timestamp, processState::ECAT_FETCH_EEPROM_FAILED});
            check = false;
        }
        catch( ... )
        {
            sendAddGeneralError(api, {timestamp, "Unknown error"});
            sendAddProcessError(api, {timestamp, processState::ECAT_FETCH_EEPROM_FAILED});
            check = false;
        }
        if (check) {sendAddProcessError(api, {timestamp, processState::ECAT_FETCH_EEPROM_OK});}
        

        check = true;
        timestamp = since_epoch();
        try
        {
            configureMailboxes();
        }
        catch(const ErrorDatagram& e)
        {
            sendAddProcessError(api, {timestamp, processState::ECAT_CONFIGURE_MAILBOXES_FAILED});
            sendAddBroadcastErroredFrame(api, {since_epoch(), ecatCommand::CONFIGURE_MAILBOXES, e.state()});
            check = false;
        }
        catch(const Error& e)
        {
            if (std::string(e.msg()) == "Datagram in error state") {sendAddProcessError(api, {timestamp, processState::ECAT_CONFIGURE_MAILBOXES_FAILED});}
            else { sendAddGeneralError(api, {timestamp, e.what()}); }
            check = false;
        }
        catch(const std::system_error& e)
        {
            if (e.code().value() == 110) { sendAddGeneralError(api, {timestamp, "Cannot read socket"}); }
            else { sendAddGeneralError(api, {timestamp, e.what()}); }
            sendAddProcessError(api, {timestamp, processState::ECAT_CONFIGURE_MAILBOXES_FAILED});
            check = false;
        }
        catch( ... )
        {
            sendAddGeneralError(api, {timestamp, "Unknown error"});
            sendAddProcessError(api, {timestamp, processState::ECAT_CONFIGURE_MAILBOXES_FAILED});
            check = false;
        }
        if (check) {sendAddProcessError(api, {timestamp, processState::ECAT_CONFIGURE_MAILBOXES_OK});}
        
        
        check = true;
        timestamp = since_epoch();
        try
        {
            requestState(State::PRE_OP);
        }
        catch(const Error& e)
        {
            if (std::string(e.msg()) == "Invalid working counter") //Function is used in different contexts
            {
                sendAddBroadcastErroredFrame(api, {timestamp, ecatCommand::REQUEST_ECAT_PREOP, DatagramState::INVALID_WKC});
            }
            sendAddProcessError(api, {timestamp, processState::ECAT_PREOP_FAILED}); 
            sendAddGeneralError(api, {timestamp, e.what()});
            check = false;
        }
        catch(const std::system_error& e)
        {
            if (e.code().value() == 110) { sendAddGeneralError(api, {timestamp, "Cannot read socket"}); }
            else { sendAddGeneralError(api, {timestamp, e.what()}); }
            sendAddProcessError(api, {timestamp, processState::ECAT_PREOP_FAILED});
            sendAddBroadcastErroredFrame(api, {timestamp, ecatCommand::REQUEST_ECAT_PREOP, DatagramState::LOST});
            check = false;
        }
        catch( ... )
        {
            sendAddGeneralError(api, {timestamp, "Unknown error"});
            sendAddProcessError(api, {timestamp, processState::ECAT_PREOP_FAILED});
            check = false;
        }
        

        timestamp = since_epoch();
        try
        {
            waitForState(State::PRE_OP, 3000ms);
        }
        catch (ErrorDatagram const& e)
        {
            sendAddBroadcastErroredFrame(api, {timestamp, ecatCommand::WAIT_ECAT_PREOP, e.state()});
            sendAddProcessError(api, {timestamp, processState::ECAT_PREOP_FAILED});
            check = false;
        }
        catch (ErrorCode const& e)
        {
            sendAddGeneralError(api, {timestamp, ALStatus_to_string(e.code())});
            sendAddProcessError(api, {timestamp, processState::ECAT_PREOP_FAILED});
            check = false;
        }
        catch(const Error& e)
        {
            if (std::string(e.msg()) == "Timeout") {sendAddProcessError(api, {timestamp, processState::ECAT_PREOP_TIMEOUT});}
            else if (std::string(e.msg()) == "Invalid working counter") {sendAddProcessError(api, {timestamp, processState::ECAT_PREOP_FAILED}); sendAddBroadcastErroredFrame(api, {timestamp, ecatCommand::WAIT_ECAT_PREOP, DatagramState::INVALID_WKC});}
            else if (std::string(e.msg()) == "State transition error") {sendAddProcessError(api, {timestamp, processState::ECAT_PREOP_INVALID_STATE_TRANSITION});}
            else {sendAddProcessError(api, {timestamp, processState::ECAT_PREOP_FAILED}); sendAddGeneralError(api, {timestamp, e.what()});}
            check = false;
        }
        catch(const std::system_error& e)
        {
            if (e.code().value() == 110) { sendAddGeneralError(api, {timestamp, "Cannot read socket"}); }
            else { sendAddGeneralError(api, {timestamp, e.what()}); }
            sendAddProcessError(api, {timestamp, processState::ECAT_PREOP_FAILED});
            sendAddBroadcastErroredFrame(api, {timestamp, ecatCommand::WAIT_ECAT_PREOP, DatagramState::LOST});
            check = false;
        }
        catch( ... )
        {
            sendAddGeneralError(api, {timestamp, "Unknown error"});
            sendAddProcessError(api, {timestamp, processState::ECAT_PREOP_FAILED});
            check = false;
        }
        for (auto& slave : slaves_)
        {
            api.slaveUpdateALStatus(slave.address, slave.al_status);
            api.slaveUpdateALStatusCode(slave.address, slave.al_status_code);
        }
        if (check) {sendAddProcessError(api, {timestamp, processState::ECAT_PREOP_OK});}
    }

    void wrapperGoPreOp_single(API& api, uint16_t const& slave_address)
    {
        bool check = true;
        nanoseconds timestamp = since_epoch();
        Slave* slave;
        try
        {
            slave = slaveAdressToSlave(slave_address);
        }
        catch(const Error& e)
        {
            sendAddGeneralError(api, {timestamp, e.what()});
            return;
        }

        try
        {
            fetchEeprom_single(*slave);
        }
        catch(const ErrorDatagram& e)
        {
            sendAddProcessError(api, {timestamp, processState::ECAT_FETCH_EEPROM_FAILED});
            sendAddIndividualErroredFrame(api,  slave_address, {timestamp, ecatCommand::READ_EEPROM, e.state()});
            check = false;
        }
        catch(const Error& e)
        {
            if (std::string(e.msg()) == "Timeout") {sendAddProcessError(api, {timestamp, processState::ECAT_FETCH_EEPROM_TIMEOUT});}
            else if (std::string(e.msg()) == "Invalid working counter") {sendAddProcessError(api, {timestamp, processState::ECAT_FETCH_EEPROM_FAILED});}
            else { sendAddGeneralError(api, {timestamp, e.what()}); }
            check = false;
        }
        catch(const std::system_error& e)
        {
            if (e.code().value() == 110) { sendAddGeneralError(api, {timestamp, "Cannot read socket"}); }
            else { sendAddGeneralError(api, {timestamp, e.what()}); }
            sendAddProcessError(api, {timestamp, processState::ECAT_FETCH_EEPROM_FAILED});
            check = false;
        }
        catch( ... )
        {
            sendAddGeneralError(api, {timestamp, "Unknown error"});
            sendAddProcessError(api, {timestamp, processState::ECAT_FETCH_EEPROM_FAILED});
            check = false;
        }
        if (check) {sendAddProcessError(api, {timestamp, processState::ECAT_FETCH_EEPROM_OK});}
        

        check = true;
        timestamp = since_epoch();
        try
        {
            configureMailboxes_single(*slave);
        }
        catch(const ErrorDatagram& e)
        {
            sendAddProcessError(api, {timestamp, processState::ECAT_CONFIGURE_MAILBOXES_FAILED});
            sendAddIndividualErroredFrame(api,  slave_address, {since_epoch(), ecatCommand::CONFIGURE_MAILBOXES, e.state()});
            check = false;
        }
        catch(const Error& e)
        {
            if (std::string(e.msg()) == "Datagram in error state") {sendAddProcessError(api, {timestamp, processState::ECAT_CONFIGURE_MAILBOXES_FAILED});}
            else { sendAddGeneralError(api, {timestamp, e.what()}); }
            check = false;
        }
        catch(const std::system_error& e)
        {
            if (e.code().value() == 110) { sendAddGeneralError(api, {timestamp, "Cannot read socket"}); }
            else { sendAddGeneralError(api, {timestamp, e.what()}); }
            sendAddProcessError(api, {timestamp, processState::ECAT_CONFIGURE_MAILBOXES_FAILED});
            check = false;
        }
        catch( ... )
        {
            sendAddGeneralError(api, {timestamp, "Unknown error"});
            sendAddProcessError(api, {timestamp, processState::ECAT_CONFIGURE_MAILBOXES_FAILED});
            check = false;
        }
        if (check) {sendAddProcessError(api, {timestamp, processState::ECAT_CONFIGURE_MAILBOXES_OK});}
        
        
        check = true;
        timestamp = since_epoch();
        try
        {
            requestState_single(*slave, State::PRE_OP);
        }
        catch(const Error& e)
        {
            if (std::string(e.msg()) == "Invalid working counter") //Function is used in different contexts
            {
                sendAddIndividualErroredFrame(api,  slave_address, {timestamp, ecatCommand::REQUEST_ECAT_PREOP, DatagramState::INVALID_WKC});
            }
            sendAddProcessError(api, {timestamp, processState::ECAT_PREOP_FAILED}); 
            sendAddGeneralError(api, {timestamp, e.what()});
            check = false;
        }
        catch(const std::system_error& e)
        {
            if (e.code().value() == 110) { sendAddGeneralError(api, {timestamp, "Cannot read socket"}); }
            else { sendAddGeneralError(api, {timestamp, e.what()}); }
            sendAddProcessError(api, {timestamp, processState::ECAT_PREOP_FAILED});
            sendAddIndividualErroredFrame(api,  slave_address, {timestamp, ecatCommand::REQUEST_ECAT_PREOP, DatagramState::LOST});
            check = false;
        }
        catch( ... )
        {
            sendAddGeneralError(api, {timestamp, "Unknown error"});
            sendAddProcessError(api, {timestamp, processState::ECAT_PREOP_FAILED});
            check = false;
        }
        

        timestamp = since_epoch();
        try
        {
            waitForState_single(*slave, State::PRE_OP, 3000ms);
        }
        catch (ErrorDatagram const& e)
        {
            sendAddIndividualErroredFrame(api,  slave_address, {timestamp, ecatCommand::WAIT_ECAT_PREOP, e.state()});
            sendAddProcessError(api, {timestamp, processState::ECAT_PREOP_FAILED});
            check = false;
        }
        catch (ErrorCode const& e)
        {
            sendAddGeneralError(api, {timestamp, ALStatus_to_string(e.code())});
            sendAddProcessError(api, {timestamp, processState::ECAT_PREOP_FAILED});
            check = false;
        }
        catch(const Error& e)
        {
            if (std::string(e.msg()) == "Timeout") {sendAddProcessError(api, {timestamp, processState::ECAT_PREOP_TIMEOUT});}
            else if (std::string(e.msg()) == "Invalid working counter") {sendAddProcessError(api, {timestamp, processState::ECAT_PREOP_FAILED}); sendAddBroadcastErroredFrame(api, {timestamp, ecatCommand::WAIT_ECAT_PREOP, DatagramState::INVALID_WKC});}
            else if (std::string(e.msg()) == "State transition error") {sendAddProcessError(api, {timestamp, processState::ECAT_PREOP_INVALID_STATE_TRANSITION});}
            else {sendAddProcessError(api, {timestamp, processState::ECAT_PREOP_FAILED}); sendAddGeneralError(api, {timestamp, e.what()});}
            check = false;
        }
        catch(const std::system_error& e)
        {
            if (e.code().value() == 110) { sendAddGeneralError(api, {timestamp, "Cannot read socket"}); }
            else { sendAddGeneralError(api, {timestamp, e.what()}); }
            sendAddProcessError(api, {timestamp, processState::ECAT_PREOP_FAILED});
            sendAddIndividualErroredFrame(api,  slave_address, {timestamp, ecatCommand::WAIT_ECAT_PREOP, DatagramState::LOST});
            check = false;
        }
        catch( ... )
        {
            sendAddGeneralError(api, {timestamp, "Unknown error"});
            sendAddProcessError(api, {timestamp, processState::ECAT_PREOP_FAILED});
            check = false;
        }

        api.slaveUpdateALStatus(slave_address, slave->al_status);
        api.slaveUpdateALStatusCode(slave_address, slave->al_status_code);
        if (check) {sendAddProcessError(api, {timestamp, processState::ECAT_PREOP_OK});}
        
    }

    void wrapperSetupIO(API& api)
    {
        bool check = true;
        nanoseconds timestamp = since_epoch();
        // clear mailboxes
        auto error_callback = [](DatagramState const& state){ THROW_ERROR_DATAGRAM("General Error", state); };
        try
        {
            checkMailboxes(error_callback);
            processMessages(error_callback);
        }
        catch(const ErrorDatagram& e)
        {
            sendAddBroadcastErroredFrame(api, {timestamp, ecatCommand::CHECK_MAILBOXES, e.state()});
            sendAddProcessError(api, {timestamp, processState::ECAT_IO_CHECKMAILBOXES_FAILED});
            check = false;
        }
        catch(const std::system_error& e)
        {
            if (e.code().value() == 110) { sendAddGeneralError(api, {timestamp, "Cannot read socket"}); }
            else { sendAddGeneralError(api, {timestamp, e.what()}); }
            sendAddBroadcastErroredFrame(api, {timestamp, ecatCommand::CHECK_MAILBOXES, DatagramState::LOST});
            check = false;
        }
        catch( ... )
        {
            sendAddGeneralError(api, {timestamp, "Unknown error"});
            sendAddProcessError(api, {timestamp, processState::ECAT_IO_CHECKMAILBOXES_FAILED});
            check = false;
        }
        

        timestamp = since_epoch();
        // create CoE emergency reception callback
        for (auto& slave : slaves_)
        {
            if (slave.supported_mailbox & eeprom::MailboxProtocol::CoE)
            {
                auto emg = std::make_shared<EmergencyMessage>(slave.mailbox);
                slave.mailbox.to_process.push_back(emg);
            }
        }

        timestamp = since_epoch();
        //Configure Mapping
        for (auto& slave : slaves_)
        {
            if (slavesConfig.at(slave.address).type == std::string("ElmoMotor"))
            {

                try
                {
                    uint32_t Rx_length = sizeof(hal::pdo::elmo::RxMapping);
                    writeSDO(slave, hal::sdo::elmo::RxPDO.index, hal::sdo::elmo::RxPDO.subindex, Bus::Access::COMPLETE, (void*) hal::pdo::elmo::RxMapping, Rx_length);
                }
                catch(const ErrorDatagram& e)
                {
                    sendAddIndividualErroredFrame(api,  slave.address, {timestamp, ecatCommand::RXMAPPING, e.state()});
                    sendAddProcessError(api, {timestamp, processState::ECAT_IO_RXMAPPING_FAILED});
                    check = false;
                }
                catch(const Error& e)
                {
                    sendAddIndividualErroredFrame(api,  slave.address, {timestamp, ecatCommand::RXMAPPING, DatagramState::LOST});
                    sendAddProcessError(api, {timestamp, processState::ECAT_IO_RXMAPPING_TIMEOUT});
                    check = false;
                }
                catch(const std::system_error& e)
                {
                    if (e.code().value() == 110) { sendAddGeneralError(api, {timestamp, "Cannot read socket"}); }
                    else { sendAddGeneralError(api, {timestamp, e.what()}); }
                    sendAddIndividualErroredFrame(api,  slave.address, {timestamp, ecatCommand::RXMAPPING, DatagramState::LOST});
                    check = false;
                }
                catch( ... )
                {
                    sendAddGeneralError(api, {timestamp, "Unknown error"});
                    sendAddProcessError(api, {timestamp, processState::ECAT_IO_RXMAPPING_FAILED});
                    check = false;
                }
                

                timestamp = since_epoch();
                try
                {
                    uint32_t Tx_length = sizeof(hal::pdo::elmo::TxMapping);
                    writeSDO(slave, hal::sdo::elmo::TxPDO.index, hal::sdo::elmo::TxPDO.subindex, Bus::Access::COMPLETE, (void*) hal::pdo::elmo::TxMapping, Tx_length);
                }
                catch(const ErrorDatagram& e)
                {
                    sendAddIndividualErroredFrame(api,  slave.address, {timestamp, ecatCommand::TXMAPPING, e.state()});
                    sendAddProcessError(api, {timestamp, processState::ECAT_IO_TXMAPPING_FAILED});
                    check = false;
                }
                catch(const Error& e)
                {
                    sendAddIndividualErroredFrame(api,  slave.address, {timestamp, ecatCommand::TXMAPPING, DatagramState::LOST});
                    sendAddProcessError(api, {timestamp, processState::ECAT_IO_TXMAPPING_TIMEOUT});
                    check = false;
                }
                catch(const std::system_error& e)
                {
                    if (e.code().value() == 110) { sendAddGeneralError(api, {timestamp, "Cannot read socket"}); }
                    else { sendAddGeneralError(api, {timestamp, e.what()}); }
                    sendAddIndividualErroredFrame(api,  slave.address, {timestamp, ecatCommand::TXMAPPING, DatagramState::LOST});
                    check = false;
                }
                catch( ... )
                {
                    sendAddGeneralError(api, {timestamp, "Unknown error"});
                    sendAddProcessError(api, {timestamp, processState::ECAT_IO_TXMAPPING_FAILED});
                    check = false;
                }
            }
        }
        
        timestamp = since_epoch();
        uint8_t io_buffer[2048];
        try
        {
            createMapping(io_buffer);
        }
        catch(const ErrorDatagram& e)
        {
            sendAddBroadcastErroredFrame(api, {timestamp, ecatCommand::IOMAPPING, e.state()});
            sendAddProcessError(api, {timestamp, processState::ECAT_IO_IOMAPPING_FAILED});
        }
        catch(const Error& e)
        {
            if (std::string(e.msg()) == "Error while reading SDO - client buffer too small")
            {
                sendAddBroadcastErroredFrame(api, {timestamp, ecatCommand::IOMAPPING, DatagramState::NO_HANDLER});
                sendAddProcessError(api, {timestamp, processState::ECAT_IO_IOMAPPING_BUFFERTOOSMALL});
            }
            else if (std::string(e.msg()) == "Error while reading SDO - emulated complete access")
            {
                sendAddProcessError(api, {timestamp, processState::ECAT_IO_IOMAPPING_ACCESS_ERROR});
            }
            else if (std::string(e.msg()) == "Error while reading SDO - Timeout")
            {
                sendAddBroadcastErroredFrame(api, {timestamp, ecatCommand::IOMAPPING, DatagramState::LOST});
                sendAddProcessError(api, {timestamp, processState::ECAT_IO_IOMAPPING_TIMEOUT});
            }
            else
            {
                sendAddProcessError(api, {timestamp, processState::ECAT_IO_IOMAPPING_FAILED});
            }
            check = false;
        }
        catch(const std::system_error& e)
        {
            if (e.code().value() == 110) { sendAddGeneralError(api, {timestamp, "Cannot read socket"}); }
            else { sendAddGeneralError(api, {timestamp, e.what()}); }
            sendAddBroadcastErroredFrame(api, {timestamp, ecatCommand::IOMAPPING, DatagramState::LOST});
            check = false;
        }
        catch( ... )
        {
            sendAddGeneralError(api, {timestamp, "Unknown error"});
            sendAddProcessError(api, {timestamp, processState::ECAT_IO_IOMAPPING_FAILED});
            check = false;
        }
        if (check) {sendAddProcessError(api, {timestamp, processState::ECAT_IO_OK});}
        
    }

    void wrapperSetupIO_single(API& api, uint16_t const& slave_address)
    {
        bool check = true;
        nanoseconds timestamp = since_epoch();
        Slave* slave;
        try
        {
            slave = slaveAdressToSlave(slave_address);
        }
        catch(const Error& e)
        {
            sendAddGeneralError(api, {timestamp, e.what()});
            return;
        }
        // clear mailboxes
        auto error_callback = [](DatagramState const& state){ THROW_ERROR_DATAGRAM("General Error", state); };
        try
        {
            checkMailboxes_single(*slave, error_callback);
            processMessages_single(*slave, error_callback);
        }
        catch(const ErrorDatagram& e)
        {
            sendAddIndividualErroredFrame(api,  slave_address, {timestamp, ecatCommand::CHECK_MAILBOXES, e.state()});
            sendAddProcessError(api, {timestamp, processState::ECAT_IO_CHECKMAILBOXES_FAILED});
            check = false;
        }
        catch(const std::system_error& e)
        {
            if (e.code().value() == 110) { sendAddGeneralError(api, {timestamp, "Cannot read socket"}); }
            else { sendAddGeneralError(api, {timestamp, e.what()}); }
            sendAddIndividualErroredFrame(api,  slave_address, {timestamp, ecatCommand::CHECK_MAILBOXES, DatagramState::LOST});
            check = false;
        }
        catch( ... )
        {
            sendAddGeneralError(api, {timestamp, "Unknown error"});
            sendAddProcessError(api, {timestamp, processState::ECAT_IO_CHECKMAILBOXES_FAILED});
            check = false;
        }

        config conf;
        try
        {
            conf = slavesConfig.at(slave_address);
        }
        catch( ... )
        {
            sendAddGeneralError(api, {timestamp, "Slave config uninitialized (missing step : detectSlaves)"});
        }
        if (not conf.mapping.empty())
        {
            if (conf.isStaticMapping())
            {
                slave->input.bsize = conf.getInputSize();
                slave->input.sync_manager = conf.getInputSM();
                slave->output.bsize = conf.getOutputSize();
                slave->output.sync_manager = conf.getOutputSM();
            }
        }

        //Elmo
        if (conf.type == std::string("ElmoMotor"))
        {
            try
            {
                uint32_t Rx_length = sizeof(hal::pdo::elmo::RxMapping);
                writeSDO(*slave, hal::sdo::elmo::RxPDO.index, hal::sdo::elmo::RxPDO.subindex, Bus::Access::COMPLETE, (void*) hal::pdo::elmo::RxMapping, Rx_length);
            }
            catch(const ErrorDatagram& e)
            {
                sendAddIndividualErroredFrame(api,  slave_address, {timestamp, ecatCommand::RXMAPPING, e.state()});
                sendAddProcessError(api, {timestamp, processState::ECAT_IO_RXMAPPING_FAILED});
                check = false;
            }
            catch(const Error& e)
            {
                sendAddIndividualErroredFrame(api,  slave_address, {timestamp, ecatCommand::RXMAPPING, DatagramState::LOST});
                sendAddProcessError(api, {timestamp, processState::ECAT_IO_RXMAPPING_TIMEOUT});
                check = false;
            }
            catch(const std::system_error& e)
            {
                if (e.code().value() == 110) { sendAddGeneralError(api, {timestamp, "Cannot read socket"}); }
                else { sendAddGeneralError(api, {timestamp, e.what()}); }
                sendAddIndividualErroredFrame(api,  slave_address, {timestamp, ecatCommand::RXMAPPING, DatagramState::LOST});
                check = false;
            }
            catch( ... )
            {
                sendAddGeneralError(api, {timestamp, "Unknown error"});
                sendAddProcessError(api, {timestamp, processState::ECAT_IO_RXMAPPING_FAILED});
                check = false;
            }
            

            timestamp = since_epoch();
            try
            {
                uint32_t Tx_length = sizeof(hal::pdo::elmo::TxMapping);
                writeSDO(*slave, hal::sdo::elmo::TxPDO.index, hal::sdo::elmo::TxPDO.subindex, Bus::Access::COMPLETE, (void*) hal::pdo::elmo::TxMapping, Tx_length);
            }
            catch(const ErrorDatagram& e)
            {
                sendAddIndividualErroredFrame(api,  slave_address, {timestamp, ecatCommand::TXMAPPING, e.state()});
                sendAddProcessError(api, {timestamp, processState::ECAT_IO_TXMAPPING_FAILED});
                check = false;
            }
            catch(const Error& e)
            {
                sendAddIndividualErroredFrame(api,  slave_address, {timestamp, ecatCommand::TXMAPPING, DatagramState::LOST});
                sendAddProcessError(api, {timestamp, processState::ECAT_IO_TXMAPPING_TIMEOUT});
                check = false;
            }
            catch(const std::system_error& e)
            {
                if (e.code().value() == 110) { sendAddGeneralError(api, {timestamp, "Cannot read socket"}); }
                else { sendAddGeneralError(api, {timestamp, e.what()}); }
                sendAddIndividualErroredFrame(api,  slave_address, {timestamp, ecatCommand::TXMAPPING, DatagramState::LOST});
                check = false;
            }
            catch( ... )
            {
                sendAddGeneralError(api, {timestamp, "Unknown error"});
                sendAddProcessError(api, {timestamp, processState::ECAT_IO_TXMAPPING_FAILED});
                check = false;
            }
        }
        
        uint8_t* iomap = memory_io_buffer;
        
        timestamp = since_epoch();
        // create CoE emergency reception callback
        if (slave->supported_mailbox & eeprom::MailboxProtocol::CoE)
        {
            auto emg = std::make_shared<EmergencyMessage>(slave->mailbox);
            slave->mailbox.to_process.push_back(emg);
        }
       
        timestamp = since_epoch();
        try
        {
            createMapping_single(slave, iomap, address);
        }
        catch(const ErrorDatagram& e)
        {
            sendAddIndividualErroredFrame(api,  slave_address, {timestamp, ecatCommand::IOMAPPING, e.state()});
            sendAddProcessError(api, {timestamp, processState::ECAT_IO_IOMAPPING_FAILED});
            check = false;
        }
        catch(const Error& e)
        {
            if (std::string(e.msg()) == "Error while reading SDO - client buffer too small")
            {
                sendAddIndividualErroredFrame(api,  slave_address, {timestamp, ecatCommand::IOMAPPING, DatagramState::NO_HANDLER});
                sendAddProcessError(api, {timestamp, processState::ECAT_IO_IOMAPPING_BUFFERTOOSMALL});
            }
            else if (std::string(e.msg()) == "Error while reading SDO - emulated complete access")
            {
                sendAddProcessError(api, {timestamp, processState::ECAT_IO_IOMAPPING_ACCESS_ERROR});
            }
            else if (std::string(e.what()) == "Error while reading SDO - Timeout")
            {
                sendAddIndividualErroredFrame(api,  slave_address, {timestamp, ecatCommand::IOMAPPING, DatagramState::LOST});
                sendAddProcessError(api, {timestamp, processState::ECAT_IO_IOMAPPING_TIMEOUT});
            }
            else
            {
                sendAddProcessError(api, {timestamp, processState::ECAT_IO_IOMAPPING_FAILED});
            }
            check = false;
        }
        catch(const std::system_error& e)
        {
            if (e.code().value() == 110) { sendAddGeneralError(api, {timestamp, "Cannot read socket"}); }
            else { sendAddGeneralError(api, {timestamp, e.what()}); }
            sendAddIndividualErroredFrame(api,  slave_address, {timestamp, ecatCommand::IOMAPPING, DatagramState::LOST});
            check = false;
        }
        catch( ... )
        {
            sendAddGeneralError(api, {timestamp, "Unknown error"});
            sendAddProcessError(api, {timestamp, processState::ECAT_IO_IOMAPPING_FAILED});
            check = false;
        }
        if (check) {sendAddProcessError(api, {timestamp, processState::ECAT_IO_OK});}
        
    }

    void wrapperGoSafeOp(API& api)
    {    
        bool check = true;
        nanoseconds timestamp = since_epoch();
        try
        {
            requestState(State::SAFE_OP);
        }
        catch(const Error& e)
        {
            if (std::string(e.msg()) == "Invalid working counter") //Function is used in different contexts
            {
                sendAddBroadcastErroredFrame(api, {timestamp, ecatCommand::REQUEST_ECAT_SAFEOP, DatagramState::INVALID_WKC});
            }
            sendAddProcessError(api, {timestamp, processState::ECAT_SAFEOP_FAILED});
            sendAddGeneralError(api, {timestamp, e.what()});
            check = false;
        }
        catch(const std::system_error& e)
        {
            if (e.code().value() == 110) { sendAddGeneralError(api, {timestamp, "Cannot read socket"}); }
            else { sendAddGeneralError(api, {timestamp, e.what()}); }
            sendAddProcessError(api, {timestamp, processState::ECAT_SAFEOP_FAILED});
            sendAddBroadcastErroredFrame(api, {timestamp, ecatCommand::REQUEST_ECAT_SAFEOP, DatagramState::LOST});
            check = false;
        }
        catch( ... )
        {
            sendAddGeneralError(api, {timestamp, "Unknown error"});
            sendAddProcessError(api, {timestamp, processState::ECAT_SAFEOP_FAILED});
            check = false;
        }
        

        timestamp = since_epoch();
        try
        {
            waitForState(State::SAFE_OP, 1s);
        }
        catch (ErrorDatagram const& e)
        {
            sendAddBroadcastErroredFrame(api, {timestamp, ecatCommand::WAIT_ECAT_SAFEOP, e.state()});
            sendAddProcessError(api, {timestamp, processState::ECAT_SAFEOP_FAILED});
            check = false;
        }
        catch (ErrorCode const& e)
        {
            sendAddGeneralError(api, {timestamp, ALStatus_to_string(e.code())});
            sendAddProcessError(api, {timestamp, processState::ECAT_SAFEOP_FAILED});
            check = false;
        }
        catch(const Error& e)
        {
            if (std::string(e.msg()) == "Timeout") {sendAddProcessError(api, {timestamp, processState::ECAT_SAFEOP_TIMEOUT});}
            else if (std::string(e.msg()) == "Invalid working counter") {sendAddProcessError(api, {timestamp, processState::ECAT_SAFEOP_FAILED}); sendAddBroadcastErroredFrame(api, {timestamp, ecatCommand::WAIT_ECAT_SAFEOP, DatagramState::INVALID_WKC});}
            else if (std::string(e.msg()) == "State transition error") {sendAddProcessError(api, {timestamp, processState::ECAT_SAFEOP_INVALID_STATE_TRANSITION});}
            else {sendAddProcessError(api, {timestamp, processState::ECAT_SAFEOP_FAILED}); sendAddGeneralError(api, {timestamp, e.what()});}
            check = false;
        }
        catch(const std::system_error& e)
        {
            if (e.code().value() == 110) { sendAddGeneralError(api, {timestamp, "Cannot read socket"}); }
            else { sendAddGeneralError(api, {timestamp, e.what()}); }
            sendAddProcessError(api, {timestamp, processState::ECAT_SAFEOP_FAILED});
            sendAddBroadcastErroredFrame(api, {timestamp, ecatCommand::WAIT_ECAT_SAFEOP, DatagramState::LOST});
            check = false;
        }
        catch( ... )
        {
            sendAddGeneralError(api, {timestamp, "Unknown error"});
            sendAddProcessError(api, {timestamp, processState::ECAT_SAFEOP_FAILED});
            check = false;
        }
        for (auto& slave : slaves_)
        {
            api.slaveUpdateALStatus(slave.address, slave.al_status);
            api.slaveUpdateALStatusCode(slave.address, slave.al_status_code);
        }
        if (check) {sendAddProcessError(api, {timestamp, processState::ECAT_SAFEOP_OK});}
        
    }

    void wrapperGoSafeOp_single(API& api, uint16_t const& slave_address)
    {    
        bool check = true;
        nanoseconds timestamp = since_epoch();
        Slave* slave;
        try
        {
            slave = slaveAdressToSlave(slave_address);
        }
        catch(const Error& e)
        {
            sendAddGeneralError(api, {timestamp, e.what()});
            return;
        }

        try
        {
            requestState_single(*slave, State::SAFE_OP);
        }
        catch(const Error& e)
        {
            if (std::string(e.msg()) == "Invalid working counter") //Function is used in different contexts
            {
                sendAddIndividualErroredFrame(api,  slave_address, {timestamp, ecatCommand::REQUEST_ECAT_SAFEOP, DatagramState::INVALID_WKC});
            }
            sendAddProcessError(api, {timestamp, processState::ECAT_SAFEOP_FAILED});
            sendAddGeneralError(api, {timestamp, e.what()});
            check = false;
        }
        catch(const std::system_error& e)
        {
            if (e.code().value() == 110) { sendAddGeneralError(api, {timestamp, "Cannot read socket"}); }
            else { sendAddGeneralError(api, {timestamp, e.what()}); }
            sendAddProcessError(api, {timestamp, processState::ECAT_SAFEOP_FAILED});
            sendAddIndividualErroredFrame(api,  slave_address, {timestamp, ecatCommand::REQUEST_ECAT_SAFEOP, DatagramState::LOST});
            check = false;
        }
        catch( ... )
        {
            sendAddGeneralError(api, {timestamp, "Unknown error"});
            sendAddProcessError(api, {timestamp, processState::ECAT_SAFEOP_FAILED});
            check = false;
        }
        

        timestamp = since_epoch();
        try
        {
            waitForState_single(*slave, State::SAFE_OP, 1s);
        }
        catch (ErrorDatagram const& e)
        {
            sendAddIndividualErroredFrame(api,  slave_address, {timestamp, ecatCommand::WAIT_ECAT_SAFEOP, e.state()});
            sendAddProcessError(api, {timestamp, processState::ECAT_SAFEOP_FAILED});
            check = false;
        }
        catch (ErrorCode const& e)
        {
            sendAddGeneralError(api, {timestamp, ALStatus_to_string(e.code())});
            sendAddProcessError(api, {timestamp, processState::ECAT_SAFEOP_FAILED});
            check = false;
        }
        catch(const Error& e)
        {
            if (std::string(e.msg()) == "Timeout") {sendAddProcessError(api, {timestamp, processState::ECAT_SAFEOP_TIMEOUT});}
            else if (std::string(e.msg()) == "Invalid working counter") {sendAddProcessError(api, {timestamp, processState::ECAT_SAFEOP_FAILED}); sendAddBroadcastErroredFrame(api, {timestamp, ecatCommand::WAIT_ECAT_SAFEOP, DatagramState::INVALID_WKC});}
            else if (std::string(e.msg()) == "State transition error") {sendAddProcessError(api, {timestamp, processState::ECAT_SAFEOP_INVALID_STATE_TRANSITION});}
            else {sendAddProcessError(api, {timestamp, processState::ECAT_SAFEOP_FAILED}); sendAddGeneralError(api, {timestamp, e.what()});}
            check = false;
        }
        catch(const std::system_error& e)
        {
            if (e.code().value() == 110) { sendAddGeneralError(api, {timestamp, "Cannot read socket"}); }
            else { sendAddGeneralError(api, {timestamp, e.what()}); }
            sendAddProcessError(api, {timestamp, processState::ECAT_SAFEOP_FAILED});
            sendAddIndividualErroredFrame(api,  slave_address, {timestamp, ecatCommand::WAIT_ECAT_SAFEOP, DatagramState::LOST});
            check = false;
        }
        catch( ... )
        {
            sendAddGeneralError(api, {timestamp, "Unknown error"});
            sendAddProcessError(api, {timestamp, processState::ECAT_SAFEOP_FAILED});
            check = false;
        }

        api.slaveUpdateALStatus(slave_address, slave->al_status);
        api.slaveUpdateALStatusCode(slave_address, slave->al_status_code);

        if (check) {sendAddProcessError(api, {timestamp, processState::ECAT_SAFEOP_OK});}
        
    }

    void wrapperGoOp(API& api)
    {    
        bool check = true;
        nanoseconds timestamp = since_epoch();
        try
        {
            processDataRead([](DatagramState const&){ THROW_ERROR("something bad happened"); });
            processDataWrite([](DatagramState const&){DEBUG_PRINT("(Previous line was a False alarm) \n");});
        }
        catch(const Error& e)
        {
            sendAddBroadcastErroredFrame(api, {timestamp, ecatCommand::VALIDATE_MAPPING, DatagramState::INVALID_WKC});
            sendAddProcessError(api, {timestamp, processState::ECAT_VALIDATE_MAPPING_FAILED});
            check = false;
        }
        catch(const std::system_error& e)
        {
            if (e.code().value() == 110) { sendAddGeneralError(api, {timestamp, "Cannot read socket"}); }
            else { sendAddGeneralError(api, {timestamp, e.what()}); }
            sendAddBroadcastErroredFrame(api, {timestamp, ecatCommand::VALIDATE_MAPPING, DatagramState::LOST});
            check = false;
        }
        catch( ... )
        {
            sendAddGeneralError(api, {timestamp, "Unknown error"});
            sendAddProcessError(api, {timestamp, processState::ECAT_VALIDATE_MAPPING_FAILED});
            check = false;
        }
        
        
        timestamp = since_epoch();
        try
        {
            requestState(State::OPERATIONAL);
        }
        catch(const Error& e)
        {
            if (std::string(e.msg()) == "Invalid working counter") //Function is used in different contexts
            {
                sendAddBroadcastErroredFrame(api, {timestamp, ecatCommand::REQUEST_ECAT_OP, DatagramState::INVALID_WKC});
            }
            sendAddProcessError(api, {timestamp, processState::ECAT_OP_FAILED});
            sendAddGeneralError(api, {timestamp, e.what()});
            check = false;
        }
        catch(const std::system_error& e)
        {
            if (e.code().value() == 110) { sendAddGeneralError(api, {timestamp, "Cannot read socket"}); }
            else { sendAddGeneralError(api, {timestamp, e.what()}); }
            sendAddProcessError(api, {timestamp, processState::ECAT_OP_FAILED});
            sendAddBroadcastErroredFrame(api, {timestamp, ecatCommand::REQUEST_ECAT_OP, DatagramState::LOST});
            check = false;
        }
        catch( ... )
        {
            sendAddGeneralError(api, {timestamp, "Unknown error"});
            sendAddProcessError(api, {timestamp, processState::ECAT_OP_FAILED});
            check = false;
        }
        

        timestamp = since_epoch();
        try
        {
            waitForState(State::OPERATIONAL, 100ms);
        }
        catch (ErrorDatagram const& e)
        {
            sendAddBroadcastErroredFrame(api, {timestamp, ecatCommand::WAIT_ECAT_OP, e.state()});
            sendAddProcessError(api, {timestamp, processState::ECAT_OP_FAILED});
            check = false;
        }
        catch (ErrorCode const& e)
        {
            sendAddGeneralError(api, {timestamp, ALStatus_to_string(e.code())});
            sendAddProcessError(api, {timestamp, processState::ECAT_OP_FAILED});
            check = false;
        }
        catch(const Error& e)
        {
            if (std::string(e.msg()) == "Timeout") {sendAddProcessError(api, {timestamp, processState::ECAT_OP_TIMEOUT});}
            else if (std::string(e.msg()) == "Invalid working counter") {sendAddProcessError(api, {timestamp, processState::ECAT_OP_FAILED}); sendAddBroadcastErroredFrame(api, {timestamp, ecatCommand::WAIT_ECAT_OP, DatagramState::INVALID_WKC});}
            else if (std::string(e.msg()) == "State transition error") {sendAddProcessError(api, {timestamp, processState::ECAT_OP_INVALID_STATE_TRANSITION});}
            else {sendAddProcessError(api, {timestamp, processState::ECAT_OP_FAILED}); sendAddGeneralError(api, {timestamp, e.what()});}
            check = false; 
        }
        catch(const std::system_error& e)
        {
            if (e.code().value() == 110) { sendAddGeneralError(api, {timestamp, "Cannot read socket"}); }
            else { sendAddGeneralError(api, {timestamp, e.what()}); }
            sendAddProcessError(api, {timestamp, processState::ECAT_OP_FAILED});
            sendAddBroadcastErroredFrame(api, {timestamp, ecatCommand::WAIT_ECAT_OP, DatagramState::LOST});
            check = false;
        }
        catch( ... )
        {
            sendAddGeneralError(api, {timestamp, "Unknown error"});
            sendAddProcessError(api, {timestamp, processState::ECAT_OP_FAILED});
            check = false;
        }
        for (auto& slave : slaves_)
        {
            api.slaveUpdateALStatus(slave.address, slave.al_status);
            api.slaveUpdateALStatusCode(slave.address, slave.al_status_code);
        }
        if (check) {sendAddProcessError(api, {timestamp, processState::ECAT_OP_OK});}
        
    }

    void wrapperGoOp_single(API& api, uint16_t const& slave_address)
    {    
        bool check = true;
        nanoseconds timestamp = since_epoch();
        Slave* slave;
        try
        {
            slave = slaveAdressToSlave(slave_address);
        }
        catch(const Error& e)
        {
            sendAddGeneralError(api, {timestamp, e.what()});
            return;
        }

        try
        {
            sendLogicalRead_single(*slave, [](DatagramState const& state){ THROW_ERROR_DATAGRAM("SendLogicalRead failed", state); });
            sendLogicalWrite_single(*slave, [](DatagramState const&){DEBUG_PRINT("(Previous line was a False alarm) \n");});
            finalizeDatagrams();
        }
        catch(const ErrorDatagram& e)
        {
            sendAddIndividualErroredFrame(api, slave_address, {timestamp, ecatCommand::VALIDATE_MAPPING, e.state()});
            sendAddProcessError(api, {timestamp, processState::ECAT_VALIDATE_MAPPING_FAILED});
            check = false;
        }
        catch(const Error& e)
        {
            sendAddIndividualErroredFrame(api,  slave_address, {timestamp, ecatCommand::VALIDATE_MAPPING, DatagramState::INVALID_WKC});
            sendAddProcessError(api, {timestamp, processState::ECAT_VALIDATE_MAPPING_FAILED});
            check = false;
        }
        catch(const std::system_error& e)
        {
            if (e.code().value() == 110) { sendAddGeneralError(api, {timestamp, "Cannot read socket"}); }
            else { sendAddGeneralError(api, {timestamp, e.what()}); }
            sendAddIndividualErroredFrame(api,  slave_address, {timestamp, ecatCommand::VALIDATE_MAPPING, DatagramState::LOST});
            check = false;
        }
        catch( ... )
        {
            sendAddGeneralError(api, {timestamp, "Unknown error"});
            sendAddProcessError(api, {timestamp, processState::ECAT_VALIDATE_MAPPING_FAILED});
            check = false;
        }
        
        
        timestamp = since_epoch();
        try
        {
            requestState_single(*slave, State::OPERATIONAL);
        }
        catch(const Error& e)
        {
            if (std::string(e.msg()) == "Invalid working counter") //Function is used in different contexts
            {
                sendAddIndividualErroredFrame(api,  slave_address, {timestamp, ecatCommand::REQUEST_ECAT_OP, DatagramState::INVALID_WKC});
            }
            sendAddProcessError(api, {timestamp, processState::ECAT_OP_FAILED});
            sendAddGeneralError(api, {timestamp, e.what()});
            check = false;
        }
        catch(const std::system_error& e)
        {
            if (e.code().value() == 110) { sendAddGeneralError(api, {timestamp, "Cannot read socket"}); }
            else { sendAddGeneralError(api, {timestamp, e.what()}); }
            sendAddProcessError(api, {timestamp, processState::ECAT_OP_FAILED});
            sendAddIndividualErroredFrame(api,  slave_address, {timestamp, ecatCommand::REQUEST_ECAT_OP, DatagramState::LOST});
            check = false;
        }
        catch( ... )
        {
            sendAddGeneralError(api, {timestamp, "Unknown error"});
            sendAddProcessError(api, {timestamp, processState::ECAT_OP_FAILED});
            check = false;
        }
        

        timestamp = since_epoch();
        try
        {
            waitForState_single(*slave, State::OPERATIONAL, 100ms);
        }
        catch (ErrorDatagram const& e)
        {
            sendAddIndividualErroredFrame(api,  slave_address, {timestamp, ecatCommand::WAIT_ECAT_OP, e.state()});
            sendAddProcessError(api, {timestamp, processState::ECAT_OP_FAILED});
            check = false;
        }
        catch (ErrorCode const& e)
        {
            sendAddGeneralError(api, {timestamp, ALStatus_to_string(e.code())});
            sendAddProcessError(api, {timestamp, processState::ECAT_OP_FAILED});
            check = false;
        }
        catch(const Error& e)
        {
            if (std::string(e.msg()) == "Timeout") {sendAddProcessError(api, {timestamp, processState::ECAT_OP_TIMEOUT});}
            else if (std::string(e.msg()) == "Invalid working counter") {sendAddProcessError(api, {timestamp, processState::ECAT_OP_FAILED}); sendAddBroadcastErroredFrame(api, {timestamp, ecatCommand::WAIT_ECAT_OP, DatagramState::INVALID_WKC});}
            else if (std::string(e.msg()) == "State transition error") {sendAddProcessError(api, {timestamp, processState::ECAT_OP_INVALID_STATE_TRANSITION});}
            else {sendAddProcessError(api, {timestamp, processState::ECAT_OP_FAILED}); sendAddGeneralError(api, {timestamp, e.what()});}
            check = false; 
        }
        catch(const std::system_error& e)
        {
            if (e.code().value() == 110) { sendAddGeneralError(api, {timestamp, "Cannot read socket"}); }
            else { sendAddGeneralError(api, {timestamp, e.what()}); }
            sendAddProcessError(api, {timestamp, processState::ECAT_OP_FAILED});
            sendAddIndividualErroredFrame(api,  slave_address, {timestamp, ecatCommand::WAIT_ECAT_OP, DatagramState::LOST});
            check = false;
        }
        catch( ... )
        {
            sendAddGeneralError(api, {timestamp, "Unknown error"});
            sendAddProcessError(api, {timestamp, processState::ECAT_OP_FAILED});
            check = false;
        }
        api.slaveUpdateALStatus(slave_address, slave->al_status);
        api.slaveUpdateALStatusCode(slave_address, slave->al_status_code);
        if (check) {sendAddProcessError(api, {timestamp, processState::ECAT_OP_OK});}
        
    }

    void wrapperCANTurnAllOn(API& api)
    {
        for (auto& slave : slaves_)
        {
            slave.CANstateMachine.setCommand(can::CANOpenCommand::ENABLE);
        }
        nanoseconds startTime = since_epoch();
        bool allOn = true;
        do
        {
            sleep(1ms);
            nanoseconds timestamp = since_epoch();
            try
            {
                sendLogicalRead([](DatagramState const& datagramStatus) {THROW_ERROR_DATAGRAM("Error", datagramStatus);});
                finalizeDatagrams();
                processAwaitingFrames();
            }
            catch(const ErrorDatagram& e)
            {
                sendAddBroadcastErroredFrame(api, {timestamp, ecatCommand::SEND_LOGICAL_READ, e.state()});
            }
            catch(const std::system_error& e)
            {
                if (e.code().value() == 110) { sendAddGeneralError(api, {timestamp, "Cannot read socket"}); }
                else { sendAddGeneralError(api, {timestamp, e.what()}); }
                sendAddBroadcastErroredFrame(api, {timestamp, ecatCommand::SEND_LOGICAL_READ, DatagramState::LOST});
            }
            catch( ... )
            {
                sendAddGeneralError(api, {timestamp, "Unknown error"});
            }
            

            allOn = true;

            for (auto& slave : slaves_)
            {
                hal::pdo::elmo::Input* inputPDO {nullptr};
                inputPDO = reinterpret_cast<hal::pdo::elmo::Input*>(slave.input.data);
                hal::pdo::elmo::Output* outputPDO {nullptr};
                outputPDO = reinterpret_cast<hal::pdo::elmo::Output*>(slave.output.data);

                slave.CANstateMachine.statusWord_ = inputPDO->statusWord;
                api.slaveUpdateCANState(slave.address, slave.CANstateMachine.statusWord_);
                slave.CANstateMachine.update();

                outputPDO->controlWord = slave.CANstateMachine.controlWord_;

                allOn = allOn & slave.CANstateMachine.isON();
            }

            timestamp = since_epoch();
            try
            {
                sendLogicalWrite([](DatagramState const& datagramStatus) {THROW_ERROR_DATAGRAM("Error", datagramStatus);});
            }
            catch(const ErrorDatagram& e)
            {
                sendAddBroadcastErroredFrame(api, {timestamp, ecatCommand::SEND_LOGICAL_WRITE, e.state()});
            }
            catch(const std::system_error& e)
            {
                if (e.code().value() == 110) { sendAddGeneralError(api, {timestamp, "Cannot read socket"}); }
                else { sendAddGeneralError(api, {timestamp, e.what()}); }
                sendAddBroadcastErroredFrame(api, {timestamp, ecatCommand::SEND_LOGICAL_WRITE, DatagramState::LOST});
            }
            catch( ... )
            {
                sendAddGeneralError(api, {timestamp, "Unknown error"});
            }
            

        }
        while((not allOn) && (elapsed_time(startTime) < 5s));

        if (not allOn)
        {
            sendAddProcessError(api, {since_epoch(), processState::CAN_TURNALLON_TIMEOUT});
        }
        else
        {
            sendAddProcessError(api, {since_epoch(), processState::CAN_TURNALLON_OK});
        } 
    }

    void wrapperCANTurnAllOff(API& api)
    {
        for (auto& slave : slaves_)
        {
            slave.CANstateMachine.setCommand(can::CANOpenCommand::DISABLE);
        }
        nanoseconds startTime = since_epoch();
        bool allOff = true;
        do
        {
            sleep(1ms);
            nanoseconds timestamp = since_epoch();
            try
            {
                sendLogicalRead([](DatagramState const& datagramStatus) {THROW_ERROR_DATAGRAM("Error", datagramStatus);});
                finalizeDatagrams();
                processAwaitingFrames();
            }
            catch(const ErrorDatagram& e)
            {
                sendAddBroadcastErroredFrame(api, {timestamp, ecatCommand::SEND_LOGICAL_READ, e.state()});
            }
            catch(const std::system_error& e)
            {
                if (e.code().value() == 110) { sendAddGeneralError(api, {timestamp, "Cannot read socket"}); }
                else { sendAddGeneralError(api, {timestamp, e.what()}); }
                sendAddBroadcastErroredFrame(api, {timestamp, ecatCommand::SEND_LOGICAL_READ, DatagramState::LOST});
            }
            catch( ... )
            {
                sendAddGeneralError(api, {timestamp, "Unknown error"});
            }
            

            allOff = true;

            for (auto& slave : slaves_)
            {
                hal::pdo::elmo::Input* inputPDO {nullptr};
                inputPDO = reinterpret_cast<hal::pdo::elmo::Input*>(slave.input.data);
                hal::pdo::elmo::Output* outputPDO {nullptr};
                outputPDO = reinterpret_cast<hal::pdo::elmo::Output*>(slave.output.data);

                slave.CANstateMachine.statusWord_ = inputPDO->statusWord;
                api.slaveUpdateCANState(slave.address, slave.CANstateMachine.statusWord_);
                slave.CANstateMachine.update();

                outputPDO->controlWord = slave.CANstateMachine.controlWord_;

                allOff = allOff & (not slave.CANstateMachine.isON());
            }

            timestamp = since_epoch();
            try
            {
                sendLogicalWrite([](DatagramState const& datagramStatus) {THROW_ERROR_DATAGRAM("Error", datagramStatus);});
            }
            catch(const ErrorDatagram& e)
            {
                sendAddBroadcastErroredFrame(api, {timestamp, ecatCommand::SEND_LOGICAL_WRITE, e.state()});
            }
            catch(const std::system_error& e)
            {
                if (e.code().value() == 110) { sendAddGeneralError(api, {timestamp, "Cannot read socket"}); }
                else { sendAddGeneralError(api, {timestamp, e.what()}); }
                sendAddBroadcastErroredFrame(api, {timestamp, ecatCommand::SEND_LOGICAL_WRITE, DatagramState::LOST});
            }
            catch( ... )
            {
                sendAddGeneralError(api, {timestamp, "Unknown error"});
            }
            

        }
        while((not allOff) && (elapsed_time(startTime) < 5s));

        if (not allOff)
        {
            sendAddProcessError(api, {since_epoch(), processState::CAN_TURNALLOFF_TIMEOUT});
        }
        else
        {
            sendAddProcessError(api, {since_epoch(), processState::CAN_TURNALLOFF_OK});
        }
        
    }

    void wrapperUnitRoutine(API& api)
    {
        nanoseconds timestamp = since_epoch();
        try
        {
            sendLogicalRead([](DatagramState const& datagramStatus) {THROW_ERROR_DATAGRAM("LogicalRead", datagramStatus);});
            timestamp = since_epoch();
            sendLogicalWrite([](DatagramState const& datagramStatus) {THROW_ERROR_DATAGRAM("LogicalWrite", datagramStatus);});
            timestamp = since_epoch();
            sendRefreshErrorCounters([](DatagramState const& datagramStatus) {THROW_ERROR_DATAGRAM("ErrorCounters", datagramStatus);});
            timestamp = since_epoch();
            sendMailboxesReadChecks([](DatagramState const& datagramStatus) {THROW_ERROR_DATAGRAM("MailboxesReadCheck", datagramStatus);});
            timestamp = since_epoch();
            sendReadMessages([](DatagramState const& datagramStatus) {THROW_ERROR_DATAGRAM("MailboxesRead", datagramStatus);});

            timestamp = since_epoch();
            finalizeDatagrams();
            processAwaitingFrames();
        }
        catch(const ErrorDatagram& e)
        {
            if (std::string(e.what()) == "LogicalRead") { sendAddBroadcastErroredFrame(api, {timestamp, ecatCommand::ROUTINE_LOGICALREAD, e.state()}); }
            else if (std::string(e.what()) == "LogicalWrite") { sendAddBroadcastErroredFrame(api, {timestamp, ecatCommand::ROUTINE_LOGICALWRITE, e.state()}); }
            else if (std::string(e.what()) == "ErrorCounters") { sendAddBroadcastErroredFrame(api, {timestamp, ecatCommand::ROUTINE_ERRORCOUNTERS, e.state()}); }
            else if (std::string(e.what()) == "MailboxesReadCheck") { sendAddBroadcastErroredFrame(api, {timestamp, ecatCommand::ROUTINE_MAILREADCHECK, e.state()}); }
            else if (std::string(e.what()) == "MailboxesRead") { sendAddBroadcastErroredFrame(api, {timestamp, ecatCommand::ROUTINE_MAILREAD, e.state()}); }
        }
        catch(const std::system_error& e)
        {
            if (e.code().value() == 110) { sendAddGeneralError(api, {timestamp, "Cannot read socket"}); }
            else { sendAddGeneralError(api, {timestamp, e.what()}); }
            sendAddBroadcastErroredFrame(api, {timestamp, ecatCommand::ROUTINE_GENERAL, DatagramState::LOST});
        }
        catch( ... )
        {
            sendAddGeneralError(api, {timestamp, "Unknown error"});
        }
        for (auto& slave : slaves_)
        {
            api.slaveUpdateErrorCounters(slave.address, slave.error_counters);
        }
        

        timestamp = since_epoch();
        for (auto& slave : slaves_)
        {
            for (auto& em : slave.mailbox.emergencies) { api.slaveAddEmergency(slave.address, {timestamp, em.error_code}); }
            if (slave.mailbox.emergencies.size() > 0) {slave.mailbox.emergencies.resize(0);}
        }
        
    }

    void wrapperCANAllStepInit(API& api)
    {
        std::vector<uint16_t> base_states;
        for (auto& slave : slaves_)
        {
            slave.CANstateMachine.setCommand(can::CANOpenCommand::ENABLE);
            base_states.push_back(slave.CANstateMachine.statusWord_);
        };
        bool allChanged = true;
        nanoseconds startTime = since_epoch();
        do
        {
            sleep(1ms);
            nanoseconds timestamp = since_epoch();
            try
            {
                sendLogicalRead([](DatagramState const& datagramStatus) {THROW_ERROR_DATAGRAM("Error", datagramStatus);});
                finalizeDatagrams();
                processAwaitingFrames();
            }
            catch(const ErrorDatagram& e)
            {
                sendAddBroadcastErroredFrame(api, {timestamp, ecatCommand::SEND_LOGICAL_READ, e.state()});
            }
            catch(const std::system_error& e)
            {
                if (e.code().value() == 110) { sendAddGeneralError(api, {timestamp, "Cannot read socket"}); }
                else { sendAddGeneralError(api, {timestamp, e.what()}); }
                sendAddBroadcastErroredFrame(api, {timestamp, ecatCommand::SEND_LOGICAL_READ, DatagramState::LOST});
            }
            catch( ... )
            {
                sendAddGeneralError(api, {timestamp, "Unknown error"});
            }
            
            allChanged = true;
            for (size_t i = 0; i < slaves_.size(); ++i)
            {
                Slave slave = slaves_.at(i);

                hal::pdo::elmo::Input* inputPDO {nullptr};
                inputPDO = reinterpret_cast<hal::pdo::elmo::Input*>(slave.input.data);
                hal::pdo::elmo::Output* outputPDO {nullptr};
                outputPDO = reinterpret_cast<hal::pdo::elmo::Output*>(slave.output.data);

                slave.CANstateMachine.statusWord_ = inputPDO->statusWord;
                api.slaveUpdateCANState(slave.address, slave.CANstateMachine.statusWord_);
                if (base_states.at(i) == slave.CANstateMachine.statusWord_)
                {
                    slave.CANstateMachine.update();
                }
                allChanged = allChanged & (base_states.at(i) == slave.CANstateMachine.statusWord_);

                outputPDO->controlWord = slave.CANstateMachine.controlWord_;
            }

            timestamp = since_epoch();
            try
            {
                sendLogicalWrite([](DatagramState const& datagramStatus) {THROW_ERROR_DATAGRAM("Error", datagramStatus);});
            }
            catch(const ErrorDatagram& e)
            {
                sendAddBroadcastErroredFrame(api, {timestamp, ecatCommand::SEND_LOGICAL_WRITE, e.state()});
            }
            catch(const std::system_error& e)
            {
                if (e.code().value() == 110) { sendAddGeneralError(api, {timestamp, "Cannot read socket"}); }
                else { sendAddGeneralError(api, {timestamp, e.what()}); }
                sendAddBroadcastErroredFrame(api, {timestamp, ecatCommand::SEND_LOGICAL_WRITE, DatagramState::LOST});
            }
            catch( ... )
            {
                sendAddGeneralError(api, {timestamp, "Unknown error"});
            }
            

        }
        while((not allChanged) && (elapsed_time(startTime) < 2s));

        if (not allChanged)
        {
            sendAddProcessError(api, {since_epoch(), processState::CAN_STEP_INIT_TIMEOUT});
        }
        else
        {
            sendAddProcessError(api, {since_epoch(), processState::CAN_STEP_INIT_OK});
        } 
    }

    void wrapperSendGetDLStatus(API& api)
    {
        nanoseconds timestamp = since_epoch();

        try
        {
            for (auto& slave : slaves_)
            {
                sendGetDLStatus(slave);
            }
        }
        catch (ErrorDatagram const& e)
        {
            sendAddBroadcastErroredFrame(api, {timestamp, ecatCommand::SEND_GET_DLSTATUS, e.state()});
        }
        catch(const Error& e)
        {
            sendAddGeneralError(api, {timestamp, e.what()});
        }
        catch(const std::system_error& e)
        {
            if (e.code().value() == 110) { sendAddGeneralError(api, {timestamp, "Cannot read socket"}); }
            else { sendAddGeneralError(api, {timestamp, e.what()}); }
            sendAddBroadcastErroredFrame(api, {timestamp, ecatCommand::SEND_GET_DLSTATUS, DatagramState::LOST});
        }
        catch( ... )
        {
            sendAddGeneralError(api, {timestamp, "Unknown error"});
        }

        for (auto& slave : slaves_)
        {
            api.slaveUpdateDLStatus(slave.address, slave.dl_status);
        }
    }

    void wrapperSendGetDLStatus_single(API& api, uint16_t const& slave_address)
    {
        nanoseconds timestamp = since_epoch();
        Slave* slave;
        try
        {
            slave = slaveAdressToSlave(slave_address);
        }
        catch(const Error& e)
        {
            sendAddGeneralError(api, {timestamp, e.what()});
            return;
        }

        try
        {
            sendGetDLStatus(*slave);
        }
        catch (ErrorDatagram const& e)
        {
            sendAddIndividualErroredFrame(api,  slave_address, {timestamp, ecatCommand::SEND_GET_DLSTATUS, e.state()});
        }
        catch(const Error& e)
        {
            sendAddGeneralError(api, {timestamp, e.what()});
        }
        catch(const std::system_error& e)
        {
            if (e.code().value() == 110) { sendAddGeneralError(api, {timestamp, "Cannot read socket"}); }
            else { sendAddGeneralError(api, {timestamp, e.what()}); }
            sendAddIndividualErroredFrame(api,  slave_address, {timestamp, ecatCommand::SEND_GET_DLSTATUS, DatagramState::LOST});
        }
        catch( ... )
        {
            sendAddGeneralError(api, {timestamp, "Unknown error"});
        }

        api.slaveUpdateDLStatus(slave_address, slave->dl_status);
    }

    void wrapperGetCurrentState(API& api)
    {
        nanoseconds timestamp = since_epoch();
        try
        {
            for (auto& slave : slaves_)
            {
                sendGetALStatus(slave, [](const DatagramState& state) {THROW_ERROR_DATAGRAM("AL Status", state);});
            }
        }
        catch (ErrorDatagram const& e)
        {
            sendAddBroadcastErroredFrame(api, {timestamp, ecatCommand::GET_CURRENT_STATE, e.state()});
        }
        catch(const Error& e)
        {
            sendAddGeneralError(api, {timestamp, e.what()});
        }
        catch(const std::system_error& e)
        {
            if (e.code().value() == 110) { sendAddGeneralError(api, {timestamp, "Cannot read socket"}); }
            else { sendAddGeneralError(api, {timestamp, e.what()}); }
            sendAddBroadcastErroredFrame(api, {timestamp, ecatCommand::GET_CURRENT_STATE, DatagramState::LOST});
        }
        catch( ... )
        {
            sendAddGeneralError(api, {timestamp, "Unknown error"});
        }

        for (auto& slave : slaves_)
        {
            api.slaveUpdateALStatus(slave.address, slave.al_status);
            api.slaveUpdateALStatusCode(slave.address, slave.al_status_code);
        }
    }

    void wrapperGetCurrentState_single(API& api, uint16_t const& slave_address)
    {
        nanoseconds timestamp = since_epoch();
        Slave* slave;
        try
        {
            slave = slaveAdressToSlave(slave_address);
        }
        catch(const Error& e)
        {
            sendAddGeneralError(api, {timestamp, e.what()});
            return;
        }

        try
        {
            sendGetALStatus(*slave, [](const DatagramState& state) {THROW_ERROR_DATAGRAM("AL Status", state);});
        }
        catch (ErrorDatagram const& e)
        {
            sendAddIndividualErroredFrame(api,  slave_address, {timestamp, ecatCommand::GET_CURRENT_STATE, e.state()});
        }
        catch(const Error& e)
        {
            sendAddGeneralError(api, {timestamp, e.what()});
        }
        catch(const std::system_error& e)
        {
            if (e.code().value() == 110) { sendAddGeneralError(api, {timestamp, "Cannot read socket"}); }
            else { sendAddGeneralError(api, {timestamp, e.what()}); }
            sendAddIndividualErroredFrame(api,  slave_address, {timestamp, ecatCommand::GET_CURRENT_STATE, DatagramState::LOST});
        }
        catch( ... )
        {
            sendAddGeneralError(api, {timestamp, "Unknown error"});
        }
        api.slaveUpdateALStatus(slave_address, slave->al_status);
        api.slaveUpdateALStatusCode(slave_address, slave->al_status_code);
    }

    void wrapperRefreshErrorCounters(API& api)
    {
        nanoseconds timestamp = since_epoch();
        try
        {
            sendRefreshErrorCounters([](const DatagramState& state) {THROW_ERROR_DATAGRAM("AL Status", state);});
        }
        catch (ErrorDatagram const& e)
        {
            sendAddBroadcastErroredFrame(api, {timestamp, ecatCommand::REFRESH_ERRORCOUNTERS, e.state()});
        }
        catch(const Error& e)
        {
            sendAddGeneralError(api, {timestamp, e.what()});
        }
        catch(const std::system_error& e)
        {
            if (e.code().value() == 110) { sendAddGeneralError(api, {timestamp, "Cannot read socket"}); }
            else { sendAddGeneralError(api, {timestamp, e.what()}); }
            sendAddBroadcastErroredFrame(api, {timestamp, ecatCommand::REFRESH_ERRORCOUNTERS, DatagramState::LOST});
        }
        catch( ... )
        {
            sendAddGeneralError(api, {timestamp, "Unknown error"});
        }

        for (auto& slave : slaves_)
        {
            api.slaveUpdateErrorCounters(slave.address, slave.errorCounters());
        }
    }

    void wrapperRefreshErrorCounters_single(API& api, uint16_t const& slave_address)
    {
        nanoseconds timestamp = since_epoch();
        Slave* slave;
        try
        {
            slave = slaveAdressToSlave(slave_address);
        }
        catch(const Error& e)
        {
            sendAddGeneralError(api, {timestamp, e.what()});
            return;
        }

        try
        {
            sendRefreshErrorCounters_single(*slave, [](const DatagramState& state) {THROW_ERROR_DATAGRAM("AL Status", state);});
        }
        catch (ErrorDatagram const& e)
        {
            sendAddIndividualErroredFrame(api,  slave_address, {timestamp, ecatCommand::GET_CURRENT_STATE, e.state()});
        }
        catch(const Error& e)
        {
            sendAddGeneralError(api, {timestamp, e.what()});
        }
        catch(const std::system_error& e)
        {
            if (e.code().value() == 110) { sendAddGeneralError(api, {timestamp, "Cannot read socket"}); }
            else { sendAddGeneralError(api, {timestamp, e.what()}); }
            sendAddIndividualErroredFrame(api,  slave_address, {timestamp, ecatCommand::GET_CURRENT_STATE, DatagramState::LOST});
        }
        catch( ... )
        {
            sendAddGeneralError(api, {timestamp, "Unknown error"});
        }

        api.slaveUpdateErrorCounters(slave_address, slave->errorCounters());
    }

    //Utils
    std::unordered_map<uint16_t, uint16_t> expectedTopology(std::string map_string)
    {
        std::unordered_map<uint16_t, uint16_t> expected_topology;
        int i = 0;
        while ((map_string.length() > 0) & (i < 10000))
        {
            std::string t = map_string.substr(0, map_string.find("/"));

            expected_topology.emplace(std::stoi(t.substr(0, t.find(":")), 0, 10), std::stoi(t.substr(t.find(":")+1), 0, 10));

            map_string = map_string.substr(map_string.find("/")+1);
            ++i;
        }
        return expected_topology;
    }

    void injectSingleMapping(uint16_t const& slave_address, std::string& map_string)
    {
        slavesConfig.at(slave_address).importMapping(map_string);
    }

    void injectSingleType(uint16_t const& slave_address, std::string& type_string)
    {
        slavesConfig.at(slave_address).importType(type_string);
    }

    void injectSingleName(uint16_t const& slave_address, std::string& name_string)
    {
        slavesConfig.at(slave_address).importName(name_string);
    }

    template<typename T> std::pair<APIRequestState, T> wrapperSendGetRegister(API& api, uint16_t const& slave_address, uint16_t const& reg_address)
    {
        nanoseconds timestamp = since_epoch();
        T value_read;
        bool check = true;
        try
        {
            sendGetRegister(link_, slave_address, reg_address, value_read);
        }
        catch(const ErrorDatagram& e)
        {
            sendAddIndividualErroredFrame(api,  slave_address, {timestamp, ecatCommand::SEND_GET_REGISTER, e.state()});
            check = false;
        }
        catch(const Error& e)
        {
            sendAddGeneralError(api, {timestamp, e.what()});
            check = false;
        }
        catch(const std::system_error& e)
        {
            if (e.code().value() == 110) { sendAddGeneralError(api, {timestamp, "Cannot read socket"}); }
            else { sendAddGeneralError(api, {timestamp, e.what()}); }
            sendAddIndividualErroredFrame(api,  slave_address, {timestamp, ecatCommand::SEND_GET_REGISTER, DatagramState::LOST});
            check = false;
        }
        catch( ... )
        {
            sendAddGeneralError(api, {timestamp, "Unknown error"});
            check = false;
        }

        if (check) {return std::pair(APIRequestState::ACK_OK, value_read);}
        else {return std::pair(APIRequestState::ACK_ISSUE, value_read);}
    }

    template<typename T> APIRequestState wrapperSendWriteRegister(API& api, uint16_t const& slave_address, uint16_t const& reg_address, T& value_write)
    {
        nanoseconds timestamp = since_epoch();
        bool check = true;
        try
        {
            sendWriteRegister(link_, slave_address, reg_address, value_write);
        }
        catch(const ErrorDatagram& e)
        {
            sendAddIndividualErroredFrame(api,  slave_address, {timestamp, ecatCommand::SEND_GET_REGISTER, e.state()});
            check = false;
        }
        catch(const Error& e)
        {
            sendAddGeneralError(api, {timestamp, e.what()});
            check = false;
        }
        catch(const std::system_error& e)
        {
            if (e.code().value() == 110) { sendAddGeneralError(api, {timestamp, "Cannot read socket"}); }
            else { sendAddGeneralError(api, {timestamp, e.what()}); }
            sendAddIndividualErroredFrame(api,  slave_address, {timestamp, ecatCommand::SEND_GET_REGISTER, DatagramState::LOST});
            check = false;
        }
        catch( ... )
        {
            sendAddGeneralError(api, {timestamp, "Unknown error"});
            check = false;
        }

        if (check) {return APIRequestState::ACK_OK;}
        else {return APIRequestState::ACK_ISSUE;}
    }

private:
    DiagBusStateMachine stateMachine = DiagBusStateMachine::IDLE;
    std::vector<config> slavesConfig;
};

#endif
