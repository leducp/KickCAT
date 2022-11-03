#include "Link.h"

#include "AbstractSocket.h"
#include "Error.h"

#include <functional>

namespace kickcat
{
    Link::Link(std::shared_ptr<AbstractSocket> socket_nominal,
                                   std::shared_ptr<AbstractSocket> socket_redundancy,
                                   std::function<void(void)> const& redundancyActivatedCallback,
                                   mac const src_mac_nominal,
                                   mac const src_mac_redundancy)
        : redundancyActivatedCallback_(redundancyActivatedCallback)
        , socket_nominal_(socket_nominal)
        , socket_redundancy_(socket_redundancy)
    {
        std::copy(src_mac_nominal, src_mac_nominal + MAC_SIZE, src_mac_nominal_);
        std::copy(src_mac_redundancy, src_mac_redundancy + MAC_SIZE, src_mac_redundancy_);
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

        for (int32_t i = 0; i < waiting_frame; ++i)
        {
            read();
            while (isDatagramAvailable())
            {
                auto [header, data, wkc] = nextDatagram();
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
    }


    void Link::checkRedundancyNeeded()
    {
        Frame frame;
        frame.addDatagram(0, Command::BRD, createAddress(0, 0x0000), nullptr, 1);
        writeFrame(socket_redundancy_, frame, SECONDARY_IF_MAC);

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
        int32_t to_write = frame.finalize();
        auto write_read_callback = [&](std::shared_ptr<AbstractSocket> from,
                                       std::shared_ptr<AbstractSocket> to,
                                       mac const src_mac)
        {
            int32_t is_faulty = 0;
            frame.setSourceMAC(src_mac);
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
        error_count += write_read_callback(socket_nominal_, socket_redundancy_, PRIMARY_IF_MAC);
        error_count += write_read_callback(socket_redundancy_, socket_nominal_, SECONDARY_IF_MAC);

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
        // save number of datagrams in the frame to handle send error properly if any
        int32_t const datagrams = frame_nominal_.datagramCounter();

        bool is_frame_sent_nominal = true;
        frame_nominal_.setSourceMAC(PRIMARY_IF_MAC);
        int32_t toWrite = frame_nominal_.finalize();
        int32_t written = socket_nominal_->write(frame_nominal_.data(), toWrite);
        if (written < 0)
        {
            is_frame_sent_nominal = false;
            DEBUG_PRINT("Nominal: write failed\n");
        }
        else if (written != toWrite)
        {
            is_frame_sent_nominal = false;
            DEBUG_PRINT("Nominal: Wrong number of bytes written");
        }

        bool is_frame_sent_redundancy = true;
        frame_nominal_.setSourceMAC(SECONDARY_IF_MAC);
        written = socket_redundancy_->write(frame_nominal_.data(), toWrite);
        frame_nominal_.clear();
        frame_redundancy_.clear();
        frame_redundancy_.resetContext();

        if (written < 0)
        {
            is_frame_sent_redundancy = false;
            DEBUG_PRINT("Redundancy: write failed\n");
        }

        else if (written != toWrite)
        {
            is_frame_sent_redundancy = false;
            DEBUG_PRINT("Redundancy: Wrong number of bytes written");
        }

        if (is_frame_sent_nominal or is_frame_sent_redundancy)
        {
            sent_frame_++;
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
        if (readFrame(socket_redundancy_, frame_nominal_) < 0)
        {
            DEBUG_PRINT("Nominal read fail\n");
        }

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




    int32_t readFrame(std::shared_ptr<AbstractSocket> socket, Frame& frame)
    {
        int32_t read = socket->read(frame.data(), ETH_MAX_SIZE);
        if (read < 0)
        {
            DEBUG_PRINT("read() failed");
            return read;
        }

        // check if the frame is an EtherCAT one. if not, drop it and try again
        if (frame.ethernet()->type != ETH_ETHERCAT_TYPE)
        {
            DEBUG_PRINT("Invalid frame type");
            return -1;
        }

        int32_t expected = frame.header()->len + sizeof(EthernetHeader) + sizeof(EthercatHeader);
        frame.clear();
        if (expected < ETH_MIN_SIZE)
        {
            expected = ETH_MIN_SIZE;
        }
        if (read != expected)
        {
            DEBUG_PRINT("Wrong number of bytes read");
            return -1;
        }

        frame.setIsDatagramAvailable();
        return read;
    }


    void writeFrame(std::shared_ptr<AbstractSocket> socket, Frame& frame, mac const src_mac)
    {
        frame.setSourceMAC(src_mac);
        int32_t toWrite = frame.finalize();
        int32_t written = socket->write(frame.data(), toWrite);
        frame.clear();

        if (written < 0)
        {
            THROW_SYSTEM_ERROR("write()");
        }

        if (written != toWrite)
        {
            THROW_ERROR("Wrong number of bytes written");
        }
    }
}
