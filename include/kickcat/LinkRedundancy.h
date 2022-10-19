#ifndef KICKCAT_LINK_REDUNDANCY_H
#define KICKCAT_LINK_REDUNDANCY_H

#include <array>
#include <memory>
#include <functional>

#include "KickCAT.h"
#include "Frame.h"
#include "AbstractLink.h"


namespace kickcat
{
    class AbstractSocket;
    class LinkRedundancy : public AbstractLink
    {
    public:
        LinkRedundancy(std::shared_ptr<AbstractSocket> socket_nominal,
                       std::shared_ptr<AbstractSocket> socket_redundancy,
                       std::function<void(void)> const& redundancyActivatedCallback,
                       uint8_t const src_mac_nominal[MAC_SIZE] = PRIMARY_IF_MAC,
                       uint8_t const src_mac_redundancy[MAC_SIZE] = SECONDARY_IF_MAC);
        ~LinkRedundancy() = default;

        /// \brief Since this method is only used for non real time operation, the redundancy mechanism used is slower
        ///        but allow to keep a consistent interface between with and without redundancy.
        void writeThenRead(Frame& frame) override;

        void checkRedundancyNeeded();
    friend class LinkRedTest;
    private:
        void read() override;
        void sendFrame() override;
        bool isDatagramAvailable() override;
        std::tuple<DatagramHeader const*, uint8_t*, uint16_t> nextDatagram() override;
        void addDatagramToFrame(uint8_t index, enum Command command, uint32_t address, void const* data, uint16_t data_size) override;
        void resetFrameContext() override;


        std::function<void(void)> redundancyActivatedCallback_;

        std::shared_ptr<AbstractSocket> socket_nominal_;
        std::shared_ptr<AbstractSocket> socket_redundancy_;

        Frame frame_redundancy_{};
        uint8_t src_mac_redundancy_[MAC_SIZE];


        bool is_redundancy_activated_{false};
    };
}

#endif
