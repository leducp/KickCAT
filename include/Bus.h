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
    // max 8 frames for process data - arbitrary setup for now
    constexpr int32_t MAX_PI_FRAMES = 8;

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

        Error processDataRead();
        Error processDataWrite();
        Error processDataReadWrite();

        void readSDO (Slave& slave, uint16_t index, uint8_t subindex, bool CA, void* data, int32_t* data_size);
        void writeSDO(Slave& slave, uint16_t index, uint8_t subindex, bool CA, void const* data, int32_t* data_size);

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

        // Slave SII eeprom helpers
        Error fetchEeprom();
        bool areEepromReady();
        Error readEeprom(uint16_t address, std::vector<Slave*> const& slaves, std::function<void(Slave&, uint32_t word)> apply);

        // mailbox helpers
        // Update slaves mailboxes state
        void checkMailboxes();

        // CoE helpers
        void SDORequest(Slave& slave, uint16_t index, uint8_t subindex, bool CA, uint8_t request, void* data = nullptr, uint32_t size = 0);

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
    };
}

#include "Bus.tpp"

#endif
