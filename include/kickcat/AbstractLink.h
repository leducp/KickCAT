#ifndef KICKCAT_ABSTRACT_LINK_H
#define KICKCAT_ABSTRACT_LINK_H

#include <array>
#include <memory>
#include <functional>

#include "KickCAT.h"
#include "Frame.h"

#include "AbstractSocket.h"

namespace kickcat
{
    /// \brief Handle link layer
    /// \details This class is responsible to handle frames and datagrams on the link layers:
    ///           - associate an id to each datagram to call the associate callback later without depending on the read order
    class AbstractLink
    {
    public:
        AbstractLink() = default;
        virtual ~AbstractLink() = default;

        /// \brief helper for trivial access (i.e. most of the init bus frames)
        virtual void writeThenRead(Frame& frame) = 0;

        virtual void addDatagram(enum Command command, uint32_t address, void const* data, uint16_t data_size,
                         std::function<DatagramState(DatagramHeader const*, uint8_t const* data, uint16_t wkc)> const& process,
                         std::function<void(DatagramState const& state)> const& error);
        template<typename T>
        void addDatagram(enum Command command, uint32_t address, T const& data,
                         std::function<DatagramState(DatagramHeader const*, uint8_t const* data, uint16_t wkc)> const& process,
                         std::function<void(DatagramState const& state)> const& error)
        {
            addDatagram(command, address, &data, sizeof(data), process, error);
        }

        virtual void finalizeDatagrams();
        virtual void processDatagrams();

        bool isRedundancyActivated() {return false;};

        void readFrame(std::shared_ptr<AbstractSocket> socket, Frame& frame);
        void writeFrame(std::shared_ptr<AbstractSocket> socket, Frame& frame, uint8_t const src_mac[MAC_SIZE]);

    protected:
        uint8_t index_queue_{0};
        uint8_t index_head_{0};
        uint8_t sent_frame_{0};

        struct Callbacks
        {
            DatagramState status{DatagramState::LOST};
            std::function<DatagramState(DatagramHeader const*, uint8_t const* data, uint16_t wkc)> process;
            std::function<void(DatagramState const& state)> error;
        };
        std::array<Callbacks, 256> callbacks_{};

        Frame frame_nominal_{};
        uint8_t src_mac_nominal_[MAC_SIZE];

    private:
        virtual void sendFrame() = 0;
        virtual bool isDatagramAvailable() = 0;
        virtual std::tuple<DatagramHeader const*, uint8_t*, uint16_t> nextDatagram() = 0;
        virtual void read() = 0;

        virtual void addDatagramToFrame(uint8_t index, enum Command command, uint32_t address, void const* data, uint16_t data_size) = 0;

        virtual void resetFrameContext() = 0;
    };
}

#endif
