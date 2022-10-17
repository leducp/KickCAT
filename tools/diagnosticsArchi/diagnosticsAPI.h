#ifndef DIAGNOSTICS_API
#define DIAGNOSTICS_API

#include "kickcat/Container.h"
#include "kickcat/Slave.h"

#include <vector>
#include <string>
#include <tuple>
#include <mutex>
#include <unordered_map>
#include <queue>
#include <thread>

enum class APIDest {SERVER, BUS};
enum class APIHeader {NOP, COMMAND, CONTROL};
enum class APIRequestState {NOP, REQUEST, ACK_OK, ACK_ISSUE};
struct APIMessage 
{
    APIDest dest;
    APIHeader header;
    APIRequestState state;
    uint8_t id;
    std::string content;
};

class API
{
public:
    std::vector<SlaveErrorContainer> slavesErrorContainers;
    std::vector<ErrorProcess> processErrors;
    std::vector<ErrorGeneral> generalErrors;

    std::queue<APIMessage> busMailbox;
    std::queue<APIMessage> serverMailbox;

    API() = default;

    //Init functions
    void resizeSlavesErrorContainer(size_t const& size)
    {
        std::lock_guard<std::mutex> locked(modelLock);
        slavesErrorContainers.resize(size);
    }

    //Broadcast function
    void addBroadcastErroredFrame(std::tuple<nanoseconds, ecatCommand, DatagramState> const& err)
    {
        ErrorFrame errfrm = {std::get<0>(err), std::get<1>(err), true, std::get<2>(err)};
        std::lock_guard<std::mutex> locked(modelLock);
        for (auto& errContainer : slavesErrorContainers)
        {
            errContainer.ErroredFrames.push_back(errfrm);
        }
    }

    void addIndividualErroredFrame(uint16_t const& slave_address, std::tuple<nanoseconds, ecatCommand, DatagramState> const& err)
    {
        ErrorFrame errfrm = {std::get<0>(err), std::get<1>(err), true, std::get<2>(err)};
        std::lock_guard<std::mutex> locked(modelLock);
        slavesErrorContainers.at(slave_address).ErroredFrames.push_back(errfrm);
    }

    void addProcessError(ErrorProcess const& errprcss)
    {
        std::lock_guard<std::mutex> locked(modelLock);
        processErrors.push_back(errprcss);
    }

    void addGeneralError(ErrorGeneral const& errgnrl)
    {
        std::lock_guard<std::mutex> locked(modelLock);
        generalErrors.push_back(errgnrl);
    }


    //Slave specific functions
    void slaveUpdateALStatus(uint16_t const& slave_address, uint8_t const& slave_al_status)
    {
        std::lock_guard<std::mutex> locked(modelLock);
        slavesErrorContainers.at(slave_address).ALStatus = std::move(slave_al_status);
    }

    void slaveUpdateALStatusCode(uint16_t const& slave_address, uint16_t const& slave_al_status_code)
    {
        std::lock_guard<std::mutex> locked(modelLock);
        slavesErrorContainers.at(slave_address).ALStatusCode = std::move(slave_al_status_code);
    }

    void slaveUpdateDLStatus(uint16_t const& slave_address, reg::DLStatus const& slave_dl_status)
    {
        std::lock_guard<std::mutex> locked(modelLock);
        slavesErrorContainers.at(slave_address).DLStatus = std::move(slave_dl_status);
    }

    void slaveUpdateErrorCounters(uint16_t const& slave_address, ErrorCounters const& slave_errorCounters)
    {
        std::lock_guard<std::mutex> locked(modelLock);
        slavesErrorContainers.at(slave_address).CRCErrorCounters = std::move(slave_errorCounters);
    }

    void slaveUpdateCANState(uint16_t const& slave_address, uint16_t const& slave_statusWord)
    {
        std::lock_guard<std::mutex> locked(modelLock);
        slavesErrorContainers.at(slave_address).CANState = std::move(slave_statusWord);
    }

    void slaveAddEmergency(uint16_t const& slave_address, Emergency const& emg)
    {
        std::lock_guard<std::mutex> locked(modelLock);
        slavesErrorContainers.at(slave_address).Emergencies.push_back(emg);
    }


    //Get values
    std::vector<ErrorProcess> getErrorsProcess()
    {
        std::lock_guard<std::mutex> locked(modelLock);
        std::vector<ErrorProcess> erp = processErrors;
        return erp;
    }

    std::vector<ErrorGeneral> getErrorsGeneral()
    {
        std::lock_guard<std::mutex> locked(modelLock);
        std::vector<ErrorGeneral> erg = generalErrors;
        return erg;
    }

    SlaveErrorContainer getSlaveErrorContainer(uint16_t const& slave_address)
    {
        std::lock_guard<std::mutex> locked(modelLock);
        SlaveErrorContainer erc;
        try {erc = slavesErrorContainers.at(slave_address);}
        catch( ... ) {}
        return erc;
    }


    //Handle coms
    void sendMail(APIMessage const& mail)
    {
        switch(mail.dest)
        {
            case APIDest::BUS :
            {
                std::lock_guard<std::mutex> locked(busMailboxLock);
                busMailbox.push(mail);
                return;
            }
            case APIDest::SERVER :
            {
                std::lock_guard<std::mutex> locked(serverMailboxLock);
                serverMailbox.push(mail);
                return;
            }
            default: return;
        }
    }

    APIMessage checkMail(APIDest const& dest)
    {
        switch(dest)
        {
            case APIDest::BUS :
            {
                APIMessage message {APIDest::BUS, APIHeader::NOP, APIRequestState::NOP, 0, std::string("")};
                std::lock_guard<std::mutex> locked(busMailboxLock);
                if (not busMailbox.empty())
                {
                    message = busMailbox.front();
                    busMailbox.pop();
                }
                busMailboxLock.unlock();
                return message;
            }
            case APIDest::SERVER :
            {
                APIMessage message {APIDest::SERVER, APIHeader::NOP, APIRequestState::NOP, 0, std::string("")};
                std::lock_guard<std::mutex> locked(serverMailboxLock);
                if (not serverMailbox.empty())
                {
                    message = serverMailbox.front();
                    serverMailbox.pop();
                }
                return message;
            }
            default: return {APIDest::SERVER, APIHeader::NOP, APIRequestState::NOP, 0, std::string("")};
        }
    }


private:
    std::mutex modelLock;
    std::mutex busMailboxLock;
    std::mutex serverMailboxLock;
};

#endif
