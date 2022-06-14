#ifndef KICKCAT_LINK_H
#define KICKCAT_LINK_H

#include <array>
#include <memory>
#include <functional>

#include "KickCAT.h"
#include "Frame.h"

namespace kickcat
{
    class AbstractSocket;

    /// \brief Handle link layer
    /// \details This class is responsible to handle frames and datagrams on the link layers:
    ///           - associate an id to each datagram to call the associate callback later without depending on the read order
    ///           - handle link redundancy (TODO)
    class Link
    {
    public:
        Link(std::shared_ptr<AbstractSocket> socket);
        ~Link() = default;

        /// \brief helper for trivial access (i.e. most of the init bus frames)
        void writeThenRead(Frame& frame);

        void addDatagram(enum Command command, uint32_t address, void const* data, uint16_t data_size,
                         std::function<DatagramState(DatagramHeader const*, uint8_t const* data, uint16_t wkc)> const& process,
                         std::function<void(DatagramState const& state)> const& error);
        template<typename T>
        void addDatagram(enum Command command, uint32_t address, T const& data,
                         std::function<DatagramState(DatagramHeader const*, uint8_t const* data, uint16_t wkc)> const& process,
                         std::function<void(DatagramState const& state)> const& error)
        {
            addDatagram(command, address, &data, sizeof(data), process, error);
        }

        void finalizeDatagrams();
        void processDatagrams();

    private:
        void sendFrame();

        std::shared_ptr<AbstractSocket> socket_;
        uint8_t index_queue_{0};
        uint8_t index_head_{0};
        uint8_t sent_frame_{0};
        Frame frame_{PRIMARY_IF_MAC};

        struct Callbacks
        {
            DatagramState status{DatagramState::LOST};
            std::function<DatagramState(DatagramHeader const*, uint8_t const* data, uint16_t wkc)> process;
            std::function<void(DatagramState const& state)> error;
        };
        std::array<Callbacks, 256> callbacks_{};
    };
}

#endif
