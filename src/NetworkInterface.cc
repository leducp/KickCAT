#include "NetworkInterface.h"

namespace kickcat
{
    NetworkInterface::NetworkInterface(std::shared_ptr<AbstractSocket> socket, uint8_t const src_mac[MAC_SIZE])
    : socket_(socket)
    {
        std::copy(src_mac, src_mac + MAC_SIZE, src_mac_);
    }


    void NetworkInterface::write(Frame& frame)
    {
        frame.setSourceMAC(src_mac_);
        int32_t toWrite = frame.finalize();
        int32_t written = socket_->write(frame.data(), toWrite);
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


    void NetworkInterface::read(Frame& frame)
    {
        int32_t read = socket_->read(frame.data(), ETH_MAX_SIZE);
//        printf("socket read %i \n", read);
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
        if (expected < ETH_MIN_SIZE)
        {
            expected = ETH_MIN_SIZE;
        }
        if (read != expected)
        {
//            printf("Read %i, expected %i len %i \n", read, expected, frame.header()->len);
            THROW_ERROR("Wrong number of bytes read");
        }
        frame.clear();

        frame.setIsDatagramAvailable(true);
    }
}
