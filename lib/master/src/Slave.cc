#include <algorithm>
#include <cstdio>

#include "Slave.h"
#include "debug.h"


namespace kickcat
{

    void Slave::parseSII(uint8_t const* data, std::size_t size)
    {
        sii.parse(data, size);

        mailbox.recv_offset = sii.info.standard_recv_mbx_offset;
        mailbox.recv_size   = sii.info.standard_recv_mbx_size;
        mailbox.send_offset = sii.info.standard_send_mbx_offset;
        mailbox.send_size   = sii.info.standard_send_mbx_size;
        mailbox_bootstrap.recv_offset = sii.info.bootstrap_recv_mbx_offset;
        mailbox_bootstrap.recv_size   = sii.info.bootstrap_recv_mbx_size;
        mailbox_bootstrap.send_offset = sii.info.bootstrap_send_mbx_offset;
        mailbox_bootstrap.send_size   = sii.info.bootstrap_send_mbx_size;
    }

    std::string Slave::name() const
    {
        auto s = sii.getString(sii.general.device_name_id);
        if (not s.empty())
        {
            return std::string{s};
        }

        // Fallback: station-address label so operators can correlate with the wire.
        char buf[16];
        std::snprintf(buf, sizeof(buf), "Slave @0x%04X", address);
        return buf;
    }


    std::string Slave::type() const
    {
        return std::string{sii.getString(sii.general.device_order_id)};
    }


    ErrorCounters const& Slave::errorCounters() const
    {
        return error_counters;
    }


    int Slave::computeRelativeErrorCounters()
    {
        int current_error_sum = computeErrorCounters();
        int delta_error_sum = current_error_sum - previous_errors_sum;

        previous_errors_sum = current_error_sum;
        return delta_error_sum;
    }


    bool Slave::checkAbsoluteErrorCounters(int max_absolute_errors)
    {
        return computeErrorCounters() > max_absolute_errors;
    }


    int Slave::computeErrorCounters() const
    {
        int sum = 0;
        for (int32_t i = 0; i < 4; ++i)
        {
            sum += error_counters.rx[i].invalid_frame;
            sum += error_counters.rx[i].physical_layer;
            sum += error_counters.lost_link[i];
        }

        return sum;
    }

    int Slave::countOpenPorts() const
    {
        return dl_status.COM_port0 + dl_status.COM_port1 + dl_status.COM_port2 + dl_status.COM_port3;
    }

    bool Slave::isDCSupport() const
    {
        return esc.features & ESC::feature::DC_AVAILABLE;
    }

    nanoseconds Slave::portTime(uint8_t port)
    {
        if (port >= 4)
        {
            THROW_SYSTEM_ERROR_CODE("port value shall be < 4", EINVAL);
        }

        return dc_received_time[port];
    }

    nanoseconds Slave::portDelta(uint8_t port_a, uint8_t port_b)
    {
        constexpr nanoseconds WRAP_32 = nanoseconds(uint64_t(1) << 32);

        nanoseconds delta = portTime(port_a) - portTime(port_b);
        if (delta < 0ns)
        {
            delta += WRAP_32;
        }
        return delta;
    }

    uint8_t Slave::activePorts()
    {
        uint8_t active = 0;
        if (dl_status.PL_port0) { active |= PORT0; }
        if (dl_status.PL_port1) { active |= PORT1; }
        if (dl_status.PL_port2) { active |= PORT2; }
        if (dl_status.PL_port3) { active |= PORT3; }
        return active;
    }

    // Calculate previous active port (order: 0 - 3 - 1 - 2)
    uint8_t Slave::prevPort(uint8_t port)
    {
        uint8_t previous_port = port;
        uint8_t active = activePorts();

        switch (port)
        {
            case 0:
            {
                if (active & PORT2)      { previous_port = 2; }
                else if (active & PORT1) { previous_port = 1; }
                else if (active & PORT3) { previous_port = 3; }
                break;
            }
            case 1:
            {
                if (active & PORT3)      { previous_port = 3; }
                else if (active & PORT0) { previous_port = 0; }
                else if (active & PORT2) { previous_port = 2; }
                break;
            }
            case 2:
            {
                if (active & PORT1)      { previous_port = 1; }
                else if (active & PORT3) { previous_port = 3; }
                else if (active & PORT0) { previous_port = 0; }
                break;
            }
            case 3:
            {
                if (active & PORT0)      { previous_port = 0; }
                else if (active & PORT2) { previous_port = 2; }
                else if (active & PORT1) { previous_port = 1; }
                break;
            }
        }
        return previous_port;
    }

    Slave* findSlaveByAddress(std::vector<Slave>& slaves, uint16_t address)
    {
        auto it = std::find_if(slaves.begin(), slaves.end(),
                               [address](Slave const& slave) { return slave.address == address; });
        if (it != slaves.end())
        {
            return &(*it);
        }

        return nullptr;
    }
}
