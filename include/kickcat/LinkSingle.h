#ifndef KICKCAT_LINK_H
#define KICKCAT_LINK_H

#include <array>
#include <memory>
#include <functional>

#include "AbstractLink.h"

namespace kickcat
{
    class AbstractSocket;

    class LinkSingle : public AbstractLink
    {
    public:
        LinkSingle(std::shared_ptr<AbstractSocket> socket, uint8_t const src_mac[MAC_SIZE] = PRIMARY_IF_MAC);
        ~LinkSingle() = default;

        void writeThenRead(Frame& frame) override;

    private:
        void sendFrame() override;
        void read() override;
        bool isDatagramAvailable() override;
        std::tuple<DatagramHeader const*, uint8_t*, uint16_t> nextDatagram() override;
        void resetFrameContext() override;
        void addDatagramToFrame(uint8_t index, enum Command command, uint32_t address, void const* data, uint16_t data_size) override;

        std::shared_ptr<AbstractSocket> socket_nominal_;
    };
}

#endif