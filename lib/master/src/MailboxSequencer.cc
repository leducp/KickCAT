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

        switch (phase_)
        {
            case 0: bus_.sendMailboxesReadChecks(error);  break;
            case 1: bus_.sendReadMessages(error);         break;
            case 2: bus_.sendMailboxesWriteChecks(error);  break;
            case 3: bus_.sendWriteMessages(error);         break;
        }

        phase_ = (phase_ + 1) % PHASES;
    }
}
