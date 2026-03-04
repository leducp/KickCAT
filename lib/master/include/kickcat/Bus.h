#ifndef KICKCAT_BUS_H
#define KICKCAT_BUS_H

#include <vector>

#include "kickcat/Error.h"
#include "kickcat/Frame.h"
#include "AbstractLink.h"
#include "Slave.h"

namespace kickcat
{
    class Bus
    {
    public:
        Bus(std::shared_ptr<AbstractLink> link);
        ~Bus() = default;

        // Enable user to adapt defaults values if they dont fit the current application (i.e. unit tests)
        void configureWaitLatency(nanoseconds tiny, nanoseconds big)
        { tiny_wait = tiny; big_wait = big; }

        // set the bus from an unknown state to PREOP state
        // 0ms disables the watchdog
        void init(nanoseconds watchdog = 100ms);

        /// \brief Enable Distributed Clock
        /// \details  Shall be called in PRE-OP, but some slaves needs a call in INIT
        /// \param    cycle_time    Duration of the slave cycle time
        /// \param    shift_cycle   When the DC SYNC0 shall be generated in the cycle
        /// \param    start_delay   Delay before activating the DC cycle
        /// \return   an absolute time point that serve as a sync point with the slaves DC cycle
        nanoseconds enableDC(nanoseconds cycle_time = 1ms, nanoseconds shift_cycle = 500us, nanoseconds start_delay = 100ms);

        /// \return the number of slaves detected on the bus
        int32_t detectedSlaves() const;

        /// \return the number of slaves detected on the bus
        int32_t detectSlaves();

        /// \brief Enable ECAT Interrupt
        /// \param Event    that trigger the interrupt to enable
        /// \param callback callback to be called whenever the IRQ trigger (rising edge)
        void enableIRQ(enum EcatEvent event, std::function<void()> callback);
        void disableIRQ(enum EcatEvent);

        // request a state for all slaves
        void requestState(State request);

        // Get the state a specific slave
        State getCurrentState(Slave& slave);

        // wait for all slaves to reached a state
        // background_task may be used to keep updated PDO while waiting for a particular state.
        void waitForState(State request, nanoseconds timeout, std::function<void()> background_task = [](){});

        // create the mapping between slaves PI and client buffer
        // if OK, set the bus to SAFE_OP state
        void createMapping(uint8_t* iomap);

        std::vector<Slave>& slaves() { return slaves_; }

        // asynchrone read/write/mailbox/state methods
        // It enable users to do one or multiple operations in a row, process something, and process all awaiting frames.
        void sendGetALStatus(Slave& slave, std::function<void(DatagramState const&)> const& error);
        void sendGetDLStatus(Slave& slave, std::function<void(DatagramState const&)> const& error);

        void sendLogicalRead(std::function<void(DatagramState const&)> const& error);
        void sendLogicalWrite(std::function<void(DatagramState const&)> const& error);
        void sendLogicalReadWrite(std::function<void(DatagramState const&)> const& error);
        void sendMailboxesReadChecks (std::function<void(DatagramState const&)> const& error);  // Fetch in  mailboxes states (full/empty) of compatible slaves
        void sendMailboxesWriteChecks(std::function<void(DatagramState const&)> const& error);  // Fetch out mailboxes states (full/empty) of compatible slaves
        void sendNop(std::function<void(DatagramState const&)> const& error);                   // Send a NOP datagram
        void processAwaitingFrames();

        // Process messages (read or write slave mailbox) - one at once per slave.
        void sendReadMessages(std::function<void(DatagramState const&)> const& error);
        void sendWriteMessages(std::function<void(DatagramState const&)> const& error);
        void sendRefreshErrorCounters(std::function<void(DatagramState const&)> const& error);

        // helpers around start/finalize operations
        void finalizeDatagrams(); // send a frame if there is awaiting datagram inside
        void processDataRead(std::function<void(DatagramState const&)> const& error);
        void processDataWrite(std::function<void(DatagramState const&)> const& error);
        void processDataReadWrite(std::function<void(DatagramState const&)> const& error);

        void checkMailboxes( std::function<void(DatagramState const&)> const& error);
        void processMessages(std::function<void(DatagramState const&)> const& error);

        /// \brief  Send drift compensation datagrams to maintain DC synchronization
        /// \details Writes the current master time to the DC reference clock slave's system time register (0x0910)
        ///          using FPWR, then reads it back with FRMW so that each slave on the segment updates its
        ///          local clock offset accordingly.
        ///          Called cyclically during process data exchange, and repeatedly (15 000 times) during
        ///          static drift compensation at DC initialization.
        /// \param  error  Callback invoked when a datagram error occurs
        void sendDriftCompensation(std::function<void(DatagramState const&)> const& error);

