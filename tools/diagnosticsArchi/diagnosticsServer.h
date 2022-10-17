#ifndef DIAGNOSTICS_SERVER
#define DIAGNOSTICS_SERVER

#include "diagnosticsAPI.h"
#include "formatter.h"

#include <unordered_map>
#include <algorithm>
#include <future>

class DiagnosticsServer
{
public:

    DiagnosticsServer() = default;

    void wait(nanoseconds snooze) {sleep(snooze); }

    void printErrorProcess(API& api)
    {
        for (auto& error : api.getErrorsProcess())
        {
            std::cout << error.timestamp.count() << " - " << ProcessStateToFormat(error.stateProcess) << "\n";
        }
    }

    void printErrorGeneral(API& api)
    {
        for (auto& error : api.getErrorsGeneral())
        {
            std::cout << error.timestamp.count() << " - " << error.msg << "\n";
        }
    }

    void printErroredFrames(API& api, uint16_t const& slave_address)
    {
        for (auto& error : api.getSlaveErrorContainer(slave_address).ErroredFrames)
        {
            std::cout << error.timestamp.count() << " - " << error.broadcast << " - " << EcatCommandToFormat(error.command) << DatagramStateToFormat(error.state) << "\n";
        }
    }

    void printCurrentState(API& api, uint16_t const& slave_address)
    {
        auto container =  api.slavesErrorContainers.at(slave_address);
        std::cout << ALStatusToFormat(container.ALStatus).name << " - " << ALStatusCodeToFormat(container.ALStatusCode).meaning << " : " << ALStatusCodeToFormat(container.ALStatusCode).desc << "\n";
    }

    void sendBusRequest(API& api, APIHeader const& header, std::string content, std::function<void(APIMessage)> onResponse)
    {
        uint8_t index = requestsIndex;
        static_cast<void>(std::async(std::launch::async, [&api, &header, &index, &content]{
            api.sendMail({APIDest::BUS, header, APIRequestState::REQUEST, index, std::move(content)});
        }));
        waitingPool.emplace(requestsIndex, onResponse);
        requestsIndex++;
    }

    template<typename T> void sendBusGetRegister(API& api, uint16_t const& slave_address, uint16_t const& reg, T type, std::function<void(APIMessage)> onResponse)
    {
        uint8_t index = requestsIndex;
        std::string content = formatGetRegRequest<T>(slave_address, reg, type);
        static_cast<void>(std::async(std::launch::async, [&api, &index, &content]{
            api.sendMail({APIDest::BUS, APIHeader::COMMAND, APIRequestState::REQUEST, index, std::move(content)});
        }));
        waitingPool.emplace(requestsIndex, onResponse);
        requestsIndex++;
    }

    template<typename T> void sendBusWriteRegister(API& api, uint16_t const& slave_address, uint16_t const& reg, T value_write, std::function<void(APIMessage)> onResponse)
    {
        uint8_t index = requestsIndex;
        std::string content = formatWriteRegRequest<T>(slave_address, reg, value_write);
        static_cast<void>(std::async(std::launch::async, [&api, &index, &content]{
            api.sendMail({APIDest::BUS, APIHeader::COMMAND, APIRequestState::REQUEST, index, std::move(content)});
        }));
        waitingPool.emplace(requestsIndex, onResponse);
        requestsIndex++;
    }

    void checkReceivedMail(API& api)
    {
        std::future<APIMessage> msg_to_get = (std::async(std::launch::async, [&api]{
            return api.checkMail(APIDest::SERVER);
        }));
        APIMessage msg = msg_to_get.get();
        if (waitingPool.find(msg.id) != waitingPool.end())
        {
            std::function<void(APIMessage)> apply = waitingPool[msg.id];
            waitingPool.erase(msg.id);
            apply(msg);
        }
    }
private:
    template<typename T> std::string formatGetRegRequest(uint16_t const& slave_address, uint16_t const& reg, T type)
    {
        //GET_REGISTER-1@0013::1
        return std::string("GET_REGISTER-") +std::to_string(slave_address) + std::string("@") + std::to_string(reg) + std::string("::")+ std::to_string(sizeof(type)); 
    };

    template<typename T> std::string formatWriteRegRequest(uint16_t const& slave_address, uint16_t const& reg, T value_write)
    {
        //GET_REGISTER-1@0013::1/0000
        return std::string("WRITE_REGISTER-") +std::to_string(slave_address) + std::string("@") + std::to_string(reg) + std::string("::")+ std::to_string(sizeof(value_write)) + std::string("/") + std::to_string(value_write); 
    };


    uint8_t requestsIndex = 0;
    std::unordered_map<uint8_t, std::function<void(APIMessage)>> waitingPool;
};

#endif
