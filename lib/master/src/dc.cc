#include <inttypes.h>
#include <unordered_map>

#include "debug.h"
#include "Bus.h"
#include "Diagnostics.h"

namespace kickcat
{
    nanoseconds Bus::enableDC(nanoseconds cycle_time, nanoseconds shift_cycle, nanoseconds start_delay)
    {
        for (auto& slave : slaves_)
        {
            if (slave.esc.features & ESC::feature::DC_AVAILABLE)
            {
                dc_slave_ = &slave;
                break;
                //TODO: read 0x910 DC_SYSTEM_TIME to check if the slave can provide a clock
                //TODO: read DC port received times - compute drift depending on topology
            }
        }
        dc_slave_ = &slaves_.at(1);
        if (dc_slave_ == nullptr)
        {
            THROW_ERROR("No dc slave found");
        }
        dc_info("DC reference slave is %d\n", dc_slave_->address);

        //----------------------- Apply cycle time ----------------------//
        uint32_t cycle_time_raw = static_cast<uint32_t>(cycle_time.count());
        broadcastWrite(reg::DC_SYNC0_CYCLE_TIME, &cycle_time_raw, 4);

        //-------- Apply propagation delay and system time offset -------//
        // Trigger latch time on received registers whenever the frame pass by the port 0-3
        uint8_t dummy = 0;
        broadcastWrite(reg::DC_RECEIVED_TIME, &dummy, 1);
        nanoseconds now = since_ecat_epoch();

        fetchReceivedTimes();
        computePropagationDelay(now);
        applyMasterTime();

        //------------------ Static drift compensation ------------------//
        // 1. reset time control loop filter
        uint16_t reset = 0x1000;
        broadcastWrite(reg::DC_SPEED_CNT_START, &reset, sizeof(uint16_t));

        // 2. Send multiple FRMW drift compensation frames (15000 from the doc)
        auto error = [](DatagramState const& state)
        {
            THROW_ERROR_DATAGRAM("Error while doing static drift compensation", state);
        };
        for (int i = 0; i < 15000; ++i)
        {
            sendDriftCompensation(error);
            link_->processDatagrams();
        }


        //------------------------ Set start time -----------------------//
        // 1. Get current network time to compute start time (relative to absolute)
        uint64_t network_time;
        auto& slave = *dc_slave_;
        auto process = [&slave, &network_time](DatagramHeader const*, uint8_t const* data, uint16_t wkc)
        {
            if (wkc != 1)
            {
                return DatagramState::INVALID_WKC;
            }

            std::memcpy(&network_time, data, 8);
            return DatagramState::OK;
        };

        link_->addDatagram(Command::FPRD, createAddress(slave.address, reg::DC_SYSTEM_TIME), nullptr, 8, process, [](DatagramState const&){});
        link_->processDatagrams();

        // 2. Convert relative start time to absolute, round it to cycle time and apply a delay and shift in the cycle
        nanoseconds start_time = (nanoseconds(network_time) / cycle_time) * cycle_time + cycle_time + shift_cycle + start_delay;
        uint64_t start_time_raw = start_time.count();

        // 3. Apply start time
        broadcastWrite(reg::DC_START_TIME, &start_time_raw, sizeof(start_time_raw));

        //---------------------- Apply SYNC config ----------------------//
        // TODO: use a config in the slave to select the sync method
        uint8_t enable_dc_sync0 = 0x3;
        broadcastWrite(reg::DC_SYNC_ACTIVATION, &enable_dc_sync0, 1);

        return to_unix_epoch(start_time - start_delay - shift_cycle + slave.dc_time_offset);
    }


