#include <iomanip>

#include "Slave.h"
#include "debug.h"


namespace kickcat
{

    void Slave::parseSII()
    {
        sii.parse();

        mailbox.recv_offset = sii.mailbox_recv_offset;
        mailbox.recv_size = sii.mailbox_recv_size;
        mailbox.send_offset= sii.mailbox_send_offset;
        mailbox.send_size= sii.mailbox_send_size;
        mailbox_bootstrap.recv_offset = sii.mailboxBootstrap_recv_offset;
        mailbox_bootstrap.recv_size = sii.mailboxBootstrap_recv_size;
        mailbox_bootstrap.send_offset= sii.mailboxBootstrap_send_offset;
        mailbox_bootstrap.send_size= sii.mailboxBootstrap_send_size;
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
        return  dl_status.PL_port0 +
                dl_status.PL_port1 +
                dl_status.PL_port2 +
                dl_status.PL_port3;
    }
}
