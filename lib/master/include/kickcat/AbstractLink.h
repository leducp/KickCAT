#ifndef KICKCAT_ABSTRACT_LINK_H
#define KICKCAT_ABSTRACT_LINK_H

#include <cstdint>
#include <cstring>
#include <functional>
#include <vector>

#include "kickcat/Frame.h"

namespace kickcat
{
    /// \brief Payload layout of a mapped logical frame, keyed by its logical address.
    /// \details Required to splice the two redundancy copies of an LRW datagram: inputs and
    ///          outputs share logical addresses, so the copies cannot be merged from the
    ///          command alone.
    struct LogicalFrameDescription
    {
        uint32_t address{0};        // logical address
        int32_t  logical_size{0};   // full logical address range (PDO + mailbox status bits)
        int32_t  pdo_size{0};       // process data only

        // Per-slave LRW expectations in bus position order, to attribute each slave to a
        // ring segment from the per-copy working counters when redundancy splices a frame
        struct Entry
        {
            uint16_t contribution{0};  // expected wkc contribution of this slave on LRW
            int32_t  input_offset{-1}; // frame offset of its input block, -1 when none
            int32_t  input_size{0};
        };
        std::vector<Entry> entries;
    };

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

        /// \brief Provide the layout of the mapped logical frames, replacing any previous one.
        /// \details Links without redundancy support ignore it.
        virtual void setLogicalMapping(std::vector<LogicalFrameDescription> const&) {}

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
