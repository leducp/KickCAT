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
#include "Time.h"

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
        void init();

        /// \return the number of slaves detected on the bus
        int32_t detectedSlaves() const;

        // request a state for all slaves
        void requestState(State request);

        // Get the state a specific slave
        State getCurrentState(Slave& slave);

        // wait for all slaves to reached a state
        void waitForState(State request, nanoseconds timeout);

        // create thje mapping between slaves PI and client buffer
        // if OK, set the bus to SAFE_OP state
        void createMapping(uint8_t* iomap);

        // Print various info about slaves, mainly from SII
        void printSlavesInfo();

        std::vector<Slave>& slaves() { return slaves_; }

        // asynchrone read/write/mailbox/state methods
        // It enable users to do one or multiple operations in a row, process something, and process all awaiting frames.
        void sendGetALStatus(Slave& slave, std::function<void()> const& error);

        void sendLogicalRead(std::function<void()> const& error);
        void sendLogicalWrite(std::function<void()> const& error);
        void sendLogicalReadWrite(std::function<void()> const& error);
        void sendMailboxesChecks(std::function<void()> const& error);   // Fetch in/out mailboxes states (full/empty) of compatible slaves
        void sendNop(std::function<void()> const& error);               // Send a NOP datagram
        void processAwaitingFrames();

        // Process messages (read or write slave mailbox) - one at once per slave.
        void sendReadMessages(std::function<void()> const& error);
        void sendWriteMessages(std::function<void()> const& error);
        void sendrefreshErrorCounters(std::function<void()> const& error);

        // helpers around start/finalize oeprations
        void processDataRead(std::function<void()> const& error);
        void processDataWrite(std::function<void()> const& error);
        void processDataReadWrite(std::function<void()> const& error);

        void checkMailboxes( std::function<void()> const& error);
        void processMessages(std::function<void()> const& error);

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

        void clearErrorCounters();


    protected: // for unit testing

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
        void detectSlaves();
        void resetSlaves();
        void setAddresses();
        void configureMailboxes();

        // mapping helpers
        void detectMapping();
        void readMappedPDO(Slave& slave, uint16_t index);
        void configureFMMUs();

        // Slave SII eeprom helpers
        void fetchEeprom();
        bool areEepromReady();
        void readEeprom(uint16_t address, std::vector<Slave*> const& slaves, std::function<void(Slave&, uint32_t word)> apply);

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
