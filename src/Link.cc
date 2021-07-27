#include "Link.h"
#include "AbstractSocket.h"
#include "Time.h"

namespace kickcat
{
    Link::Link(std::shared_ptr<AbstractSocket> socket)
        : socket_(socket)
    {

    }


    void Link::writeThenRead(Frame& frame)
    {
        frame.write(socket_);
        frame.read(socket_);
    }


    void Link::sendFrame()
    {
        frame_.write(socket_);
        ++sent_frame_;
    }


    void Link::addDatagram(enum Command command, uint32_t address, void const* data, uint16_t data_size,
                           std::function<bool(DatagramHeader const*, uint8_t const* data, uint16_t wkc)> const& process,
                           std::function<void()> const& error)
    {
        if (index_queue_ == (index_head_ + 1))
        {
            THROW_ERROR("Too many datagrams in flight. Max is 256");
        }

        uint16_t const needed_space = datagram_size(data_size);
        if (frame_.freeSpace() < needed_space)
        {
            sendFrame();
        }

        frame_.addDatagram(index_head_, command, address, data, data_size);
        callbacks_[index_head_].process = process;
        callbacks_[index_head_].error = error;
        callbacks_[index_head_].in_error = true;
        ++index_head_;

        if (frame_.isFull())
        {
            sendFrame();
        }
    }


    void Link::finalizeDatagrams()
    {
        if (frame_.datagramCounter() != 0)
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
            try
            {
                frame_.read(socket_);
                while (frame_.isDatagramAvailable())
                {
                    auto [header, data, wkc] = frame_.nextDatagram();
                    callbacks_[header->index].in_error = callbacks_[header->index].process(header, data, wkc);
                }
            }
            catch (std::exception const& e)
            {
                DEBUG_PRINT("%s\n", e.what());
            }
        }

        std::exception_ptr client_exception;
        for (uint8_t i = index_queue_; i != index_head_; ++i)
        {
            if (callbacks_[i].in_error)
            {
                // Datagram was either lost or processing it encountered an error.
                try
                {
                    callbacks_[i].error();
                }
                catch (...)
                {
                    client_exception = std::current_exception();
                }
            }

            // Attach a callback to handle not THAT lost frames.
            // -> if a frame suspected to be lost was in fact in the pipe, it is needed to pop it
            callbacks_[i].process = [&](DatagramHeader const*, uint8_t const*, uint16_t){ frame_.read(socket_); return false; };
        }

        index_queue_ = index_head_;

        // Rethrow last catched client exception.
        if (client_exception)
        {
            std::rethrow_exception(client_exception);
        }
    }
}
