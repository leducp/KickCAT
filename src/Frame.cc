#include "Frame.h"
#include <cstring>

namespace kickcat
{
    Frame::Frame(uint8_t const src_mac[6])
        : ethernet_{reinterpret_cast<EthernetHeader*>(frame_.data())}
        , header_  {reinterpret_cast<EthercatHeader*>(frame_.data() + sizeof(EthernetHeader))}
        , first_datagram_{frame_.data() + sizeof(EthernetHeader) + sizeof(EthercatHeader)}
        , next_datagram_{first_datagram_}
        , last_datagram_{next_datagram_}
        , datagram_counter_{0}
    {
        // prepare Ethernet header once for the all future communication
        std::memset(ethernet_->dst_mac, 0xFF, sizeof(ethernet_->dst_mac));      // broadcast
        std::memcpy(ethernet_->src_mac, src_mac, sizeof(ethernet_->src_mac));

        // type is EtherCAT
        ethernet_->type = ETH_ETHERCAT_TYPE;

        // EtherCAT type is always 'command'
        header_->type = 1;
        header_->len  = 0;
    }

    int32_t Frame::freeSpace() const
    {
        return ETH_MTU_SIZE - sizeof(EthercatHeader) - (next_datagram_ - first_datagram_);
    }


    void Frame::addDatagram(uint8_t index, enum Command command, uint32_t address, void* data, uint16_t data_size)
    {
        DatagramHeader* header = reinterpret_cast<DatagramHeader*>(next_datagram_);
        uint8_t* pos = next_datagram_;

        header->index = index;
        header->command = command;
        header->address = address;
        header->len = data_size;
        header->multiple = 1;   // by default, consider that more datagrams will follow
        header->IRQ = 0;        //TODO what's that ?

        pos += sizeof(DatagramHeader);

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
                    std::memset(pos, 0, data_size);
                    break;
                }
                default:
                {
                    // commands that require to write something: copy data
                    std::memcpy(pos, data, data_size);
                    pos += data_size;
                }
            }
        }

        // clear working counter
        std::memset(pos, 0, 2);
        pos += 2;

        header_->len += sizeof(DatagramHeader) + data_size + 2; // +2 for wkc
        last_datagram_ = reinterpret_cast<uint8_t*>(header);    // save last datagram header to finalize frame when ready
        next_datagram_ = pos;    // save current
        ++datagram_counter_;     // one more datagram in the frame to be sent
    }


    int32_t Frame::finalize()
    {
        DatagramHeader* header = reinterpret_cast<DatagramHeader*>(last_datagram_);
        header->multiple = 0;           // no more datagram in this frame -> ready to be sent!
        uint8_t* pos = next_datagram_;  // save current position before resetting context (needed to handle padding)

        // reset context
        next_datagram_ = first_datagram_;
        last_datagram_ = first_datagram_;
        datagram_counter_ = 0;

        if (header_->len < ETH_MIN_SIZE)
        {
            // reset padding
            std::memset(pos, 0, ETH_MIN_SIZE - header_->len);
            return ETH_MIN_SIZE;
        }
        return header_->len;
    }


    std::tuple<DatagramHeader const*, uint8_t const*, uint16_t> Frame::nextDatagram()
    {
        DatagramHeader const* header = reinterpret_cast<DatagramHeader*>(next_datagram_);
        uint8_t const* data = next_datagram_ + sizeof(DatagramHeader);
        uint16_t const* wkc = reinterpret_cast<uint16_t const*>(data + header->len);
        return std::make_tuple(header, data, *wkc);
    }


    Error Frame::read(std::shared_ptr<AbstractSocket> socket)
    {
        next_datagram_ = first_datagram_;
        int32_t read = socket->read(frame_.data(), frame_.size());
        printf("read %d\n", read);
        if (read < 0)
        {
            return EERROR(std::strerror(errno));
        }

        // check if the frame is an EtherCAT one. if not, drop it and try again
        if (ethernet_->type != ETH_ETHERCAT_TYPE)
        {
            return EERROR("Wrong frame type");
        }


        int32_t expected = header_->len + sizeof(EthernetHeader) + sizeof(EthercatHeader);
        if (expected < ETH_MIN_SIZE)
        {
            expected = ETH_MIN_SIZE;
        }
        if (read != expected)
        {
            return EERROR("wrong number of read bytes: expected " + std::to_string(expected) + " got " + std::to_string(read));
        }

        return ESUCCESS;
    }


    Error Frame::write(std::shared_ptr<AbstractSocket> socket)
    {
        int32_t toWrite = finalize();
        int32_t written = socket->write(frame_.data(), toWrite);
        if (written < 0)
        {
            return EERROR(std::strerror(errno));
        }

        if (written != toWrite)
        {
            return EERROR("wrong number of written bytes: expected " + std::to_string(toWrite) + ", written  " + std::to_string(written));
        }

        printf("written %d\n", written);

        return ESUCCESS;
    }
}
