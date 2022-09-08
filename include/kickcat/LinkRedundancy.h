#ifndef KICKCAT_LINK_REDUNDANCY_H
#define KICKCAT_LINK_REDUNDANCY_H

#include <array>
#include <memory>
#include <functional>

#include "KickCAT.h"
#include "Frame.h"
#include "Link.h"


namespace kickcat
{
    /// \brief Handle link layer
    /// \details This class is responsible to handle frames and datagrams on the link layers:
    ///           - associate an id to each datagram to call the associate callback later without depending on the read order
    class LinkRedundancy : public Link
    {
    public:
        LinkRedundancy(std::shared_ptr<AbstractSocket> socketNominal,
                       std::shared_ptr<AbstractSocket> socketRedundancy,
                       std::function<void(void)> const& redundancyActivatedCallback);
        ~LinkRedundancy() = default;

        /// \brief helper for trivial access (i.e. most of the init bus frames)
        void writeThenRead(Frame& frame) override;

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

        bool isRedundancyNeeded();

        void finalizeDatagrams();
        void processDatagrams() override;

    private:
        void sendFrame() override;

        std::shared_ptr<AbstractSocket> socketRedundancy_;

        std::function<void(void)> redundancyActivatedCallback_;
    };
}

#endif