    void Bus::fetchReceivedTimes()
    {
        for (auto& slave : slaves_)
        {
            auto error = [](DatagramState const& state)
            {
                THROW_ERROR_DATAGRAM("Error while fetching slave DC received times", state);
            };

            auto process = [&slave](DatagramHeader const*, uint8_t const* data, uint16_t wkc)
            {
                if (wkc != 1)
                {
                    return DatagramState::INVALID_WKC;
                }

                for (int i = 0; i < 4; ++i)
                {
                    uint32_t raw_timestamp;
                    std::memcpy(&raw_timestamp, data + i * sizeof(uint32_t), sizeof(uint32_t));
                    slave.dc_received_time[i] = nanoseconds(raw_timestamp);
                }
                return DatagramState::OK;
            };
            link_->addDatagram(Command::FPRD, createAddress(slave.address, reg::DC_RECEIVED_TIME), nullptr, 16, process, error);

            auto process_ecat = [&slave](DatagramHeader const*, uint8_t const* data, uint16_t wkc)
            {
                if (wkc != 1)
                {
                    return DatagramState::INVALID_WKC;
                }

                uint64_t raw_timestamp;
                std::memcpy(&raw_timestamp, data, sizeof(uint64_t));
                slave.dc_ecat_received_time = nanoseconds(raw_timestamp);

                return DatagramState::OK;
            };
            link_->addDatagram(Command::FPRD, createAddress(slave.address, reg::DC_ECAT_RECEIVED_TIME), nullptr, 8, process_ecat, error);
        }
        link_->processDatagrams();

        for (auto& slave : slaves_)
        {
            dc_info("Received times %d\n", slave.address);
            dc_info(  "Port 0: %ld\n", slave.dc_received_time[0].count());
            dc_info(  "Port 1: %ld\n", slave.dc_received_time[1].count());
            dc_info(  "Port 2: %ld\n", slave.dc_received_time[2].count());
            dc_info(  "Port 3: %ld\n", slave.dc_received_time[3].count());
            dc_info(  "EPU:    %ld\n", slave.dc_ecat_received_time.count());

            nanoseconds delta = slave.dc_received_time[1] - slave.dc_received_time[0];
            dc_info("  T1 - T0 = %ld\n",delta.count());
        }
    }


    namespace
    {
        // Helper function to get port timestamp
        nanoseconds portTime(Slave const& slave, uint8_t port)
        {
            if (port < 4)
            {
                return slave.dc_received_time[port];
            }
            return 0ns;
        }

        // Get active ports bitmap from DLStatus
        uint8_t getActivePorts(Slave const& slave)
        {
            uint8_t active = 0;
            if (slave.dl_status.PL_port0) active |= (1 << 0);
            if (slave.dl_status.PL_port1) active |= (1 << 1);
            if (slave.dl_status.PL_port2) active |= (1 << 2);
            if (slave.dl_status.PL_port3) active |= (1 << 3);
            return active;
        }

        // Calculate previous active port (order: 0 - 3 - 1 - 2)
        uint8_t prevPort(Slave const& slave, uint8_t port)
        {
            uint8_t pport = port;
            uint8_t active = getActivePorts(slave);

            switch (port)
            {
                case 0:
                    if (active & (1 << 2)) pport = 2;
                    else if (active & (1 << 1)) pport = 1;
                    else if (active & (1 << 3)) pport = 3;
                    break;
                case 1:
                    if (active & (1 << 3)) pport = 3;
                    else if (active & (1 << 0)) pport = 0;
                    else if (active & (1 << 2)) pport = 2;
                    break;
                case 2:
                    if (active & (1 << 1)) pport = 1;
                    else if (active & (1 << 3)) pport = 3;
                    else if (active & (1 << 0)) pport = 0;
                    break;
                case 3:
                    if (active & (1 << 0)) pport = 0;
                    else if (active & (1 << 2)) pport = 2;
                    else if (active & (1 << 1)) pport = 1;
                    break;
            }
            return pport;
        }

        // Find and consume an unconsumed port on parent (search order: 3 - 1 - 2 - 0)
        uint8_t parentPort(uint8_t& consumed_ports)
        {
            uint8_t parentport = 0;
            uint8_t b = consumed_ports;

            if (b & (1 << 3))
            {
                parentport = 3;
                b &= ~(1 << 3);
            }
            else if (b & (1 << 1))
            {
                parentport = 1;
                b &= ~(1 << 1);
            }
            else if (b & (1 << 2))
            {
                parentport = 2;
                b &= ~(1 << 2);
            }
            else if (b & (1 << 0))
            {
                parentport = 0;
                b &= ~(1 << 0);
            }

            consumed_ports = b;
            return parentport;
        }

        // Find slave by address
        Slave* findSlaveByAddress(std::vector<Slave>& slaves, uint16_t address)
        {
            for (auto& slave : slaves)
            {
                if (slave.address == address)
                {
                    return &slave;
                }
            }
            return nullptr;
        }
    }

