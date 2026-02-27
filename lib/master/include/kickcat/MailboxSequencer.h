#ifndef KICKCAT_MAILBOX_SEQUENCER_H
#define KICKCAT_MAILBOX_SEQUENCER_H

#include "Bus.h"

namespace kickcat
{
    /// \brief Spreads mailbox operations across main loop iterations to limit network load.
    ///
    /// Instead of calling all four mailbox operations every cycle (which may overload
    /// the EtherCAT network), this helper executes one operation per call, cycling through:
    ///   1. sendMailboxesReadChecks  - check if slaves have responses to read
    ///   2. sendReadMessages         - read available responses
    ///   3. sendMailboxesWriteChecks - check if slave mailboxes have space
    ///   4. sendWriteMessages        - send queued requests
    ///
    /// Reads are prioritized over writes: reading responses first unblocks the application
    /// faster and frees the slave outbox so it can process new requests sooner.
    ///
    /// The period parameter controls how often the sequencer advances: a period of 1 means
    /// one mailbox step per call (full cycle in 4 calls), a period of 2 means one step every
    /// two calls (full cycle in 8 calls), etc.
    ///
    /// \code
    ///   auto msg = slave.mailbox.createSDO(index, subindex, CA, request, &data, &size, timeout);
    ///   MailboxSequencer sequencer(bus);
    ///
    ///   while (running)
    ///   {
    ///       bus.sendLogicalRead(error);
    ///       bus.sendLogicalWrite(error);
    ///       sequencer.step(error);
    ///       bus.finalizeDatagrams();
    ///       bus.processAwaitingFrames();
    ///
    ///       if (msg->status() != MessageStatus::RUNNING)
    ///       {
    ///           // message completed (check for SUCCESS, TIMEDOUT, etc.)
    ///       }
    ///   }
    /// \endcode
    class MailboxSequencer
    {
    public:
        /// \param bus     Reference to the bus managing the EtherCAT network
        /// \param period  Number of main loop iterations between each mailbox step (1 = every iteration)
        MailboxSequencer(Bus& bus, int32_t period = 1);

        /// \brief Execute one mailbox operation per 'period' calls, round-robin.
        /// Must be called once per main loop iteration, before finalizeDatagrams/processAwaitingFrames.
        void step(std::function<void(DatagramState const&)> const& error);

    private:
        Bus& bus_;
        int32_t period_;
        int32_t counter_{0};
        int32_t phase_{0};

        static constexpr int32_t PHASES = 4;
    };
}

#endif
