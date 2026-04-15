#ifndef KICKCAT_SLAVE_H
#define KICKCAT_SLAVE_H

#include <sstream>
#include <string_view>
#include <vector>

#include "kickcat/Mailbox.h"
#include "kickcat/SIIParser.h"
#include "kickcat/protocol.h"

namespace kickcat
{
    struct Slave
    {
        void parseSII(uint8_t const* data, std::size_t size);

        /// \brief Human-readable slave name. Returns the SII general-category device name when present,
        ///        otherwise a fallback derived from the fixed station address (e.g. "Slave @0x1001").
        std::string name() const;

        /// \brief Slave order/type code from the SII general category (ESI Device:Type / OrderIdx).
        ///        Returns an empty string if the SII has no order string.
        std::string type() const;

        ErrorCounters const& errorCounters() const;
        int computeErrorCounters() const;

        /// \return the number of new errors since last call.
        int computeRelativeErrorCounters();

        /// \brief  Check the total number of errors since start of the slave
        /// \return True if too many errors detected since start of the slave. Return false otherwise.
        bool checkAbsoluteErrorCounters(int max_absolute_errors);

        int countOpenPorts() const;

        //---------------------- DC Helpers --------------------------//
        // Port bitmask
        static constexpr uint8_t PORT0 = (1 << 0);
        static constexpr uint8_t PORT1 = (1 << 1);
        static constexpr uint8_t PORT2 = (1 << 2);
        static constexpr uint8_t PORT3 = (1 << 3);

        bool isDCSupport() const;

        // Retrieve the timestamp of a port (for DC)
        nanoseconds portTime(uint8_t port);

        // Compute the delta time between port_a and port_b - handle the wrapping
        nanoseconds portDelta(uint8_t port_a, uint8_t port_b);

        /// \return active ports bitmap from DLStatus
        uint8_t activePorts();

        // Calculate previous active port (order: 0 - 3 - 1 - 2)
        uint8_t prevPort(uint8_t port);

        //-----------------------------------------------------------//

        uint16_t address;
        uint8_t al_status{State::INVALID};
        uint16_t al_status_code;

        mailbox::request::Mailbox mailbox;
        mailbox::request::Mailbox mailbox_bootstrap;
        int32_t waiting_datagram;  // how many datagram to process for this slave

        DLStatus dl_status;

        eeprom::SII sii{};

        struct PIMapping
        {
            uint8_t* data;         // buffer client to read or write back
            int32_t size;          // size fo the mapping (in bits)
            int32_t bsize;         // size of the mapping (in bytes)
            int32_t sync_manager;  // associated Sync manager
            uint32_t address;      // logical address
        };
        // set it to true to let user define the mapping, false to autodetect it
        // If set to true, user shall set input and output mapping bsize and sync_manager members.
        bool is_static_mapping;
        PIMapping input;  // slave to master
        PIMapping output;

        ErrorCounters error_counters;
        int previous_errors_sum{0};

        ESC::Description esc;

        // DC received time record - required to compute propagation delay
        nanoseconds dc_received_time[4];
        nanoseconds dc_ecat_received_time;
        nanoseconds delay = 0ns;
        nanoseconds dc_time_offset = 0ns;
    };

    // Helper to find a slave by its address
    Slave* findSlaveByAddress(std::vector<Slave>& slaves, uint16_t address);
}

#endif