    void Bus::computePropagationDelay(nanoseconds master_time)
    {
        // Get topology to find parent relationships
        std::unordered_map<uint16_t, uint16_t> topology = getTopology(slaves_);

        // Map to track consumed ports per slave (bitmap: ....3210)
        std::unordered_map<uint16_t, uint8_t> consumed_ports;
        // Map to track entry port per slave
        std::unordered_map<uint16_t, uint8_t> entry_ports;

        // Initialize consumed ports with active ports for all slaves
        for (auto& slave : slaves_)
        {
            consumed_ports[slave.address] = getActivePorts(slave);
        }

        // Set DC reference slave time offset
        dc_slave_->dc_time_offset = master_time - dc_slave_->dc_ecat_received_time;
        dc_slave_->delay = 0ns;

        // Process all slaves in order
        uint16_t parenthold = 0;
        for (auto& slave : slaves_)
        {
            if (slave.isDCSupport())
            {
                // Compute time offset from ref clock
                slave.dc_time_offset = master_time - slave.dc_ecat_received_time;

                // Find entry port (port with lowest timestamp)
                uint8_t active = getActivePorts(slave);
                int8_t active_port_count = 0;
                int8_t active_port_numbers[4];
                nanoseconds port_timestamps[4];

                // Build list of active ports and their timestamps (order: 0, 3, 1, 2)
                if (active & (1 << 0))
                {
                    active_port_numbers[active_port_count] = 0;
                    port_timestamps[active_port_count] = portTime(slave, 0);
                    active_port_count++;
                }
                if (active & (1 << 3))
                {
                    active_port_numbers[active_port_count] = 3;
                    port_timestamps[active_port_count] = portTime(slave, 3);
                    active_port_count++;
                }
                if (active & (1 << 1))
                {
                    active_port_numbers[active_port_count] = 1;
                    port_timestamps[active_port_count] = portTime(slave, 1);
                    active_port_count++;
                }
                if (active & (1 << 2))
                {
                    active_port_numbers[active_port_count] = 2;
                    port_timestamps[active_port_count] = portTime(slave, 2);
                    active_port_count++;
                }

                // Entry port is port with the lowest timestamp
                uint8_t entryport = 0;
                if ((active_port_count > 1) and (port_timestamps[1] < port_timestamps[entryport]))
                {
                    entryport = 1;
                }
                if ((active_port_count > 2) and (port_timestamps[2] < port_timestamps[entryport]))
                {
                    entryport = 2;
                }
                if ((active_port_count > 3) and (port_timestamps[3] < port_timestamps[entryport]))
                {
                    entryport = 3;
                }
                entryport = active_port_numbers[entryport];
                entry_ports[slave.address] = entryport;

                // Consume entry port from consumed ports
                consumed_ports[slave.address] &= ~(1 << entryport);

                // Find DC parent (walk up parent chain until we find a DC slave or master)
                Slave* parent_slave = nullptr;
                uint16_t parent_address = topology[slave.address];

                // Walk up parent chain until we find a DC slave or reach master
                // Note: a slave that is its own parent (topology[address] == address) is connected to master
                while (parent_address != 0)
                {
                    // Check if current parent is DC-capable
                    Slave* candidate = findSlaveByAddress(slaves_, parent_address);
                    if (candidate and candidate->isDCSupport())
                    {
                        parent_slave = candidate;
                        break;
                    }

                    // Stop if we've reached master (slave that is its own parent)
                    if (parent_address == topology[parent_address])
                    {
                        break;
                    }

                    // Move to next parent in chain
                    parent_address = topology[parent_address];
                }

                // Only calculate propagation delay if slave has a DC parent (not master)
                if (parent_slave != nullptr)
                {
                    // Find port on parent this slave is connected to
                    uint8_t parentport = parentPort(consumed_ports[parent_slave->address]);

                    // If parent has only one link (topology == 1), use parent's entry port
                    if (parent_slave->countOpenPorts() == 1)
                    {
                        parentport = entry_ports[parent_slave->address];
                    }

                    // Calculate delta times for propagation delay computation
                    nanoseconds entry_to_prev_delta = 0ns;  // Time from entry port to previous port on current slave (subtracted for children)
                    nanoseconds parent_prev_to_entry_delta = 0ns;  // Time from parent's previous port to parent's entry port (added for previous siblings)

                    // Time from parent port to previous port on parent (base propagation time)
                    nanoseconds parent_port_to_prev_delta = portTime(*parent_slave, parentport) -
                                                            portTime(*parent_slave, prevPort(*parent_slave, parentport));

                    // If current slave has children (topology > 1), subtract their delays
                    if (slave.countOpenPorts() > 1)
                    {
                        entry_to_prev_delta = portTime(slave, prevPort(slave, entryport)) -
                                              portTime(slave, entryport);
                    }

                    // We are only interested in positive difference
                    if (entry_to_prev_delta > parent_port_to_prev_delta)
                    {
                        entry_to_prev_delta = -entry_to_prev_delta;
                    }

                    // If current slave is not the first child of parent, add previous child's delays
                    // Check if there are other DC slaves that are direct children of the same parent
                    // and appear before current slave in the sequential order
                    bool is_first_child = true;
                    for (auto const& check_slave : slaves_)
                    {
                        // Stop when we reach current slave
                        if (&check_slave == &slave)
                        {
                            break;
                        }

                        // Check if this slave is a DC slave and a direct child of the same parent
                        if (check_slave.isDCSupport() and
                            topology[check_slave.address] == parent_slave->address)
                        {
                            is_first_child = false;
                            break;
                        }
                    }

                    if (not is_first_child)
                    {
                        parent_prev_to_entry_delta = portTime(*parent_slave, prevPort(*parent_slave, parentport)) -
                                                     portTime(*parent_slave, entry_ports[parent_slave->address]);
                    }
                    if (parent_prev_to_entry_delta < 0ns)
                    {
                        parent_prev_to_entry_delta = -parent_prev_to_entry_delta;
                    }

                    // Calculate current slave delay from delta times
                    // Assumption: forward delay equals return delay
                    slave.delay = ((parent_port_to_prev_delta - entry_to_prev_delta) / 2) +
                                  parent_prev_to_entry_delta +
                                  parent_slave->delay;
                }
                else
                {
                    // No DC parent found (connected directly to master or through non-DC slaves)
                    // Delay is 0 (or could be calculated differently if needed)
                    slave.delay = 0ns;
                }

                // Clear parenthold since this branch has DC slave
                parenthold = 0;
            }
            else
            {
                // Non-DC slave
                uint16_t parent_address = topology[slave.address];

                // If non-DC slave found on first position on branch, hold root parent
                if (parent_address != 0 and parent_address != topology[parent_address])
                {
                    Slave* parent = findSlaveByAddress(slaves_, parent_address);
                    if (parent and parent->countOpenPorts() > 2)
                    {
                        parenthold = parent_address;
                    }
                }

                // If branch has no DC slaves, consume port on root parent
                if (parenthold != 0 and slave.countOpenPorts() == 1)
                {
                    parentPort(consumed_ports[parenthold]);
                    parenthold = 0;
                }
            }
        }

        // Apply propagation delay to slaves
        for (auto& slave : slaves_)
        {
            if (slave.isDCSupport())
            {
                dc_info("DC slave %d delay is %ld from DC ref\n", slave.address, slave.delay.count());
                auto error = [](DatagramState const& state)
                {
                    THROW_ERROR_DATAGRAM("Error while applying slave DC delay", state);
                };

                auto process = [&slave](DatagramHeader const*, uint8_t const*, uint16_t wkc)
                {
                    if (wkc != 1)
                    {
                        return DatagramState::INVALID_WKC;
                    }

                    return DatagramState::OK;
                };

                uint32_t raw_delay = static_cast<uint32_t>(slave.delay.count());
                link_->addDatagram(Command::FPWR, createAddress(slave.address, reg::DC_SYSTEM_TIME_DELAY), &raw_delay, sizeof(raw_delay), process, error);
            }
        }
        link_->processDatagrams();
    }


