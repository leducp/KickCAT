#include "Link.h"

#include "AbstractSocket.h"
#include "Error.h"

#include <functional>

namespace kickcat
{
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

        for (int32_t i = 0; i < waiting_frame; ++i)
        {
            read();
            while (isDatagramAvailable())
            {
                auto [header, data, wkc] = nextDatagram();
                irq |= header->irq; // aggregate IRQ feedbacks
                callbacks_[header->index].status = callbacks_[header->index].process(header, data, wkc);
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

            // Attach a callback to handle not THAT lost frames.
            // -> if a frame suspected to be lost was in fact in the pipe, it is needed to pop it
            callbacks_[i].process = [&](DatagramHeader const*, uint8_t const*, uint16_t)
                {
                    read();
                    return DatagramState::OK;
                };
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
        if (writeFrame(socket_redundancy_, frame, SECONDARY_IF_MAC) < 0)
        {
            DEBUG_PRINT("Fail to write on redundancy interface \n");
        }

        socket_nominal_->setTimeout(timeout_);
        socket_redundancy_->setTimeout(timeout_);
        if (readFrame(socket_nominal_, frame) < 0)
        {
            DEBUG_PRINT("Fail to read nominal interface \n");
            if (readFrame(socket_redundancy_, frame) < 0)
            {
                DEBUG_PRINT("Fail to read redundancy interface, master redundancy interface is not connected \n");
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
                DEBUG_PRINT("Nominal: write failed, written %i, to write %i\n", written, to_Write);
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
                int32_t index = index_head_ - i - 1;
                callbacks_[index].status = DatagramState::SEND_ERROR;
            }
        }
    }

    void Link::read()
    {
        nanoseconds deadline = since_epoch() + timeout_;

        socket_redundancy_->setTimeout(timeout_);
        if (readFrame(socket_redundancy_, frame_nominal_) < 0)
        {
            DEBUG_PRINT("Nominal read fail\n");
        }

        nanoseconds remaining_timeout = deadline - since_epoch();
        nanoseconds min_timeout = 0us;
        nanoseconds timeout_second_socket = std::max(remaining_timeout, min_timeout);

        socket_nominal_->setTimeout(timeout_second_socket);
        if (readFrame(socket_nominal_, frame_redundancy_) < 0)
        {
            DEBUG_PRINT("redundancy read fail\n");
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


    std::tuple<DatagramHeader const*, uint8_t*, uint16_t> Link::nextDatagram()
    {
        bool nom = frame_nominal_.isDatagramAvailable();
        bool red = frame_redundancy_.isDatagramAvailable();

        auto [header_nominal, data_nominal, wkc_nominal] = frame_nominal_.nextDatagram();
        auto [header_redundancy, data_redundancy, wkc_redundancy] = frame_redundancy_.nextDatagram();

        // When using the bus without redundancy not nom and red is the used case.
        if (not nom and red)
        {
            return std::make_tuple(header_redundancy, data_redundancy, wkc_redundancy);
        }

        if (nom and not red)
        {
            return std::make_tuple(header_nominal, data_nominal, wkc_nominal);
        }

        // all frames have data
        std::transform(data_nominal, &data_nominal[header_nominal->len], data_redundancy, data_nominal, std::bit_or<uint8_t>());
        uint16_t wkc = wkc_nominal + wkc_redundancy;
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
