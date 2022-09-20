#include "Link.h"
#include "AbstractSocket.h"

namespace kickcat
{
    Link::Link(std::shared_ptr<AbstractSocket> socket, uint8_t const src_mac[MAC_SIZE])
        : nominal_interface_(socket, src_mac)
    {

    }


    void Link::writeThenRead(Frame& frame)
    {
        nominal_interface_.write(frame);
        nominal_interface_.read(frame);
        frame.clear();
    }


    void Link::sendFrame()
    {
        // save number of datagrams in the frame to handle send error properly if any
        int32_t const datagrams = frame_nominal_.datagramCounter();
        try
        {
            nominal_interface_.write(frame_nominal_);
            frame_nominal_.clear();
            ++sent_frame_;
        }
        catch (std::exception const& e)
        {
            DEBUG_PRINT("%s\n", e.what());

            for (int32_t i = 0; i < datagrams; ++i)
            {
                int32_t index = index_head_ - i - 1;
                callbacks_[index].status = DatagramState::SEND_ERROR;
            }
        }
    }


    void Link::readFrame()
    {
        nominal_interface_.read(frame_nominal_);
        frame_nominal_.clear();
    }


    bool Link::isDatagramAvailable()
    {
        return frame_nominal_.isDatagramAvailable();
    }


    std::tuple<DatagramHeader const*, uint8_t*, uint16_t> Link::nextDatagram()
    {
        return frame_nominal_.nextDatagram();
    }


    void Link::addDatagramToFrame(uint8_t index, enum Command command, uint32_t address, void const* data, uint16_t data_size)
    {
        frame_nominal_.addDatagram(index, command, address, data, data_size);
    }


    void Link::resetFrameContext()
    {
        frame_nominal_.resetContext();
    }
}
