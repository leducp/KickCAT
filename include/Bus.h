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

    class Bus
    {
    public:
        Bus(std::shared_ptr<AbstractSocket> socket);
        ~Bus() = default;

        // set the bus from an unknown state to PREOP state
        void init();

        // detect the number of slaves on the line
        uint16_t getSlavesOnNetwork();

        // request a state for all slaves
        void requestState(State request);

        // Get the state a specific slave
        State getCurrentState(Slave const& slave);

        // wait for all slaves to reached a state
        void waitForState(State request, nanoseconds timeout);

        // create thje mapping between slaves PI and client buffer
        // if OK, set the bus to SAFE_OP state
        void createMapping(uint8_t* iomap);

        // Print various info about slaves, mainly from SII
        void printSlavesInfo();

        std::vector<Slave>& slaves() { return slaves_; }

        void processDataRead();
        void processDataWrite();
        void processDataReadWrite();

        void readSDO (Slave& slave, uint16_t index, uint8_t subindex, bool CA, void* data, uint32_t* data_size);
        void writeSDO(Slave& slave, uint16_t index, uint8_t subindex, bool CA, void* data, uint32_t data_size);

        void clearErrorCounters();
        void refreshErrorCounters();

        void processMessages();

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
        // Update slaves mailboxes state
        void checkMailboxes();
        bool waitForMessage(Slave& slave, nanoseconds timeout);

        std::shared_ptr<AbstractSocket> socket_;
        std::vector<Frame> frames_;
        int32_t current_frame_{0};
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

        bool check_loop_{true}; // check slaves mailbox or process messages, but do not do all in the same row to reduce loop latency
    };
}

#include "Bus.tpp"

#endif
