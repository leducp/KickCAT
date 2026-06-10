#include <inttypes.h>
#include <algorithm>
#include <cstring>

#include "Link.h"

#include "AbstractSocket.h"
#include "Error.h"
#include "debug.h"

namespace kickcat
{
    void mergeSplitLRW(LogicalFrameDescription const& desc, uint8_t* data_nominal, uint8_t const* data_redundancy,
                       uint16_t wkc_nominal, uint16_t wkc_redundancy)
    {
        if (wkc_redundancy == 0)
        {
            return; // nominal copy went through every slave of the frame
        }

        if (wkc_nominal == 0)
        {
            std::memcpy(data_nominal, data_redundancy, static_cast<size_t>(desc.logical_size));
            return;
        }

        // Both wkc non-zero <=> split ring. Each copy loops back at the break and returns on the
        // interface it was injected on, so through Link::read()'s socket crossover data_nominal
        // is the bus-order suffix and data_redundancy the prefix. Attribution is exact only when
        // every slave answered; otherwise the wkc check discards the cycle anyway.
        uint16_t acc = 0;
        for (auto const& entry : desc.entries)
        {
            if (acc >= wkc_redundancy)
            {
                break;
            }
            acc += entry.contribution;
            if (entry.input_offset >= 0)
            {
                std::memcpy(data_nominal + entry.input_offset, data_redundancy + entry.input_offset,
                            static_cast<size_t>(entry.input_size));
            }
        }

        // Mailbox status bits are inserted at bit granularity over a zero-filled area: OR
        // rebuilds them whichever segment each slave ended on.
        for (int32_t i = desc.pdo_size; i < desc.logical_size; ++i)
        {
            data_nominal[i] |= data_redundancy[i];
        }
    }


    Link::Link(std::shared_ptr<AbstractSocket> socket_nominal,
                                   std::shared_ptr<AbstractSocket> socket_redundancy,
                                   std::function<void(void)> const& redundancyActivatedCallback,
                                   MAC const src_nominal,
                                   MAC const src_redundancy)
        : redundancyActivatedCallback_(redundancyActivatedCallback)
        , socket_nominal_(socket_nominal)
        , socket_redundancy_(socket_redundancy)
    {
        std::copy(src_nominal, src_nominal + MAC_SIZE, src_nominal_);
        std::copy(src_redundancy, src_redundancy + MAC_SIZE, src_redundancy_);
    }


    void Link::addDatagram(enum Command command, uint32_t address, void const* data, uint16_t data_size,
                               std::function<DatagramState(DatagramHeader const*, uint8_t const* data, uint16_t wkc)> const& process,
                               std::function<void(DatagramState const& state)> const& error)
    {
        if (index_queue_ == static_cast<uint8_t>(index_head_ + 1))
        {
            THROW_ERROR("Too many datagrams in flight. Max is 255");
        }
        link_info("Adding a datagram (already %d pending)\n", index_queue_);

        uint16_t const needed_space = datagram_size(data_size);
        if (frame_nominal_.freeSpace() < needed_space)
        {
            sendFrame();
        }

        addDatagramToFrame(index_head_, command, address, data, data_size);
        callbacks_[index_head_].process = process;
        callbacks_[index_head_].error = error;
        callbacks_[index_head_].status = DatagramState::LOST;
        ++index_head_;

        if (frame_nominal_.isFull())
        {
            sendFrame();
        }
    }


    void Link::finalizeDatagrams()
    {
        if (frame_nominal_.datagramCounter() != 0)
        {
            sendFrame();
        }
    }


