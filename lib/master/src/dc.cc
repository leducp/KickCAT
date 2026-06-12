#include <inttypes.h>
#include <unordered_map>

#include "Bus.h"
#include "debug.h"
#include "Diagnostics.h"
#include "helpers.h"

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
            }
        }
        if (dc_slave_ == nullptr)
        {
            THROW_ERROR("No dc slave found");
        }
        dc_info("DC reference slave is %d\n", dc_slave_->address);

        //-------- Apply propagation delay and system time offset -------//
        // Trigger latch time on received registers whenever the frame pass by the port 0-3.
        // Master time is sampled before sending the latch frame: the offsets are computed
        // against the latched receive times, sampling after the round trip would inflate
        // every offset by one full round trip.
        uint8_t dummy = 0;
        nanoseconds now = since_ecat_epoch();
        broadcastWrite(reg::DC_RECEIVED_TIME, &dummy, 1);

        fetchReceivedTimes();
        computePropagationDelay(now);
        applyMasterTime();

        //------------------ Static drift compensation ------------------//
        // 1. set filter depths, then reset the time control loop
        //    - System Time Diff filter depth (0x0934) = 0x00 (no filtering on diff)
        //    - Speed Counter filter depth (0x0935)    = 0x0c (12 -> 2^12 IIR averaging)
        //    A higher speed counter filter depth damps the PLL, preventing oscillation
        //    when the initial system time difference is large (e.g. branch 2 in Y-topology).
        //    Depths are written first: on several ESCs a depth change only takes effect
        //    once the speed counter start (0x0930) is rewritten, which resets the filters.
        uint16_t filter_settings = 0x0c00;
        broadcastWrite(reg::DC_TIME_FILTER, &filter_settings, sizeof(filter_settings));
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

        //----------------------- Apply cycle time ----------------------//
        uint32_t cycle_time_raw = static_cast<uint32_t>(cycle_time.count());
        broadcastWrite(reg::DC_SYNC0_CYCLE_TIME, &cycle_time_raw, 4);

        //------------------------ Set start time -----------------------//
        // 1. Get current network time to compute start time (relative to absolute)
        uint64_t network_time = 0;
        bool network_time_acquired = false;
        auto process = [&network_time, &network_time_acquired](DatagramHeader const*, uint8_t const* data, uint16_t wkc)
        {
            if (wkc != 1)
            {
                return DatagramState::INVALID_WKC;
            }

            std::memcpy(&network_time, data, 8);
            network_time_acquired = true;
            return DatagramState::OK;
        };

        // A lost frame here would silently yield a bogus SYNC0 start time: retry a
        // bounded number of times before giving up loudly.
        DatagramState last_state = DatagramState::LOST;
        auto network_time_error = [&last_state](DatagramState const& state)
        {
            last_state = state;
        };

        constexpr int NETWORK_TIME_READ_ATTEMPTS = 3;
        for (int attempt = 0; (attempt < NETWORK_TIME_READ_ATTEMPTS) and (not network_time_acquired); ++attempt)
        {
            link_->addDatagram(Command::FPRD, createAddress(dc_slave_->address, reg::DC_SYSTEM_TIME), nullptr, 8, process, network_time_error);
            link_->processDatagrams();
        }
        if (not network_time_acquired)
        {
            THROW_ERROR_DATAGRAM("Error while reading DC system time to compute the SYNC0 start time", last_state);
        }

        // 2. Convert relative start time to absolute, round it to cycle time and apply a delay and shift in the cycle
        nanoseconds start_time = (nanoseconds(network_time) / cycle_time) * cycle_time + cycle_time + shift_cycle + start_delay;
        uint64_t start_time_raw = start_time.count();

        // 3. Apply start time
        broadcastWrite(reg::DC_START_TIME, &start_time_raw, sizeof(start_time_raw));

        //---------------------- Apply SYNC config ----------------------//
        // Pulse length (0x0982) is NOT written: it is r/- from ECAT on all ESCs
        // (datasheet sec2 2.15.4.2), the EEPROM owns it.

        // TODO: use a config in the slave to select the sync method
        uint8_t enable_dc_sync0 = 0x3;
        broadcastWrite(reg::DC_SYNC_ACTIVATION, &enable_dc_sync0, 1);

        return to_unix_epoch(start_time - start_delay - shift_cycle);
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
        // Find and consume an unconsumed port on parent (search order: 3 - 1 - 2 - 0)
        uint8_t parentPort(uint8_t& consumed_ports)
        {
            uint8_t parent_port = 0;
            uint8_t b = consumed_ports;

            if (b & Slave::PORT3)
            {
                parent_port = 3;
                b &= ~Slave::PORT3;
            }
            else if (b & Slave::PORT1)
            {
                parent_port = 1;
                b &= ~Slave::PORT1;
            }
            else if (b & Slave::PORT2)
            {
                parent_port = 2;
                b &= ~Slave::PORT2;
            }
            else if (b & Slave::PORT0)
            {
                parent_port = 0;
                b &= ~Slave::PORT0;
            }

            consumed_ports = b;
            return parent_port;
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
            consumed_ports[slave.address] = slave.activePorts();
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
                uint8_t active = slave.activePorts();
                int8_t active_port_count = 0;
                int8_t active_port_numbers[4];
                nanoseconds port_timestamps[4];

                // Build list of active ports and their timestamps (order: 0, 3, 1, 2)
                if (active & Slave::PORT0)
                {
                    active_port_numbers[active_port_count] = 0;
                    port_timestamps[active_port_count] = slave.portTime(0);
                    active_port_count++;
                }
                if (active & Slave::PORT3)
                {
                    active_port_numbers[active_port_count] = 3;
                    port_timestamps[active_port_count] = slave.portTime(3);
                    active_port_count++;
                }
                if (active & Slave::PORT1)
                {
                    active_port_numbers[active_port_count] = 1;
                    port_timestamps[active_port_count] = slave.portTime(1);
                    active_port_count++;
                }
                if (active & Slave::PORT2)
                {
                    active_port_numbers[active_port_count] = 2;
                    port_timestamps[active_port_count] = slave.portTime(2);
                    active_port_count++;
                }

                // Entry port is port with the lowest timestamp
                uint8_t entry_port = 0;
                if ((active_port_count > 1) and (port_timestamps[1] < port_timestamps[entry_port]))
                {
                    entry_port = 1;
                }
                if ((active_port_count > 2) and (port_timestamps[2] < port_timestamps[entry_port]))
                {
                    entry_port = 2;
                }
                if ((active_port_count > 3) and (port_timestamps[3] < port_timestamps[entry_port]))
                {
                    entry_port = 3;
                }
                entry_port = active_port_numbers[entry_port];
                entry_ports[slave.address] = entry_port;

                // Consume entry port from consumed ports
                consumed_ports[slave.address] &= ~(1 << entry_port);

                // Find DC parent (walk up parent chain until we find a DC slave or master)
                Slave* parent_slave = nullptr;
                uint16_t parent_address = topology[slave.address];

                // Walk up parent chain until we find a DC slave or master
                // Note: a slave that is its own parent (topology[address] == address) is connected to master
                // Skip self to avoid finding ourselves as our own parent
                if (parent_address != slave.address)
                {
                    while (parent_address != 0)
                    {
                        Slave* candidate = findSlaveByAddress(slaves_, parent_address);
                        if (candidate and candidate->isDCSupport())
                        {
                            parent_slave = candidate;
                            break;
                        }

                        if (parent_address == topology[parent_address])
                        {
                            break;
                        }

                        parent_address = topology[parent_address];
                    }
                }

                // Only calculate propagation delay if slave has a DC parent and is not the DC reference
                if (parent_slave != nullptr and &slave != dc_slave_)
                {
                    // Find port on parent this slave is connected to
                    uint8_t parent_port = parentPort(consumed_ports[parent_slave->address]);

                    // If parent has only one link (topology == 1), use parent's entry port
                    if (parent_slave->countOpenPorts() == 1)
                    {
                        parent_port = entry_ports[parent_slave->address];
                    }

                    nanoseconds entry_to_prev_delta = 0ns;
                    nanoseconds parent_prev_to_entry_delta = 0ns;

                    // Round-trip time through the child branch as seen from the parent
                    nanoseconds parent_port_to_prev_delta = parent_slave->portDelta(parent_port,
                                                                                    parent_slave->prevPort(parent_port));

                    // Round-trip time through this slave's own children (0 if leaf node)
                    if (slave.countOpenPorts() > 1)
                    {
                        entry_to_prev_delta = slave.portDelta(slave.prevPort(entry_port), entry_port);
                    }

                    // Check if this slave is NOT the first child on this parent branch
                    bool is_first_child = true;

                    size_t current_index = 0;
                    size_t parent_index = 0;
                    for (size_t i = 0; i < slaves_.size(); ++i)
                    {
                        if (&slaves_[i] == &slave)
                        {
                            current_index = i;
                        }
                        if (&slaves_[i] == parent_slave)
                        {
                            parent_index = i;
                        }
                    }

                    if (current_index > parent_index + 1)
                    {
                        for (size_t i = parent_index + 1; i < current_index; ++i)
                        {
                            if (slaves_[i].isDCSupport() and
                                topology[slaves_[i].address] == parent_slave->address)
                            {
                                is_first_child = false;
                                break;
                            }
                        }
                    }

                    if (not is_first_child)
                    {
                        parent_prev_to_entry_delta = parent_slave->portDelta(parent_slave->prevPort(parent_port),
                                                                             entry_ports[parent_slave->address]);
                    }

                    // ESC-specific difference between processing and forwarding delay.
                    // Should be parsed from the ESI per slave - 0 as fallback.
                    constexpr nanoseconds tDiff = 0ns;

                    // If child's internal round-trip exceeds parent's view of the branch,
                    // the subtraction would yield a negative delay - negate dt1 to compensate.
                    if (entry_to_prev_delta > parent_port_to_prev_delta)
                    {
                        entry_to_prev_delta = -entry_to_prev_delta;
                    }

                    // Spec formula: tAB = ((tA1 - tA0) - (tB1 - tB0) + tDiff) / 2
                    nanoseconds prop_delay = (parent_port_to_prev_delta - entry_to_prev_delta + tDiff) / 2;
                    slave.delay = prop_delay + parent_prev_to_entry_delta + parent_slave->delay;
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
            if (not slave.isDCSupport())
            {
                continue;
            }
            int64_t raw_dc_time_diff = slave.dc_time_offset.count();
            dc_info("DC slave %d time offset is %ld from DC ref\n", slave.address, slave.dc_time_offset.count());
            link_->addDatagram(Command::FPWR, createAddress(slave.address, reg::DC_SYSTEM_TIME_OFFSET), &raw_dc_time_diff, sizeof(raw_dc_time_diff), process, error);
        }
        link_->processDatagrams();
    }


    void Bus::sendDriftCompensation(std::function<void(DatagramState const&)> const& error)
    {
        auto process = [](DatagramHeader const*, uint8_t const*, uint16_t wkc)
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

        // Slaves not matching the FRMW address write the payload to their own clock. Pre-fill it
        // with master time: slaves cut from the reference by a ring split track master time instead
        // of being slammed to zero by the redundancy frame copy.
        link_->addDatagram(Command::FRMW, createAddress(dc_slave_->address, reg::DC_SYSTEM_TIME), &raw_now, sizeof(uint64_t), process, error);

        // Age of the drift timestamp when the caller flushes the datagrams: the payload
        // is already this stale when it leaves the master (live ripple investigation).
        // Compile-gated, argument not evaluated when DEBUG_DC_INFO is off.
        dc_info("DC drift timestamp age: %" PRId64 " ns\n", (since_ecat_epoch() - now).count());
    }


    bool Bus::isDCSynchronized(nanoseconds threshold, bool log_all)
    {
        if (dc_slave_ == nullptr)
        {
            return false;
        }

        struct DCSlaveSync
        {
            Slave* slave;
            uint32_t time_diff_raw{0};
        };
        std::vector<DCSlaveSync> dc_syncs;

        // process callbacks keep references into dc_syncs: no reallocation allowed past this point
        size_t dc_slaves_count = 0;
        for (auto const& slave : slaves_)
        {
            if (slave.isDCSupport() and &slave != dc_slave_)
            {
                ++dc_slaves_count;
            }
        }
        dc_syncs.reserve(dc_slaves_count);

        bool datagram_error = false;
        auto error = [&datagram_error](DatagramState const& state)
        {
            dc_error("DC sync status: datagram %s\n", toString(state));
            datagram_error = true;
        };

        for (auto& slave : slaves_)
        {
            if (not slave.isDCSupport() or &slave == dc_slave_)
            {
                continue;
            }

            dc_syncs.push_back({&slave, 0});
            auto& sync = dc_syncs.back();

            auto process = [&sync](DatagramHeader const*, uint8_t const* data, uint16_t wkc)
            {
                if (wkc != 1)
                {
                    return DatagramState::INVALID_WKC;
                }
                std::memcpy(&sync.time_diff_raw, data, sizeof(sync.time_diff_raw));
                return DatagramState::OK;
            };

            link_->addDatagram(Command::FPRD, createAddress(slave.address, reg::DC_SYSTEM_TIME_DIFF), nullptr, sizeof(sync.time_diff_raw), process, error);
        }
        link_->processDatagrams();

        bool synchronized = not datagram_error;
        for (auto const& sync : dc_syncs)
        {
            // DC_SYSTEM_TIME_DIFF uses sign-magnitude encoding (bit 31 = sign, bits 30:0 = magnitude)
            nanoseconds drift = nanoseconds(sync.time_diff_raw & 0x7FFFFFFF);
            if (drift > threshold)
            {
                dc_warning("DC slave %d NOT synchronized: drift = %ld ns (threshold = %ld ns)\n",
                           sync.slave->address, drift.count(), threshold.count());
                synchronized = false;
            }
            else if (log_all)
            {
                dc_info("DC slave %d synchronized: drift = %ld ns\n",
                        sync.slave->address, drift.count());
            }
        }

        return synchronized;
    }
}