        /// \brief  Check if distributed clocks are synchronized
        /// \details Reads the system time difference register (0x092C) of each DC slave.
        ///          The value converges to zero when the slave is synchronized with the reference clock.
        /// \param  threshold  Maximum acceptable time difference in nanoseconds
        /// \return true if all DC slaves are synchronized within the given threshold
        bool isDCSynchronized(nanoseconds threshold = 1000ns);


        enum Access
        {
            PARTIAL = 0,
            COMPLETE = 1,
            EMULATE_COMPLETE = 2
        };

        // Note: timeout is used on a per message basis: if complete access is emulated,
        // global call timeout will be at most N * timeout (with N the number of subindex to reached)
        void readSDO (Slave& slave, uint16_t index, uint8_t subindex, Access CA, void* data, uint32_t* data_size, nanoseconds timeout = 1s);
        void writeSDO(Slave& slave, uint16_t index, uint8_t subindex, Access CA, void const* data, uint32_t data_size, nanoseconds timeout = 1s);

        /// \brief  Add a gateway message to the bus
        /// \param  raw_message         A raw EtherCAT mailbox message
        /// \param  raw_message_size    Size of the mailbox message (shall be less or equal of the actual storage size)
        /// \param  gateway_index       Index to link the request to the client in the gateway mechanism.
        /// \return nullptr if message cannot be added (malformed, bad address, unsupported protocol, etc.), a handle on the message otherwise
        std::shared_ptr<mailbox::request::GatewayMessage> addGatewayMessage(uint8_t const* raw_message, int32_t raw_message_size, uint16_t gateway_index);

        void clearErrorCounters();

        // Helpers for broadcast commands, mainly for init purpose
        /// \return working counter
        uint16_t broadcastRead(uint16_t ADO, uint16_t data_size);

        /// \return working counter
        uint16_t broadcastWrite(uint16_t ADO, void const* data, uint16_t data_size);
        void setAddresses();

        // Slave SII eeprom helpers
        void fetchEeprom();
        bool areEepromReady();
        void readEeprom(uint16_t address, std::vector<Slave*> const& slaves, std::function<void(Slave&, uint32_t word)> apply);

        bool isEepromAcknowledged(Slave& slave);
        void writeEeprom(Slave& slave, uint16_t address, void* data, uint16_t size);

        void resetSlaves(nanoseconds watchdog);
        void fetchESC();
        void fetchDL();

        // mailbox helpers
        void waitForMessage(std::shared_ptr<mailbox::request::AbstractMessage> message);

    protected: // for unit testing
        // helper with trivial bus management (write then read)
        void processFrames();

        template<typename T>
        std::tuple<DatagramHeader const*, T const*, uint16_t> nextDatagram();

        // INIT state methods

        void configureMailboxes();

        // mapping helpers
        void detectMapping();
        void readMappedPDO(Slave& slave, uint16_t index);
        void configureFMMUs();

        // DC helpers
        void fetchReceivedTimes();
        void computePropagationDelay(nanoseconds master_time); // Master time (ref point is EtherCAT epoch, NOT UNIX epoch)
        void applyMasterTime();

        std::shared_ptr<AbstractLink> link_;
        std::vector<Slave> slaves_;

        uint8_t* iomap_read_section_{};   // pointer on read section (to write back inputs)
        uint8_t* iomap_write_section_{};  // pointer on write section (to send to the slaves)

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

        uint16_t irq_mask_{0};

        Slave* dc_slave_{nullptr};
    };

    /**
     * @brief Configure a slave PDO mapping
     * @param bus           EtherCAT bus to use for SDO transfers
     * @param slave         Slave to configure
     * @param pdo_map       PDO mapping index (e.g. 0x1A00 for TxPDO, 0x1600 for RxPDO)
     * @param mapping       List of object address/bit length to map (e.g. 0x60000010)
     * @param mapping_count Number of objects in the mapping
     * @param sm_map        Sync Manager mapping index (e.g. 0x1C12 for Outputs, 0x1C13 for Inputs)
     */
    void mapPDO(Bus& bus, Slave& slave, uint16_t pdo_map, uint32_t const* mapping, uint8_t mapping_count, uint16_t sm_map);
}

#endif
