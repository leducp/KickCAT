#ifndef KICKCAT_ENI_SEQUENCER_H
#define KICKCAT_ENI_SEQUENCER_H

#include <functional>
#include <memory>
#include <optional>
#include <vector>

#include "kickcat/AbstractLink.h"
#include "kickcat/Bus.h"
#include "kickcat/ENI/Config.h"

namespace kickcat::ENI
{
    /// \brief Configure a bus through the state transitions described by an ENI.
    /// \details Startup commands program the slaves; their SII data is checked against the ENI.
    ///          Mailbox and process data layouts are read back from the programmed registers.
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

        /// \return the process image size, known once the ENI programmed the FMMUs (SAFE_OP and above)
        std::size_t processImageSize() const;

        /// \brief Bind slave input/output to iomap, see Bus::mapProcessImage. Needs SAFE_OP or above,
        ///        and no Bus::configureMailboxStatusCheck(): an ENI does not program its FMMUs.
        void mapProcessImage(uint8_t* iomap, std::size_t iomap_size);

        /// \return the DC cycle and shift shared by every DC slave of the ENI, nullopt without DC
        std::optional<nanoseconds> cycleTime() const { return cycle_time_; }
        std::optional<nanoseconds> shiftTime() const { return shift_time_; }

        /// \return the ENI slave driven at a bus position
        ENI::Slave const& slaveAt(std::size_t position) const;

    private:
        void detect();
        void transition(State from, State to, std::function<void()> const& background);
        void execute(std::string const& who, InitCmd const& cmd, std::function<void()> const& background);
        void sendCoE(transition::Type t, std::function<void()> const& background);
        void attachSlaves();
        void checkIdentity(std::size_t position);
        void checkMailbox(std::size_t position);
        void checkIdentification();
        void readProcessDataLayout();
        std::vector<uint8_t> read(std::size_t position, uint16_t ado, uint16_t size);

        Bus& bus_;
        std::shared_ptr<AbstractLink> link_;
        Config const& config_;

        std::vector<std::size_t> eni_of_position_;
        std::vector<std::size_t> position_of_eni_;
        std::optional<nanoseconds> cycle_time_;
        std::optional<nanoseconds> shift_time_;

        State state_ = State::INIT;
        bool detected_ = false;
        bool handlers_attached_ = false;
        bool layout_known_ = false;
    };
}

#endif
