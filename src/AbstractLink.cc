#include "AbstractLink.h"
#include "Error.h"

namespace kickcat
{
    void AbstractLink::addDatagram(enum Command command, uint32_t address, void const* data, uint16_t data_size,
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


    void AbstractLink::finalizeDatagrams()
    {
        if (frame_nominal_.datagramCounter() != 0)
        {
            sendFrame();
        }
    }


    void AbstractLink::processDatagrams()
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


    void AbstractLink::readFrame(std::shared_ptr<AbstractSocket> socket, Frame& frame)
    {
        int32_t read = socket->read(frame.data(), ETH_MAX_SIZE);
        if (read < 0)
        {
            THROW_SYSTEM_ERROR("read()");
        }

        // check if the frame is an EtherCAT one. if not, drop it and try again
        if (frame.ethernet()->type != ETH_ETHERCAT_TYPE)
        {
            THROW_ERROR("Invalid frame type");
        }

        int32_t expected = frame.header()->len + sizeof(EthernetHeader) + sizeof(EthercatHeader);
        frame.clear();
        if (expected < ETH_MIN_SIZE)
        {
            expected = ETH_MIN_SIZE;
        }
        if (read != expected)
        {
            THROW_ERROR("Wrong number of bytes read");
        }

        frame.setIsDatagramAvailable();
    }


    void AbstractLink::writeFrame(std::shared_ptr<AbstractSocket> socket, Frame& frame, uint8_t const src_mac[MAC_SIZE])
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
