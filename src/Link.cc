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

    Link::~Link()
    {
        socket_->close();
    }

    void Link::addFrame(Frame& frame, std::function<bool(Frame&)>& process, std::function<void()> const& error)
    {
        if (index_ == 255)
        {
            THROW_ERROR("Too many frame in flight. Max is 256");
        }
        callbacks_[index_].process = process;
        callbacks_[index_].error = error;

        frame.setIndex(index_);
        frame.write(socket_);
        ++index_;
    }

    // read the frame associated with the id 'frame_id'
    void Link::processFrames()
    {
        uint8_t waiting_frame = index_;
        index_ = 0;

        Frame frame;
        for (int32_t i = 0; i < waiting_frame; ++i)
        {
            try
            {
                frame.read(socket_);
                uint8_t index = frame.index();

                callbacks_[index].was_run = true;
                if (not callbacks_[index].process(frame))
                {
                    callbacks_[index].error();
                }
            }
            catch(std::exception const& e)
            {
                std::cout << e.what() << std::endl;
            }
        }

        for (int32_t i = 0; i < waiting_frame; ++i)
        {
            if (callbacks_[i].was_run == false)
            {
                // frame was lost
                callbacks_[i].error();
            }
        }
    }


    void Link::writeThenRead(Frame& frame)
    {
        frame.write(socket_);
        frame.read(socket_);
    }


    void Link::addDatagram(enum Command command, uint32_t address, void const* data, uint16_t data_size,
                           std::function<bool(DatagramHeader const*, uint8_t const* data, uint16_t wkc)> const& process,
                           std::function<void()> const& error)
    {
        if (index_ == 255)
        {
            THROW_ERROR("Too many datagrams in flight. Max is 256");
        }

        frame_.addDatagram(index_, command, address, data, data_size);
        callbacks_new_[index_].process = process;
        callbacks_new_[index_].error = error;
        ++index_;

        if (frame_.isFull())
        {
            frame_.write(socket_);
            ++sent_frame_;
        }
    }

    void Link::finalizeDatagrams()
    {
        if (frame_.datagramCounter() != 0)
        {
            frame_.write(socket_);
            ++sent_frame_;
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
                    printf("yolo %d \n",header->index);
                    callbacks_new_[header->index].in_error = callbacks_new_[header->index].process(header, data, wkc);
                    printf("yala\n");
                }
            }
            catch(std::exception const& e)
            {
                std::cout << e.what() << std::endl;
            }
        }

        for (int32_t i = 0; i < waiting_datagrams_; ++i)
        {
            if (callbacks_new_[i].in_error)
            {
                // datagram was either lost or processing it encountered an errro
                printf("duck\n");
                callbacks_new_[i].error();
                printf("coincoin\n");
            }
        }
    }
}
