#include <inttypes.h>

#include "debug.h"
#include "Bus.h"

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


    void Bus::computePropagationDelay(nanoseconds master_time)
    {
        // !!! Assume a linear topology for now !!!

        dc_slave_->dc_time_offset = master_time - dc_slave_->dc_ecat_received_time;

        // Start from the DC slave (the first one on the network that have DC capability)
        auto current_dc_slave = dc_slave_;
        current_dc_slave->delay = 0ns;

        auto next_dc_slave = [&](Slave* current)
        {
            int id = current->address - 1001; //TODO create a constant for the slave address offset

            for (auto it = slaves_.begin() + id + 1; it != slaves_.end(); ++it)
            {
                if (it->esc.features & ESC::feature::DC_AVAILABLE)
                {
                    return &(*it);
                }
            }

            // No DC slave past the current one
            return current;
        };

        while (true)
        {
            auto next = next_dc_slave(current_dc_slave);
            if (next->address == current_dc_slave->address)
            {
                // End of scan
                break;
            }

            // Compute time offset from ref clock
            next->dc_time_offset = master_time - next->dc_ecat_received_time;

            // Set the current delay to next as an offset
            next->delay = current_dc_slave->delay;

            // !!! TODO: handle overflow of 32bits registers !!!
            nanoseconds delta_current = current_dc_slave->dc_received_time[1] - current_dc_slave->dc_received_time[0];
            nanoseconds delta_next    = next->dc_received_time[1]             - next->dc_received_time[0];

            if (next->dl_status.PL_port1 == 0)
            {
                // End of line
                next->delay += delta_current / 2;
            }
            else
            {

                next->delay += delta_current - delta_next;
            }
            current_dc_slave = next;
        }

        // Apply propagation delay to slaves
        for (auto& slave : slaves_)
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
