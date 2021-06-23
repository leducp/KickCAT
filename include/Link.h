#ifndef KICKCAT_LINK_H
#define KICKCAT_LINK_H

#include <array>
#include <memory>
#include <functional>

#include "Frame.h"

namespace kickcat
{
    class AbstractSocket;

    /// \brief Handle link layer
    /// \details This class is responsible to handle frames on the link layers:
    ///           - associate an id to each written frame to let the client fetch it later without knowning write/read ordering
    ///           - handle link redundancy (TODO)
    class Link
    {
    public:
        Link(std::shared_ptr<AbstractSocket> socket);
        ~Link();

        // write a frame on the line and record the associated callbacks for later usage
        void addFrame(Frame& frame, std::function<bool(Frame&)>& callback, std::function<void()> const& errorCallback);

        /// process all waiting tasks
        /// \details do N read on the line with N the number of addFrame() call.
        ///          For each read frame, it will run the associated callback. If it fail, it will run the error callback.
        void processFrames();

        /// \brief helper for trivial access (i.e. most of the init bus frames)
        void writeThenRead(Frame& frame);

        void addDatagram(enum Command command, uint32_t address, void const* data, uint16_t data_size,
                         std::function<bool(DatagramHeader const*, uint8_t const* data, uint16_t wkc)> const& process,
                         std::function<void()> const& error);
        template<typename T>
        void addDatagram(enum Command command, uint32_t address, T const& data,
                         std::function<bool(DatagramHeader const*, uint8_t const* data, uint16_t wkc)> const& process,
                         std::function<void()> const& error)
        {
            addDatagram(command, address, &data, sizeof(data), process, error);
        }

        void finalizeDatagrams();
        void processDatagrams();

    private:
        std::shared_ptr<AbstractSocket> socket_;
        uint8_t index_{0};
        uint8_t sent_frame_{0};
        Frame frame_{PRIMARY_IF_MAC};

        struct callbacks
        {
            bool was_run{false};
            std::function<bool(Frame&)> process;
            std::function<void()> error;
        };

        // 256 -> index is an uint8_t
        std::array<callbacks, 256> callbacks_{};

        struct callback_new
        {
            bool in_error{false};
            std::function<bool(DatagramHeader const*, uint8_t const* data, uint16_t wkc)> process;
            std::function<void()> error;
        };
        std::array<callback_new, 256> callbacks_new_{};
    };
}

#endif
