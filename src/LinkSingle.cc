#include "AbstractSocket.h"
#include "Error.h"

#include <cstring>
#include "LinkSingle.h"

namespace kickcat
{
    LinkSingle::LinkSingle(std::shared_ptr<AbstractSocket> socket, uint8_t const src_mac[MAC_SIZE])
        : socket_nominal_(socket)
    {
        std::copy(src_mac, src_mac + MAC_SIZE, src_mac_nominal_);
    }


    void LinkSingle::writeThenRead(Frame& frame)
    {
        writeFrame(socket_nominal_, frame, src_mac_nominal_);
        readFrame(socket_nominal_, frame);
    }


    void LinkSingle::sendFrame()
    {
        // save number of datagrams in the frame to handle send error properly if any
        int32_t const datagrams = frame_nominal_.datagramCounter();
        try
        {
            writeFrame(socket_nominal_, frame_nominal_, src_mac_nominal_);
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


    void LinkSingle::read()
    {
        try
        {
            readFrame(socket_nominal_, frame_nominal_);
        }
        catch (std::exception const& e)
        {
            DEBUG_PRINT("Read fail %s\n", e.what());
        }
    }


    bool LinkSingle::isDatagramAvailable()
    {
        return frame_nominal_.isDatagramAvailable();
    }


    std::tuple<DatagramHeader const*, uint8_t*, uint16_t> LinkSingle::nextDatagram()
    {
        return frame_nominal_.nextDatagram();
    }


    void LinkSingle::addDatagramToFrame(uint8_t index, enum Command command, uint32_t address, void const* data, uint16_t data_size)
    {
        frame_nominal_.addDatagram(index, command, address, data, data_size);
    }


    void LinkSingle::resetFrameContext()
    {
        frame_nominal_.resetContext();
    }
}
