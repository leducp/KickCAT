#ifndef KICKCAT_BUS_H
#define KICKCAT_BUS_H

#include <memory>
#include <tuple>
#include <list>
#include <vector>
#include <functional>

#include "Error.h"
#include "Frame.h"
#include "Link.h"
#include "Slave.h"

namespace kickcat
{
    class AbstractSocket;

    class Bus
    {
    public:
        Bus(std::shared_ptr<AbstractSocket> socket);
        ~Bus() = default;

        // Enable user to adapt defaults values if they dont fit the current application (i.e. unit tests)
        void configureWaitLatency(nanoseconds tiny, nanoseconds big)
        { tiny_wait = tiny; big_wait = big; }

        // set the bus from an unknown state to PREOP state
        // 0ms disables the watchdog
        void init(nanoseconds watchdog = 100ms);

        /// \return the number of slaves detected on the bus
        int32_t detectedSlaves() const;

        /// \return the number of slaves detected on the bus
        int32_t detectSlaves();

        // request a state for all slaves
        void requestState(State request);
        //request a state for a single slave
        void requestState_single(Slave& slave, State request);

        // Get the state a specific slave
        State getCurrentState(Slave& slave);

        // wait for all slaves to reached a state
        void waitForState(State request, nanoseconds timeout);
        // wait for a single slave to reach a state
        void waitForState_single(Slave& slave, State request, nanoseconds timeout);

        // create the mapping between slaves PI and client buffer
        // if OK, set the bus to SAFE_OP state
        void createMapping(uint8_t* iomap);

        void resetPiFrames(uint32_t& address);
        //create mapping between single slave PI and client buffer
        void createMapping_single(Slave* slave, uint8_t* iomap, uint32_t& address);

        std::vector<Slave>& slaves() { return slaves_; }
        Link& link() { return link_; };

        // asynchrone read/write/mailbox/state methods
        // It enable users to do one or multiple operations in a row, process something, and process all awaiting frames.
        void sendGetALStatus(Slave& slave, std::function<void(DatagramState const&)> const& error);
        void sendGetDLStatus(Slave& slave);

        void sendLogicalRead(std::function<void(DatagramState const&)> const& error);
        void sendLogicalRead_single(Slave& slave, std::function<void(DatagramState const&)> const& error);
        void sendLogicalWrite(std::function<void(DatagramState const&)> const& error);
        void sendLogicalWrite_single(Slave& slave, std::function<void(DatagramState const&)> const& error);
        void sendLogicalReadWrite(std::function<void(DatagramState const&)> const& error);
        void sendMailboxesReadChecks (std::function<void(DatagramState const&)> const& error);  // Fetch in  mailboxes states (full/empty) of compatible slaves
        void sendMailboxesReadChecks_single(Slave& slave, std::function<void(DatagramState const&)> const& error);
        void sendMailboxesWriteChecks(std::function<void(DatagramState const&)> const& error);  // Fetch out mailboxes states (full/empty) of compatible slaves
        void sendMailboxesWriteChecks_single(Slave& slave, std::function<void(DatagramState const&)> const& error);
        void sendNop(std::function<void(DatagramState const&)> const& error);                   // Send a NOP datagram
        void processAwaitingFrames();

        void ping(Slave& slave, std::function<void(DatagramState const&)> const& error);

        // Process messages (read or write slave mailbox) - one at once per slave.
        void sendReadMessages(std::function<void(DatagramState const&)> const& error);
        void sendReadMessages_single(Slave& slave, std::function<void(DatagramState const&)> const& error);
        void sendWriteMessages(std::function<void(DatagramState const&)> const& error);
        void sendWriteMessages_single(Slave& slave, std::function<void(DatagramState const&)> const& error);
        void sendRefreshErrorCounters(std::function<void(DatagramState const&)> const& error);
        void sendRefreshErrorCounters_single(Slave& slave, std::function<void(DatagramState const&)> const& error);

        // helpers around start/finalize operations
        void finalizeDatagrams(); // send a frame if there is awaiting datagram inside
        void processDataRead(std::function<void(DatagramState const&)> const& error);
        void processDataWrite(std::function<void(DatagramState const&)> const& error);
        void processDataReadWrite(std::function<void(DatagramState const&)> const& error);

        void checkMailboxes( std::function<void(DatagramState const&)> const& error);
        void checkMailboxes_single(Slave& slave, std::function<void(DatagramState const&)> const& error);
        void processMessages(std::function<void(DatagramState const&)> const& error);
        void processMessages_single(Slave& slave, std::function<void(DatagramState const&)> const& error);


        enum Access
        {
            PARTIAL = 0,
            COMPLETE = 1,
            EMULATE_COMPLETE = 2
        };

        // Note: timeout is used on a per message basis: if complete access is emulated,
        // global call timeout will be at most N * timeout (with N the number of subindex to reached)
        void readSDO (Slave& slave, uint16_t index, uint8_t subindex, Access CA, void* data, uint32_t* data_size, nanoseconds timeout = 1s);
        void writeSDO(Slave& slave, uint16_t index, uint8_t subindex, bool CA,   void* data, uint32_t  data_size, nanoseconds timeout = 1s);

        uint16_t clearErrorCounters();

        //Supposed to be protected

        // Helpers for broadcast commands, mainly for init purpose
        /// \return working counter
        uint16_t broadcastRead(uint16_t ADO, uint16_t data_size);
        /// \return working counter
        uint16_t broadcastWrite(uint16_t ADO, void const* data, uint16_t data_size);

        // helper with trivial bus management (write then read)
        void processFrames();

        template<typename T>
        std::tuple<DatagramHeader const*, T const*, uint16_t> nextDatagram();

        // INIT state methods
        void resetSlaves(nanoseconds watchdog);
        void setAddresses();
        void configureMailboxes();
        void configureMailboxes_single(Slave& slave);

        // mapping helpers
        void detectMapping();
        void detectMapping_single(Slave& slave);
        void readMappedPDO(Slave& slave, uint16_t index);
        void configureFMMUs();
        void configureFMMUs_single(Slave& slave);

        // Slave SII eeprom helpers
        void fetchEeprom();
        void fetchEeprom_single(Slave& slave);
        bool areEepromReady();
        bool isEepromReady(Slave* const& slave);
        void readEeprom(uint16_t address, std::vector<Slave*> const& slaves, std::function<void(Slave&, uint32_t word)> apply);
        void readEeprom_single(uint16_t address, Slave* const& slave, std::function<void(Slave&, uint32_t word)> apply);

        // mailbox helpers
        void waitForMessage(std::shared_ptr<AbstractMessage> message, nanoseconds timeout);

        Link link_;
        std::vector<Slave> slaves_;

        uint8_t* iomap_read_section_;   // pointer on read section (to write back inputs)
        uint8_t* iomap_write_section_;  // pointer on write section (to send to the slaves)

        struct blockIO
        {
            uint8_t* iomap;     // client buffer
            uint32_t offset;    // frame offset
            int32_t  size;      // block size
            Slave*   slave;     // associated slave of this input
        };

        struct PIFrame
        {
            uint32_t address;               // logical address
            int32_t size;                   // frame size
            std::vector<blockIO> inputs;    // slave to master
            std::vector<blockIO> outputs;
        };
        std::vector<PIFrame> pi_frames_; // PI frame description

        nanoseconds tiny_wait{200us};
        nanoseconds big_wait{10ms};
    };
}

#endif
