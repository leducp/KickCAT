#include "LinkRedundancy.h"
#include "AbstractSocket.h"
#include "Time.h"

#include <cstring>

namespace kickcat
{
    LinkRedundancy::LinkRedundancy(std::shared_ptr<AbstractSocket> socket_nominal,
                                   std::shared_ptr<AbstractSocket> socket_redundancy,
                                   std::function<void(void)> const& redundancyActivatedCallback,
                                   uint8_t const src_mac_nominal[MAC_SIZE],
                                   uint8_t const src_mac_redundancy[MAC_SIZE])
        : nominal_interface_(socket_nominal, src_mac_nominal)
        , redundancy_interface_(socket_redundancy, src_mac_redundancy)
        , redundancyActivatedCallback_(redundancyActivatedCallback)
        , socket_nominal_(socket_nominal)
        , socket_redundancy_(socket_redundancy)
    {
//        if (isRedundancyNeeded())
//        {
//            is_redundancy_activated_ = true;
//            redundancyActivatedCallback_();
//        }
    }


    bool LinkRedundancy::isRedundancyNeeded()
    {
        Frame frame;
        frame.addDatagram(0, Command::BRD, createAddress(0, 0x0000), nullptr, 1);
        redundancy_interface_.write(frame);

        try
        {
            nominal_interface_.read(frame);
        }
        catch (std::exception const& e1)
        {
            DEBUG_PRINT("%s\n Fail to read nominal interface \n", e1.what());
            try
            {
                redundancy_interface_.read(frame);
            }
            catch (std::exception const& e2)
            {
                DEBUG_PRINT("%s\n Fail to read redundancy interface, master redundancy interface is not connected \n", e2.what());
                return true;
            }
        }

        auto [header, _, wkc] = frame.nextDatagram();
        return wkc != 0;
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

//
//        lamda op[&](to, from, src_mac)
//        {
//            res = write(from);
//            if (failed (res))
//                return aie
//
//            res = read(to);
//            if (failed(res))
//            {
//                res = read(from);
//            }
//            return res;
//        }
//
//        res_count = 0;
//        res += op (nom, red)
//        res += op (red, nom)
//
//        if (res > 1)
//        {
//            THROW LENFER IT NEVER HAPPENED
//        }
//
//        if (not isEthercatType(frame))
//        {
//            throw l'enfer
//        }
//
//        cuurent_size = frame.len + sizeof(header ether) + siezof(headerCat))
//        if (cur_size < 60) cur_size = 60;
//        if cur_size != to_write
//        {
//            throw l'enfer'
//        }






//        /// WRITE without reset
//        frame.setSourceMAC(PRIMARY_IF_MAC);
//
//        int32_t to_write = frame.finalize();
//        int32_t written = socket_nominal_->write(frame.data(), to_write);
//        printf("Writtent nominal %i \n", written);
//
//        // TODO check written ?
//        int32_t error_count = 0;
//
//        /// READ
//        int32_t read = socket_redundancy_->read(frame.data(), ETH_MAX_SIZE);
//        // TODO check size coherency, check ethertype consistency  (Throw because frame has been messed up by someone on the network)
//        // if read < 1 -> enregistre timeout
//
//        if (read <= 0)
//        {
//            read = socket_nominal_->read(frame.data(), ETH_MAX_SIZE);
//            error_count++;
//        }
//
//        if (frame.ethernet()->type != ETH_ETHERCAT_TYPE)
//        {
//            THROW_ERROR("Invalid frame type");
//        }
//
//
//
//
//        /// WRITE
//        frame.setSourceMAC(SECONDARY_IF_MAC);
//
//        written = socket_redundancy_->write(frame.data(), to_write);
//
//        ///READ
////        nominal_interface_.read(frame);
//
//        read = socket_nominal_->read(frame.data(), ETH_MAX_SIZE);
//        if (read <= 0)
//        {
//            error_count++;
//        }
//
//        if (error_count >= 2)
//        {
//            THROW_SYSTEM_ERROR("WriteThenRead was not able to read anything on both interfaces");
//        }
//
//        // check if the frame is an EtherCAT one. if not, drop it and try again
//        if (frame.ethernet()->type != ETH_ETHERCAT_TYPE)
//        {
//            THROW_ERROR("Invalid frame type");
//        }
//
////        frame.clear();
//        frame.setIsDatagramAvailable(true);
//
//
//
//
//        printf("Written redundancy %i \n", written);














//        try
//        {
//            printf("Frame len %i  N\n", frame.header()->len);
//            nominal_interface_.write(frame);
//            redundancy_interface_.read(frame);
//
//        }
//        catch (std::exception const& e)
//        {
//            DEBUG_PRINT("Nominal read fail %s\n", e.what());
//        }
//
//        sleep(1s);
//
//        try
//        {
//            printf("Frame len %i  R\n", frame.header()->len);
//            redundancy_interface_.write(frame);
//            frame.resetContext();
//            nominal_interface_.read(frame);
//            frame.clear();
//        }
//        catch (std::exception const& e)
//        {
//            DEBUG_PRINT("Redundancy read fail %s\n", e.what());
//        }
    }


    void LinkRedundancy::sendFrame()
    {
        // save number of datagrams in the frame to handle send error properly if any
        int32_t const datagrams = frame_nominal_.datagramCounter();

        bool is_frame_sent = false;
        try
        {
            nominal_interface_.write(frame_nominal_);
            is_frame_sent = true;
        }
        catch (std::exception const& e)
        {
            DEBUG_PRINT("nominal write %s\n", e.what());
        }

        try
        {
            redundancy_interface_.write(frame_redundancy_);
            is_frame_sent = true;
        }
        catch (std::exception const& e)
        {
            DEBUG_PRINT("Redundancy write %s\n", e.what());
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

    void LinkRedundancy::readFrame()
    {
        try
        {
            redundancy_interface_.read(frame_nominal_);
        }
        catch (std::exception const& e)
        {
            DEBUG_PRINT("Nominal read fail %s\n", e.what());
        }

        try
        {
            nominal_interface_.read(frame_redundancy_);
        }
        catch (std::exception const& e)
        {
            DEBUG_PRINT("redundancy read fail %s\n", e.what());
        }
    }


    void LinkRedundancy::addDatagramToFrame(uint8_t index, enum Command command, uint32_t address, void const* data, uint16_t data_size)
    {
        frame_nominal_.addDatagram(index, command, address, data, data_size);
        frame_redundancy_.addDatagram(index, command, address, data, data_size);
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
        // TODO handle case nominal is not send at all.

        for (uint8_t i = 0; i < header_nominal->len; i++)
        {
            data_nominal[i] |= data_redundancy[i];  // todo test transform method.
        }

//        std::transform(data_nominal, data_nominal[header_nominal->len], data_redundancy, data_redundancy[header_nominal->len], )

        uint16_t wkc = wkc_nominal + wkc_redundancy;
//        printf("next DTG, wkc n %i, wkc r %i wkc: %i\n", wkc_nominal, wkc_redundancy, wkc);
        return std::make_tuple(header_nominal, data_nominal, wkc);
    }
}
