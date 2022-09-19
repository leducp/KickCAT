#include "LinkRedundancy.h"
#include "AbstractSocket.h"
#include "Time.h"

#include "Teleplot.h"

namespace kickcat
{
    LinkRedundancy::LinkRedundancy(std::shared_ptr<AbstractSocket> socketNominal,
                                   std::shared_ptr<AbstractSocket> socketRedundancy,
                                   std::function<void(void)> const& redundancyActivatedCallback
                                   )
        : Link(socketNominal)
        , socketRedundancy_(socketRedundancy)
        , redundancyActivatedCallback_(redundancyActivatedCallback)
    {
        if (isRedundancyNeeded())
        {
            redundancyActivatedCallback_();
        }
    }


    bool LinkRedundancy::isRedundancyNeeded()
    {
        Frame frame;
        frame.addDatagram(0, Command::BRD, createAddress(0, 0x0000), nullptr, 1);
        frame.write(socketRedundancy_);

        try
        {
            frame.read(socketNominal_);
        }
        catch (std::exception const& e)
        {
            DEBUG_PRINT("%s\n Fail to read nominal interface \n", e.what());
            frame.read(socketRedundancy_);
        }

        auto [header, _, wkc] = frame.nextDatagram();
        return wkc != 0;
    }


    void LinkRedundancy::writeThenRead(Frame& frame, Frame& frame_redundancy)
    {
        try
        {
            frame.write(socketNominal_);
        }
        catch (std::exception const& e)
        {
            DEBUG_PRINT("%s\n", e.what());
        }

        try
        {
            frame_redundancy.write(socketRedundancy_);
        }
        catch (std::exception const& e)
        {
            DEBUG_PRINT("%s\n", e.what());
        }

        try
        {
            frame.read(socketRedundancy_);
        }
        catch (std::exception const& e)
        {
            DEBUG_PRINT("Nominal read fail %s\n", e.what());
        }

        try
        {
            frame_redundancy.read(socketNominal_);
        }
        catch (std::exception const& e)
        {
            DEBUG_PRINT("redundancy read fail %s\n", e.what());
        }
    }


    void LinkRedundancy::sendFrame()
    {
        // save number of datagrams in the frame to handle send error properly if any
        int32_t const datagrams = frame_.datagramCounter();

        bool is_frame_sent = false;
        try
        {
            frame_.write(socketNominal_);
            is_frame_sent = true;
        }
        catch (std::exception const& e)
        {
            DEBUG_PRINT("%s\n", e.what());
        }

        try
        {
            frameRedundancy_.write(socketRedundancy_);
            is_frame_sent = true;
        }
        catch (std::exception const& e)
        {
            DEBUG_PRINT("%s\n", e.what());
        }

        if (is_frame_sent)
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


    void LinkRedundancy::addDatagram(enum Command command, uint32_t address, void const* data, uint16_t data_size,
                           std::function<DatagramState(DatagramHeader const*, uint8_t const* data, uint16_t wkc)> const& process,
                           std::function<void(DatagramState const& state)> const& error)
    {
        if (index_queue_ == static_cast<uint8_t>(index_head_ + 1))
        {
            THROW_ERROR("Too many datagrams in flight. Max is 255");
        }

        uint16_t const needed_space = datagram_size(data_size);
        if (frame_.freeSpace() < needed_space)
        {
            sendFrame();
        }

        frame_.addDatagram(index_head_, command, address, data, data_size);
        frameRedundancy_.addDatagram(index_head_, command, address, data, data_size);
        callbacks_[index_head_].process = process;
        callbacks_[index_head_].error = error;
        callbacks_[index_head_].status = DatagramState::LOST;
        ++index_head_;

        if (frame_.isFull())
        {
            sendFrame();
        }
    }


    void LinkRedundancy::finalizeDatagrams()
    {
        if (frame_.datagramCounter() != 0)
        {
            sendFrame();
        }
    }


    bool LinkRedundancy::isDatagramAvailable()
    {
        return frame_.isDatagramAvailable() or frameRedundancy_.isDatagramAvailable();
    }


    void LinkRedundancy::nextDatagram()
    {
        auto [header_nominal, data_nominal, wkc_nominal] = frame_.nextDatagram();
        auto [header_redundancy, data_redundancy, wkc_redundancy] = frameRedundancy_.nextDatagram();
        // TODO handle case nominal is not send at all.

        Teleplot::localhost().update("wkc nominal frame", wkc_nominal);
        Teleplot::localhost().update("wkc redundancy frame", wkc_redundancy);

        for (uint8_t i = 0; i < header_nominal->len; i++)
        {
//            printf("Before: Nominal: %04x, Redundancy %04x \n", *(data_nominal + i), *(data_redundancy + i));
            *(data_nominal + i) |= *(data_redundancy + i);
//            printf("After: Res: %04x \n", *(data_nominal + i));
        }

        uint16_t wkc = wkc_nominal + wkc_redundancy;
        callbacks_[header_nominal->index].status = callbacks_[header_nominal->index].process(header_nominal, data_nominal, wkc);
    }


    void LinkRedundancy::processDatagrams()
    {
        finalizeDatagrams();

        uint8_t waiting_frame = sent_frame_;
        sent_frame_ = 0;

        for (int32_t i = 0; i < waiting_frame; ++i)
        {
            try
            {
                frame_.read(socketRedundancy_);
            }
            catch (std::exception const& e)
            {
                DEBUG_PRINT("Nominal read fail %s\n", e.what());
            }

            try
            {
                frameRedundancy_.read(socketNominal_);
            }
            catch (std::exception const& e)
            {
                DEBUG_PRINT("redundancy read fail %s\n", e.what());
            }

//            try
//            {
                while (isDatagramAvailable())
                {
                    nextDatagram();
                }
//            }
//            catch (std::exception const& e)
//            {
//                DEBUG_PRINT("Next datagram fail: %s\n", e.what());
//            }

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
                    try
                    {
                        frame_.read(socketNominal_);
                        printf("socketNominal_ Read in lost frame \n");

                    }
                    catch (std::exception const& e)
                    {
                        DEBUG_PRINT("Process lost Nominal frame failed %s\n", e.what());
                    }

                    try
                    {
                        frame_.read(socketRedundancy_);
                        printf("socketRedundancy_ Read in lost frame \n");

                    }
                    catch (std::exception const& e)
                    {
                        DEBUG_PRINT("Process lost Redundancy frame failed %s\n", e.what());
                    }
                    return DatagramState::OK;
                };
        }

        index_queue_ = index_head_;
        frame_.clear();
        frameRedundancy_.clear();

        // Rethrow last catched client exception.
        if (client_exception)
        {
            std::rethrow_exception(client_exception);
        }
    }
}
