#ifndef KICKCAT_LINK_REDUNDANCY_H
#define KICKCAT_LINK_REDUNDANCY_H

#include <array>
#include <memory>
#include <functional>

#include "KickCAT.h"
#include "Frame.h"

namespace kickcat
{
    class AbstractSocket;

    class Link
    {
    public:
        Link(std::shared_ptr<AbstractSocket> socket_nominal,
                       std::shared_ptr<AbstractSocket> socket_redundancy,
                       std::function<void(void)> const& redundancyActivatedCallback,
                       mac const src_mac_nominal = PRIMARY_IF_MAC,
                       mac const src_mac_redundancy = SECONDARY_IF_MAC);
        ~Link() = default;

        /// \brief   Helper for trivial access (i.e. most of the init bus frames)
        ///
        /// \details Since this method is only used for non real time operation, the redundancy mechanism used is slower
        ///          but guaranty slave order access, ie for setAdresses.
        void writeThenRead(Frame& frame) ;

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


        void checkRedundancyNeeded();
    friend class LinkTest;


    private:
        uint8_t index_queue_{0};
        uint8_t index_head_{0};
        uint8_t sent_frame_{0};

        struct Callbacks
        {
            DatagramState status{DatagramState::LOST};
            std::function<DatagramState(DatagramHeader const*, uint8_t const* data, uint16_t wkc)> process; // Shall not throw exception.
            std::function<void(DatagramState const& state)> error; // May throw exception.
        };
        std::array<Callbacks, 256> callbacks_{};


        void read() ;
        void sendFrame() ;
        bool isDatagramAvailable() ;
        std::tuple<DatagramHeader const*, uint8_t*, uint16_t> nextDatagram() ;
        void addDatagramToFrame(uint8_t index, enum Command command, uint32_t address, void const* data, uint16_t data_size) ;
        void resetFrameContext() ;

        std::function<void(void)> redundancyActivatedCallback_;

        std::shared_ptr<AbstractSocket> socket_nominal_;
        std::shared_ptr<AbstractSocket> socket_redundancy_;

        Frame frame_nominal_{};
        mac src_mac_nominal_;

        Frame frame_redundancy_{};
        mac src_mac_redundancy_;

        bool is_redundancy_activated_{false};
    };
}

#endif
