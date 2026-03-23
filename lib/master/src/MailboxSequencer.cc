#include "MailboxSequencer.h"

namespace kickcat
{
    MailboxSequencer::MailboxSequencer(Bus& bus, int32_t period)
        : bus_(bus)
        , period_(period)
    {
    }

    void MailboxSequencer::step(std::function<void(DatagramState const&)> const& error)
    {
        if (++counter_ < period_)
        {
            return;
        }
        counter_ = 0;

        auto mode = bus_.mailboxStatusFMMUMode();

        // When mailbox status is mapped via FMMU, the check is already done in the
        // LRD/LRW callback — skip the FPRD phase to stay reactive.
        if ((phase_ == 0 and (mode & MailboxStatusFMMU::READ_CHECK))
         or (phase_ == 2 and (mode & MailboxStatusFMMU::WRITE_CHECK)))
        {
            phase_ = (phase_ + 1) % PHASES;
        }

        switch (phase_)
        {
            case 0: { bus_.sendMailboxesReadChecks(error);  break; }
            case 1: { bus_.sendReadMessages(error);         break; }
            case 2: { bus_.sendMailboxesWriteChecks(error);  break; }
            case 3: { bus_.sendWriteMessages(error);         break; }
        }

        phase_ = (phase_ + 1) % PHASES;
    }
}
