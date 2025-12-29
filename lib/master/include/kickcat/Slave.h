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
        void parseSII();

        ErrorCounters const& errorCounters() const;
        int computeErrorCounters() const;

        /// \return the number of new errors since last call.
        int computeRelativeErrorCounters();

        /// \brief  Check the total number of errors since start of the slave
        /// \return True if too many errors detected since start of the slave. Return false otherwise.
        bool checkAbsoluteErrorCounters(int max_absolute_errors);

        int countOpenPorts();

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

}

#endif