    void Link::processDatagrams()
    {
        finalizeDatagrams();

        uint8_t waiting_frame = sent_frame_;
        sent_frame_ = 0;
        uint16_t irq = 0;

        int32_t stale_budget = waiting_frame;
        for (int32_t i = 0; i < waiting_frame; ++i)
        {
            read();
            bool stale_frame = false;
            bool dropped_datagram = false;
            while (isDatagramAvailable())
            {
                bool dropped_pair = false;
                auto [header, data, wkc] = nextDatagram(dropped_pair);
                dropped_datagram = dropped_datagram or dropped_pair;
                if (isStale(header->index))
                {
                    // Frame from a previous cycle, already reported lost: drop it. Reading it again
                    // here would corrupt the dispatch of the frames currently being iterated.
                    stale_frame = true;
                    continue;
                }
                irq |= header->irq; // aggregate IRQ feedbacks
                auto& callback = callbacks_[header->index];
                if (callback.status == DatagramState::OK)
                {
                    continue; // already answered by the other redundancy copy: never downgrade it
                }
                callback.status = callback.process(header, data, wkc);
            }

            if ((stale_frame or dropped_datagram) and (stale_budget > 0))
            {
                // A stale or desynchronized frame consumed this read: the expected one may still
                // be queued behind it.
                --stale_budget;
                --i;
            }
        }

        std::exception_ptr client_exception;
        for (uint8_t i = index_queue_; i != index_head_; ++i)
        {
            if (callbacks_[i].status != DatagramState::OK)
            {
                // Datagram was either lost or processing it encountered an error.
                try
                {
                    callbacks_[i].error(callbacks_[i].status);
                }
                catch (...)
                {
                    client_exception = std::current_exception();
                }
            }
        }

        index_queue_ = index_head_;
        resetFrameContext();

        // Rethrow last catched client exception.
        if (client_exception)
        {
            std::rethrow_exception(client_exception);
        }

        checkEcatEvents(irq);
    }


    void Link::checkRedundancyNeeded()
    {
        Frame frame;
        frame.addDatagram(0, Command::BRD, createAddress(0, 0x0000), nullptr, 1);
        if (writeFrame(*socket_redundancy_, frame, SECONDARY_IF_MAC) < 0)
        {
            link_error("Fail to write on redundancy interface \n");
        }

        socket_nominal_->setTimeout(timeout_);
        socket_redundancy_->setTimeout(timeout_);
        if (readFrame(*socket_nominal_, frame) < 0)
        {
            link_error("Fail to read nominal interface \n");
            if (readFrame(*socket_redundancy_, frame) < 0)
            {
                link_error("Fail to read redundancy interface, master redundancy interface is not connected \n");
                return;
            }
        }

        auto [header, _, wkc] = frame.nextDatagram();
        if (wkc != 0)
        {
            is_redundancy_activated_ = true;
            redundancyActivatedCallback_();
        }
    }


    void Link::writeThenRead(Frame& frame)
    {
        socket_nominal_->setTimeout(timeout_);
        socket_redundancy_->setTimeout(timeout_);
        int32_t to_write = frame.finalize();
        auto write_read = [&](std::shared_ptr<AbstractSocket> from,
                              std::shared_ptr<AbstractSocket> to,
                              MAC const& src)
        {
            int32_t is_faulty = 0;
            frame.setSourceMAC(src);
            int32_t written = from->write(frame.data(), to_write);
            if (written < to_write)
            {
                THROW_ERROR("Can't write to interface");
            }
            int32_t read = to->read(frame.data(), ETH_MAX_SIZE);
            if (read <= 0)
            {
                read = from->read(frame.data(), ETH_MAX_SIZE);
                if (read <= 0)
                {
                    is_faulty = 1;
                }
            }
            return is_faulty;
        };

        int32_t error_count = 0;
        error_count += write_read(socket_nominal_, socket_redundancy_, PRIMARY_IF_MAC);
        error_count += write_read(socket_redundancy_, socket_nominal_, SECONDARY_IF_MAC);

        if (error_count > 1)
        {
            THROW_SYSTEM_ERROR("WriteThenRead was not able to read anything on both interfaces");
        }

        if (frame.ethernet()->type != ETH_ETHERCAT_TYPE)
        {
            THROW_ERROR("Invalid frame type");
        }

        int32_t current_size = frame.header()->len + sizeof(EthernetHeader) + sizeof(EthercatHeader);

        // Take padding into account.
        if (current_size < ETH_MIN_SIZE)
        {
            current_size = ETH_MIN_SIZE;
        }
        if (current_size != to_write)
        {
            THROW_ERROR("Wrong number of bytes read");
        }
        frame.setIsDatagramAvailable();
    }


