#ifndef KICKCAT_LINK_H
#define KICKCAT_LINK_H

#include <array>
#include <functional>

#include "kickcat/AbstractLink.h"

namespace kickcat
{
    class AbstractSocket;

    /// \brief Rebuild a spliced LRW payload in place in data_nominal from the two ring copies.
    /// \details Slaves are attributed to a segment by accumulating their expected contributions
    ///          against the prefix copy's wkc.
    /// \warning On a split ring each copy loops back at the break: data_nominal holds the
    ///          tail-injected copy (bus-order suffix) and data_redundancy the head-injected
    ///          copy (bus-order prefix).
    void mergeSplitLRW(LogicalFrameDescription const& desc, uint8_t* data_nominal, uint8_t const* data_redundancy,
                       uint16_t wkc_nominal, uint16_t wkc_redundancy);

    class Link final : public AbstractLink
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
        ~Link() override = default;

        void writeThenRead(Frame& frame) override;

        using AbstractLink::addDatagram;
        void addDatagram(enum Command command, uint32_t address, void const* data, uint16_t data_size,
                         std::function<DatagramState(DatagramHeader const*, uint8_t const* data, uint16_t wkc)> const& process,
                         std::function<void(DatagramState const& state)> const& error) override;

        void setLogicalMapping(std::vector<LogicalFrameDescription> const& mapping) override { logical_mapping_ = mapping; }

        void finalizeDatagrams() override;
        void processDatagrams() override;

        void setTimeout(nanoseconds const& timeout) override {timeout_ = timeout;};

        void checkRedundancyNeeded() override;

        void attachEcatEventCallback(enum EcatEvent event, std::function<void()> callback) override;

    private:
        uint8_t index_queue_{0};
        uint8_t index_head_{0};
        uint8_t sent_frame_{0};

        // Index outside the [index_queue_, index_head_) window: previous-cycle leftover.
        bool isStale(uint8_t index) const
        {
            uint8_t const in_flight = static_cast<uint8_t>(index_head_ - index_queue_);
            return static_cast<uint8_t>(index - index_queue_) >= in_flight;
        }

        struct Callbacks
        {
            DatagramState status{DatagramState::LOST};
            std::function<DatagramState(DatagramHeader const*, uint8_t const* data, uint16_t wkc)> process; // Shall not throw exception.
            std::function<void(DatagramState const& state)> error; // May throw exception.
        };
        std::array<Callbacks, 256> callbacks_{};

        std::vector<LogicalFrameDescription> logical_mapping_{}; // Empty: command-based default merge.

        struct IRQ
        {
            std::function<void()> callback{[](){}};
            bool is_armed{true};
        };
        std::array<IRQ, 16> irqs_{};


        void read() ;
        void sendFrame() ;
        bool isDatagramAvailable() ;
        std::tuple<DatagramHeader const*, uint8_t*, uint16_t> nextDatagram(bool& dropped_pair) ;
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
