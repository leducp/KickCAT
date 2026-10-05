#ifndef KICKCAT_ENI_SEQUENCER_H
#define KICKCAT_ENI_SEQUENCER_H

#include <functional>
#include <memory>
#include <vector>

#include "kickcat/AbstractLink.h"
#include "kickcat/Bus.h"
#include "kickcat/ENI/Config.h"

namespace kickcat::ENI
{
    /// \brief Configure a bus through the state transitions described by an ENI.
    /// \details Startup commands program the slaves; their SII data is checked against the ENI.
    ///          The mailbox layout is read back from the programmed registers.
    class Sequencer
    {
    public:
        /// \param link the link given to bus: raw InitCmds are sent through it
        /// \param config must outlive the sequencer
        Sequencer(Bus& bus, std::shared_ptr<AbstractLink> link, Config const& config);

        /// \brief Run every ENI transition from the current state to target.
        /// \details Up: one state at a time (INIT -> PRE_OP -> SAFE_OP -> OPERATIONAL). Down: the direct transition.
        ///          The first call detects the slaves and checks them against the ENI.
        /// \param background called while a Validate polls, e.g. to cycle the process data before OPERATIONAL
        void requestState(State target, std::function<void()> const& background = [](){});
        State state() const { return state_; }

        /// \return the ENI slave driven at a bus position
        ENI::Slave const& slaveAt(std::size_t position) const;

    private:
        void detect();
        void transition(State from, State to, std::function<void()> const& background);
        void execute(std::string const& who, InitCmd const& cmd, std::function<void()> const& background);
        void sendCoE(transition::Type t, std::function<void()> const& background);
        void identifySlaves();
        void attachMailboxes();
        void checkIdentity(std::size_t position);
        void checkMailbox(std::size_t position);
        void checkIdentification();

        Bus& bus_;
        std::shared_ptr<AbstractLink> link_;
        Config const& config_;

        std::vector<std::size_t> eni_of_position_;
        std::vector<std::size_t> position_of_eni_;

        State state_ = State::INIT;
        bool detected_ = false;
        bool handlers_attached_ = false;
    };
}

#endif