    void Link::sendFrame()
    {
        auto write = [&](std::shared_ptr<AbstractSocket> socket, Frame& frame, MAC const& src, int32_t to_Write)
        {
            bool is_frame_sent = true;
            frame.setSourceMAC(src);
            int32_t written = socket->write(frame.data(), to_Write);
            if (written != to_Write)
            {
                is_frame_sent = false;
                link_error("Nominal: write failed, written %" PRIi32 ", to write %" PRIi32 "\n", written, to_Write);
            }

            return is_frame_sent;
        };

        // save number of datagrams in the frame to handle send error properly if any
        int32_t const datagrams = frame_nominal_.datagramCounter();
        int32_t to_Write = frame_nominal_.finalize();

        bool is_frame_sent_nominal = write(socket_nominal_, frame_nominal_, PRIMARY_IF_MAC, to_Write);
        bool is_frame_sent_redundancy = write(socket_redundancy_, frame_nominal_, SECONDARY_IF_MAC, to_Write);

        frame_nominal_.clear();
        frame_redundancy_.clear();
        frame_redundancy_.resetContext();

        if (is_frame_sent_nominal or is_frame_sent_redundancy)
        {
            ++sent_frame_;
        }
        else
        {
            for (int32_t i = 0; i < datagrams; ++i)
            {
                uint8_t index = static_cast<uint8_t>(index_head_ - i - 1);
                callbacks_[index].status = DatagramState::SEND_ERROR;
            }
        }
    }

    void Link::read()
    {
        nanoseconds deadline = since_epoch() + timeout_;

        socket_redundancy_->setTimeout(timeout_);
        if (readFrame(*socket_redundancy_, frame_nominal_) < 0)
        {
            link_warning("Nominal frame read fail\n");
        }

        nanoseconds remaining_timeout = deadline - since_epoch();
        nanoseconds min_timeout = 0us;
        nanoseconds timeout_second_socket = std::max(remaining_timeout, min_timeout);

        socket_nominal_->setTimeout(timeout_second_socket);
        if (readFrame(*socket_nominal_, frame_redundancy_) < 0)
        {
            link_warning("Redundancy frame read fail\n");
        }
    }


    void Link::addDatagramToFrame(uint8_t index, enum Command command, uint32_t address, void const* data, uint16_t data_size)
    {
        frame_nominal_.addDatagram(index, command, address, data, data_size);
    }


    void Link::resetFrameContext()
    {
        frame_nominal_.resetContext();
        frame_redundancy_.resetContext();
    }


    bool Link::isDatagramAvailable()
    {
        return frame_nominal_.isDatagramAvailable() or frame_redundancy_.isDatagramAvailable();
    }


