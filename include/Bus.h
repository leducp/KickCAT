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
#include "Time.h"

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

        // set the bus from an unknown state to PREOP state
        Error init();

        // detect the number of slaves on the line
        uint16_t getSlavesOnNetwork();

        // request a state for all slaves
        Error requestState(State request);

        // Get the state a specific slave
        State getCurrentState(Slave const& slave);

        // wait for all slaves to reached a state
        Error waitForState(State request, nanoseconds timeout);

        // create thje mapping between slaves PI and client buffer
        // if OK, set the bus to SAFE_OP state
        Error createMapping(uint8_t* iomap);

        // Print various info about slaves, mainly from SII
        void printSlavesInfo();

        std::vector<Slave>& slaves() { return slaves_; }

        Error sendProcessData();

    protected: // for unit testing
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
        Error setAddresses();
        Error configureMailboxes();

        // mapping helpers
        Error detectMapping();
        Error readMappedPDO(Slave& slave, uint16_t index);
        Error configureFMMUs();

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

        uint8_t* iomap_;
        uint8_t pi_frames_;             // number of frames required for PI
        uint16_t pi_expected_wkc_[8];   // max 8 frames in PI - arbitrary setup for now
    };
}

#include "Bus.tpp"

#endif
