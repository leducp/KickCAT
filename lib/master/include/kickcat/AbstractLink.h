#ifndef KICKCAT_ABSTRACT_LINK_H
#define KICKCAT_ABSTRACT_LINK_H

#include <cstring>
#include <functional>

#include "kickcat/Frame.h"

namespace kickcat
{
    /// \brief Abstract interface for the EtherCAT link layer.
    /// \details The link layer is responsible for sending and receiving EtherCAT frames on the wire.
    ///          It provides two communication modes:
    ///          - Synchronous: writeThenRead() sends a frame and blocks until the response is received.
    ///          - Asynchronous: addDatagram() queues datagrams with associated callbacks, then
    ///            processDatagrams() sends them all and dispatches the responses through the callbacks.
    class AbstractLink
    {
    public:
        virtual ~AbstractLink() = default;

        /// \brief Send a frame on the wire and wait for the response (synchronous).
        /// \param frame Frame to send - modified in place with the response data.
        virtual void writeThenRead(Frame& frame) = 0;

        /// \brief Queue a datagram to be sent on the next processDatagrams() call.
        /// \param command   EtherCAT command (e.g. BRD, FPRD, FPWR, LRD, LWR, LRW).
        /// \param address   Slave address (auto-increment, configured, or logical depending on command).
        /// \param data      Pointer to the data payload to send.
        /// \param data_size Size of the data payload in bytes.
        /// \param process   Callback invoked on response: returns the resulting datagram state.
        /// \param error     Callback invoked when the datagram was lost or its processing failed.
        virtual void addDatagram(enum Command command, uint32_t address, void const* data, uint16_t data_size,
                                 std::function<DatagramState(DatagramHeader const*, uint8_t const* data, uint16_t wkc)> const& process,
                                 std::function<void(DatagramState const& state)> const& error) = 0;

        /// \brief Convenience overload: queue a datagram with a typed payload.
        template<typename T>
        void addDatagram(enum Command command, uint32_t address, T const& data,
                         std::function<DatagramState(DatagramHeader const*, uint8_t const* data, uint16_t wkc)> const& process,
                         std::function<void(DatagramState const& state)> const& error)
        {
            addDatagram(command, address, &data, sizeof(data), process, error);
        }

        /// \brief Flush any remaining queued datagrams onto the wire.
        virtual void finalizeDatagrams() = 0;

        /// \brief Send all queued datagrams, read responses, and invoke callbacks.
        virtual void processDatagrams() = 0;

        /// \brief Set the read timeout for the underlying sockets.
        virtual void setTimeout(nanoseconds const& timeout) = 0;

        /// \brief Check if redundancy is needed to handle the current bus. If yes it means to system is already degraded.
        virtual void checkRedundancyNeeded() = 0;

        /// \brief Register a callback to be called when an EtherCAT IRQ event is detected.
        virtual void attachEcatEventCallback(enum EcatEvent event, std::function<void()> callback) = 0;
    };
}

#endif