    std::tuple<DatagramHeader const*, uint8_t*, uint16_t> Link::nextDatagram(bool& dropped_pair)
    {
        bool nom = frame_nominal_.isDatagramAvailable();
        bool red = frame_redundancy_.isDatagramAvailable();

        // When using the bus without redundancy not nom and red is the used case.
        if (not nom and red)
        {
            return frame_redundancy_.nextDatagram();
        }

        if (nom and not red)
        {
            return frame_nominal_.nextDatagram();
        }

        auto [header_nominal, data_nominal, wkc_nominal] = frame_nominal_.nextDatagram();
        auto [header_redundancy, data_redundancy, wkc_redundancy] = frame_redundancy_.nextDatagram();

        if (header_nominal->index != header_redundancy->index)
        {
            // Desynchronized sockets (frame lost or delayed on one interface): these are two
            // unrelated datagrams, merging them would corrupt the dispatch. Keep the earliest
            // in-window one, drop the other.
            dropped_pair = true;
            uint8_t const ahead_nominal = static_cast<uint8_t>(header_nominal->index - index_queue_);
            uint8_t const ahead_redundancy = static_cast<uint8_t>(header_redundancy->index - index_queue_);
            if (ahead_redundancy < ahead_nominal)
            {
                return std::make_tuple(header_redundancy, data_redundancy, wkc_redundancy);
            }
            return std::make_tuple(header_nominal, data_nominal, wkc_nominal);
        }

        if (isStale(header_nominal->index))
        {
            // Stale pair: its merge context may be gone, report it unmerged for the caller to drop.
            return std::make_tuple(header_nominal, data_nominal, wkc_nominal);
        }

        // Two copies of the same datagram: each one carries the events and wkc of its own ring segment.
        header_nominal->irq |= header_redundancy->irq;
        uint16_t wkc = wkc_nominal + wkc_redundancy;
        switch (header_nominal->command)
        {
            case Command::NOP:
            case Command::BRD:
            case Command::APRD:
            case Command::FPRD:
            case Command::LRD:
            case Command::BRW:
            {
                // Read command: the payload was zeroed at send time, so OR-ing the two copies
                // rebuilds the data whichever segment each slave answered on. BRW confirmations
                // are request|slave-data per segment: OR-ing them yields the intact-ring result.
                std::transform(data_nominal, &data_nominal[header_nominal->len], data_redundancy, data_nominal, std::bit_or<uint8_t>());
                break;
            }
            case Command::LRW:
            {
                auto desc = std::find_if(logical_mapping_.begin(), logical_mapping_.end(),
                    [&](LogicalFrameDescription const& d) { return d.address == header_nominal->address; });
                if ((desc != logical_mapping_.end()) and (header_nominal->len == desc->logical_size))
                {
                    // The length check fences the splice: mergeSplitLRW writes logical_size
                    // bytes, which only the mapped datagram itself can absorb.
                    mergeSplitLRW(*desc, data_nominal, data_redundancy, wkc_nominal, wkc_redundancy);
                    break;
                }
                [[fallthrough]]; // LRW outside the mapping: single-segment rule
            }
            case Command::APRW:
            case Command::FPRW:
            {
                // Read data is only in the copy whose segment holds the addressed slaves: take
                // the other copy when the nominal one went through none of them.
                if ((wkc_nominal == 0) and (wkc_redundancy != 0))
                {
                    std::memcpy(data_nominal, data_redundancy, header_nominal->len);
                }
                break;
            }
            default:
            {
                // Write commands echo the sent payload, identical in both copies; ARMW/FRMW read
                // data cannot be attributed to a copy from the wkc alone. Keep the nominal copy.
                break;
            }
        }
        return std::make_tuple(header_nominal, data_nominal, wkc);
    }


    void Link::attachEcatEventCallback(enum EcatEvent ecat_event, std::function<void()> callback)
    {
        int32_t index = 0;
        uint16_t mask = ecat_event;
        while (mask != 0)
        {
            if (mask & 1)
            {
                auto& event = irqs_[index];
                event.callback = callback;
            }

            ++index;
            mask = mask >> 1;
        }
    }


    void Link::checkEcatEvents(uint16_t irq)
    {
        for (uint16_t i = 0; i < irqs_.size(); ++i)
        {
            auto& event = irqs_[i];
            if (irq & 1)
            {
                if (event.is_armed)
                {
                    event.is_armed = false;     // Disable event: we want to trigger on slope
                    event.callback();
                }
            }
            else
            {
                // Rearm event
                event.is_armed = true;
            }

            irq = irq >> 1;
        }
    }
}
