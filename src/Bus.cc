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
        current_pos_ = ethercat_payload_;

        header_recv_ = reinterpret_cast<EthercatHeader*>(frame_received_.data() + ETH_HEADER_SIZE);
        ethercat_payload_recv_ = frame_received_.data() + ETH_HEADER_SIZE + sizeof(EthercatHeader);
        current_pos_recv_ = ethercat_payload_recv_;
    }


    int32_t Bus::freeSpace() const
    {
        return ETH_MTU_SIZE - sizeof(EthercatHeader) - (current_pos_ - ethercat_payload_);
    }


    void Bus::addDatagram(uint8_t index, enum Command command, uint16_t ADP, uint16_t ADO, void* data, uint16_t data_size)
    {
        DatagramHeader* header = reinterpret_cast<DatagramHeader*>(current_pos_);
        header->index = index;
        header->command = command;
        header->address[0] = (ADP & 0x00FF) >> 0;
        header->address[1] = (ADP & 0xFF00) >> 8;
        header->address[2] = (ADO & 0x00FF) >> 0;
        header->address[3] = (ADO & 0xFF00) >> 8;
        header->len = data_size;
        header->multiple = 1; // by default, consider that more datagrams will follow

        current_pos_ += sizeof(DatagramHeader);

        if (data_size > 0)
        {
            switch (command)
            {
                case Command::NOP:
                case Command::BRD:
                case Command::APRD:
                case Command::FPRD:
                case Command::LRD:
                {
                    // no-op or read only command: clear the area
                    std::memset(current_pos_, 0, data_size);
                    break;
                }
                default:
                {
                    // commands that require to write something: copy data
                    std::memcpy(current_pos_, data, data_size);
                    current_pos_ += data_size;
                }
            }
        }

        // clear working counter
        std::memset(current_pos_, 0, 2);
        current_pos_ += 2;

        header_->len += sizeof(DatagramHeader) + data_size + 2; // +2 for wkc
        last_datagram_ = header;    // save last datagram header to finalize frame when ready
        ++datagram_counter_;        // one more datagram in the frame to be sent
    }


    int32_t Bus::finalizeFrame()
    {
        last_datagram_->multiple = 0; // no more datagram in this frame -> ready to be sent!
        uint8_t* saved_current_pos = current_pos_; // save current position before resetting context (needed to handle padding)

        // reset context
        current_pos_ = ethercat_payload_;

        if (header_->len < ETH_MIN_SIZE)
        {
            // reset padding
            std::memset(saved_current_pos, 0, ETH_MIN_SIZE - header_->len);
            return ETH_MIN_SIZE;
        }
        return header_->len;
    }


    std::tuple<DatagramHeader const*, uint8_t const*, uint16_t> Bus::readNextDatagram()
    {
        DatagramHeader const* header = reinterpret_cast<DatagramHeader*>(current_pos_recv_);
        uint8_t const* data = current_pos_recv_ + sizeof(DatagramHeader);
        uint16_t const* wkc = reinterpret_cast<uint16_t const*>(data + header->len);
        return std::make_tuple(header, data, *wkc);
    }

    int32_t Bus::getSlavesOnNetwork()
    {
        uint8_t param = 0;
        addDatagram(1, Command::BWR, 0, 0x0101, &param, 1);
        int32_t toWrite = finalizeFrame();

        Error err = socket_->write(frame_.data(), toWrite);
        if (err) { err.what(); }

        err = socket_->read(frame_received_.data(), toWrite);
        if (err) { err.what(); }

        auto [header, data, wkc] = readNextDatagram();

        printf("----> There is %d slaves on the bus %02x%02x %02x%02x\n", wkc, header->address[0], header->address[1], header->address[2], header->address[3]);

        return 0;
    }
}
