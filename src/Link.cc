#include "Link.h"
#include "AbstractSocket.h"
#include "Time.h"
#include <iostream> //debug, to remove

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
        if (index_ == 255)
        {
            THROW_ERROR("Too many datagrams in flight. Max is 256");
        }

        uint16_t const needed_space = datagram_size(data_size);
        if (frame_.freeSpace() < needed_space)
        {
            sendFrame();
        }

        frame_.addDatagram(index_, command, address, data, data_size);
        callbacks_[index_].process = process;
        callbacks_[index_].error = error;
        ++index_;

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
        uint8_t waiting_datagrams_ = index_;
        index_ = 0;

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
            catch(std::exception const& e)
            {
                std::cout << e.what() << std::endl;
            }
        }

        for (int32_t i = 0; i < waiting_datagrams_; ++i)
        {
            if (callbacks_[i].in_error)
            {
                // datagram was either lost or processing it encountered an errro
                callbacks_[i].error();
            }
        }
    }
}
