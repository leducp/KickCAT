#ifndef KICKCAT_BUS_H
#define KICKCAT_BUS_H

#include <memory>
#include <tuple>
#include <list>
#include <vector>
#include <functional>

#include "Error.h"
#include "Frame.h"
#include "Slave.h"

namespace kickcat
{
    class AbstractSocket;

    struct RawMessage
    {
        uint16_t id;
        std::vector<uint8_t> payload;
    };


    class Bus
    {
    public:
        Bus(std::shared_ptr<AbstractSocket> socket);
        ~Bus() = default;

        Error init();
        Error requestState(State request);
        State getCurrentState(Slave const& slave);

        uint16_t getSlavesOnNetwork();

        void printSlavesInfo();

    private:
        uint8_t idx_{0};

        // Helpers for broadcast commands, mainly for init purpose
        /// \return working counter
        uint16_t broadcastRead(uint16_t ADO, uint16_t data_size);
        /// \return working counter
        uint16_t broadcastWrite(uint16_t ADO, void const* data, uint16_t data_size);

        // helpers to aggregate multiple datagrams and process them on the line
        void addDatagram(enum Command command, uint32_t address, void const* data, uint16_t data_size);
        template<typename T>
        void addDatagram(enum Command command, uint32_t address, T const& data)
        {
            addDatagram(command, address, &data, sizeof(data));
        }
        Error processFrames();
        template<typename T>
        std::tuple<DatagramHeader const*, T const*, uint16_t> nextDatagram();

        // INIT state methods
        Error detectSlaves();
        Error resetSlaves();
        Error configureMailboxes();

        // mapping helpers
        Error createMapping(uint8_t* iomap);
        Error readMappedPDO(Slave& slave, uint16_t index);

        Error fetchEeprom();
        bool areEepromReady();
        Error readEeprom(uint16_t address, std::vector<Slave*> const& slaves, std::function<void(Slave&, uint32_t word)> apply);

        // mailbox helpers
        // Update slaves mailboxes state
        void checkMailboxes();
        void readSDO(Slave& slave, uint16_t index, uint8_t subindex, bool CA, void* data, int32_t* data_size);


        std::shared_ptr<AbstractSocket> socket_;
        std::vector<Frame> frames_;
        int32_t current_frame_{0};
        std::vector<Slave> slaves_;
    };
}

#include "Bus.tpp"

#endif
