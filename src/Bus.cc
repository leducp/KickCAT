#include "Bus.h"
#include "AbstractSocket.h"
#include <unistd.h>

#include <cstring>

namespace kickcat
{
    Bus::Bus(std::unique_ptr<AbstractSocket> socket)
        : socket_{std::move(socket)}
    {
        // prepare Ethernet header once for the all future communication

        // destination is broadcast
        for (int32_t i = 0; i < 6; ++i)
        {
            frame_.data()[i] = 0xFF;
        }

        // source is primary NIC MAC address (for debug when reading trace)
        for (int32_t i = 0; i < 6; ++i)
        {
            frame_.data()[i + 6] = PRIMARY_IF_MAC[i];
        }

        // type is EtherCAT
        frame_.data()[12] = static_cast<uint8_t>((ETH_ETHERCAT_TYPE & 0xFF00) >> 8);
        frame_.data()[13] = static_cast<uint8_t>((ETH_ETHERCAT_TYPE & 0x00FF) >> 0);

        // set EtherCAT header pointer at the right position
        header_ = reinterpret_cast<EthercatHeader*>(frame_.data() + ETH_HEADER_SIZE);
        header_->type = 1;

        // set the EtherCAT payload pointer at the right position
        ethercat_payload_ = frame_.data() + ETH_HEADER_SIZE + sizeof(EthercatHeader);
    }


    int32_t Bus::getSlavesOnNetwork()
    {
        std::memset(ethercat_payload_, 0, 60);

        DatagramHeader* header = reinterpret_cast<DatagramHeader*>(ethercat_payload_);
        header->command = Command::BWR;
        header->index = 0x80;
        header->address[0] = 0x0F;
        header->address[1] = 0x00;
        header->address[2] = 0x01;
        header->address[3] = 0x01;
        header->len = 1;

        uint8_t* payload = ethercat_payload_ + sizeof(DatagramHeader);
        payload[0] = 0;
        uint16_t* wkc = reinterpret_cast<uint16_t*>(ethercat_payload_ + sizeof(DatagramHeader) + 1);
        *wkc = 0;

        int32_t length = sizeof(DatagramHeader) + 1 + 2; //+2 working counter
        header_->len = length;

        Error err = socket_->write(frame_.data(), 60);
        if (err) { err.what(); }

        for (int32_t i = 0; i < 10; ++i)
        {
            usleep(1000);

            err = socket_->read(frame_.data(), 60);
            if (err) { err.what(); }
            else break;
        }

        printf("----> There is %d slaves on the bus\n", *wkc);

        return 0;
    }
}
