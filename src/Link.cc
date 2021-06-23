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
}