    void Bus::applyMasterTime()
    {
        auto error = [](DatagramState const& state)
        {
            THROW_ERROR_DATAGRAM("Error while applying slave DC offset", state);
        };

        auto process = [](DatagramHeader const*, uint8_t const*, uint16_t wkc)
        {
            if (wkc != 1)
            {
                return DatagramState::INVALID_WKC;
            }

            return DatagramState::OK;
        };

        for (auto& slave : slaves_)
        {
            uint64_t raw_delay = static_cast<uint32_t>(slave.dc_time_offset.count());
            if (raw_delay == 0)
            {
                continue;
            }
            dc_info("DC slave %d time offset is %ld from DC ref\n", slave.address, slave.dc_time_offset.count());
            link_->addDatagram(Command::FPWR, createAddress(slave.address, reg::DC_SYSTEM_TIME_OFFSET), &raw_delay, sizeof(uint64_t), process, error);
        }
        link_->processDatagrams();
    }


    void Bus::sendDriftCompensation(std::function<void(DatagramState const&)> const& error)
    {
        auto process = [&](DatagramHeader const*, uint8_t const*, uint16_t wkc)
        {
            if (wkc == 0)
            {
                dc_error("Invalid working counter:  %" PRIu16 "\n", wkc);
                return DatagramState::INVALID_WKC;
            }

            return DatagramState::OK;
        };

        nanoseconds now = since_ecat_epoch();
        uint64_t raw_now = now.count();
        link_->addDatagram(Command::FPWR, createAddress(dc_slave_->address, reg::DC_SYSTEM_TIME), &raw_now, sizeof(uint64_t), process, error);
        link_->addDatagram(Command::FRMW, createAddress(dc_slave_->address, reg::DC_SYSTEM_TIME), nullptr,  sizeof(uint64_t), process, error);
    }
}
