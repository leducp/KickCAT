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

    class Link
    {
        friend class LinkTest;
    public:
        /// \brief   Handle link layer
        /// \details This class is responsible to handle frames and datagrams on the link layers:
        /// - associate an id to each datagram to call the associate callback later without depending on the read order
        /// - handle link redundancy
        Link(std::shared_ptr<AbstractSocket> socket_nominal,
                       std::shared_ptr<AbstractSocket> socket_redundancy,
                       std::function<void(void)> const& redundancyActivatedCallback,
                       MAC const src_nominal = PRIMARY_IF_MAC,
                       MAC const src_redundancy = SECONDARY_IF_MAC);
        ~Link() = default;

        /// \brief   Helper for trivial access (i.e. most of the init bus frames)
        ///
        /// \details Since this method is only used for non real time operation, the redundancy mechanism used is slower
        ///          but guaranty slave order access, ie for setAddresses().
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

        void setTimeout(nanoseconds const& timeout) {timeout_ = timeout;};

        void checkRedundancyNeeded();

        void attachEcatEventCallback(enum EcatEvent event, std::function<void()> callback);

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

        struct IRQ
        {
            std::function<void()> callback{[](){}};
            bool is_armed{true};
        };
        std::array<IRQ, 16> irqs_{};


        void read() ;
        void sendFrame() ;
        bool isDatagramAvailable() ;
        std::tuple<DatagramHeader const*, uint8_t*, uint16_t> nextDatagram() ;
        void addDatagramToFrame(uint8_t index, enum Command command, uint32_t address, void const* data, uint16_t data_size) ;
        void resetFrameContext() ;
        void checkEcatEvents(uint16_t irq);

        std::function<void(void)> redundancyActivatedCallback_;
        bool is_redundancy_activated_{false};

        std::shared_ptr<AbstractSocket> socket_nominal_;
        std::shared_ptr<AbstractSocket> socket_redundancy_;

        Frame frame_nominal_{};
        MAC src_nominal_;

        Frame frame_redundancy_{};
        MAC src_redundancy_;

        nanoseconds timeout_{2ms};
    };
}

#endif
