#include "LinkRedundancy.h"
#include "AbstractSocket.h"
#include "Error.h"

#include <functional>

namespace kickcat
{
    LinkRedundancy::LinkRedundancy(std::shared_ptr<AbstractSocket> socket_nominal,
                                   std::shared_ptr<AbstractSocket> socket_redundancy,
                                   std::function<void(void)> const& redundancyActivatedCallback,
                                   uint8_t const src_mac_nominal[MAC_SIZE],
                                   uint8_t const src_mac_redundancy[MAC_SIZE])
        : redundancyActivatedCallback_(redundancyActivatedCallback)
        , socket_nominal_(socket_nominal)
        , socket_redundancy_(socket_redundancy)
    {
        std::copy(src_mac_nominal, src_mac_nominal + MAC_SIZE, src_mac_nominal_);
        std::copy(src_mac_redundancy, src_mac_redundancy + MAC_SIZE, src_mac_redundancy_);
    }


    void LinkRedundancy::checkRedundancyNeeded()
    {
        Frame frame;
        frame.addDatagram(0, Command::BRD, createAddress(0, 0x0000), nullptr, 1);
        writeFrame(socket_redundancy_, frame, SECONDARY_IF_MAC);

        try
        {
            readFrame(socket_nominal_, frame);
        }
        catch (std::exception const& e1)
        {
            DEBUG_PRINT("%s\n Fail to read nominal interface \n", e1.what());
            try
            {
                readFrame(socket_redundancy_, frame);
            }
            catch (std::exception const& e2)
            {
                DEBUG_PRINT("%s\n Fail to read redundancy interface, master redundancy interface is not connected \n", e2.what());
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


    void LinkRedundancy::writeThenRead(Frame& frame)
    {
        int32_t to_write = frame.finalize();
        auto write_read_callback = [&](std::shared_ptr<AbstractSocket> to,
                                       std::shared_ptr<AbstractSocket> from,
                                       uint8_t const src_mac[MAC_SIZE])
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
        error_count += write_read_callback(socket_redundancy_, socket_nominal_, PRIMARY_IF_MAC);
        error_count += write_read_callback(socket_nominal_, socket_redundancy_, SECONDARY_IF_MAC);

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
    }


    void LinkRedundancy::sendFrame()
    {
        // save number of datagrams in the frame to handle send error properly if any
        int32_t const datagrams = frame_nominal_.datagramCounter();

        bool is_frame_sent = true;
        frame_nominal_.setSourceMAC(PRIMARY_IF_MAC);
        int32_t toWrite = frame_nominal_.finalize();
        int32_t written = socket_nominal_->write(frame_nominal_.data(), toWrite);
        if (written < 0)
        {
            is_frame_sent = false;
            DEBUG_PRINT("Nominal: write failed\n");
        }

        else if (written != toWrite)
        {
            is_frame_sent = false;
            DEBUG_PRINT("Nominal: Wrong number of bytes written");
        }

        frame_nominal_.setSourceMAC(SECONDARY_IF_MAC);
        written = socket_redundancy_->write(frame_nominal_.data(), toWrite);
        frame_nominal_.clear();

        if (written < 0)
        {
            is_frame_sent = false;
            DEBUG_PRINT("Nominal: write failed\n");
        }

        else if (written != toWrite)
        {
            is_frame_sent = false;
            DEBUG_PRINT("Nominal: Wrong number of bytes written");
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

    void LinkRedundancy::read()
    {
        try
        {
            readFrame(socket_redundancy_, frame_nominal_);
        }
        catch (std::exception const& e)
        {
            DEBUG_PRINT("Nominal read fail %s\n", e.what());
        }

        try
        {
            readFrame(socket_nominal_, frame_redundancy_);
        }
        catch (std::exception const& e)
        {
            DEBUG_PRINT("redundancy read fail %s\n", e.what());
        }
    }


    void LinkRedundancy::addDatagramToFrame(uint8_t index, enum Command command, uint32_t address, void const* data, uint16_t data_size)
    {
        frame_nominal_.addDatagram(index, command, address, data, data_size);
    }


    void LinkRedundancy::resetFrameContext()
    {
        frame_nominal_.resetContext();
        frame_redundancy_.resetContext();
    }


    bool LinkRedundancy::isDatagramAvailable()
    {
        return frame_nominal_.isDatagramAvailable() or frame_redundancy_.isDatagramAvailable();
    }


    std::tuple<DatagramHeader const*, uint8_t*, uint16_t> LinkRedundancy::nextDatagram()
    {
        auto [header_nominal, data_nominal, wkc_nominal] = frame_nominal_.nextDatagram();
        auto [header_redundancy, data_redundancy, wkc_redundancy] = frame_redundancy_.nextDatagram();

        std::transform(data_nominal, &data_nominal[header_nominal->len], data_redundancy, data_nominal, std::bit_or<uint8_t>());

        uint16_t wkc = wkc_nominal + wkc_redundancy;
        return std::make_tuple(header_nominal, data_nominal, wkc);
    }
}
