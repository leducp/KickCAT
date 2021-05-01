#include "Frame.h"
#include <cstring>
#include <unistd.h>

#include <fstream> // debug

namespace kickcat
{
    Frame::Frame(uint8_t const src_mac[6])
        : ethernet_{reinterpret_cast<EthernetHeader*>(frame_.data())}
        , header_  {reinterpret_cast<EthercatHeader*>(frame_.data() + sizeof(EthernetHeader))}
        , first_datagram_{frame_.data() + sizeof(EthernetHeader) + sizeof(EthercatHeader)}
        , next_datagram_{first_datagram_}
        , last_datagram_{next_datagram_}
        , datagram_counter_{0}
        , isDatagramAvailable_{false}
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


    Frame::Frame(Frame&& other)
        : frame_{std::move(other.frame_)}
        , ethernet_{reinterpret_cast<EthernetHeader*>(frame_.data())}
        , header_  {reinterpret_cast<EthercatHeader*>(frame_.data() + sizeof(EthernetHeader))}
        , first_datagram_{frame_.data() + sizeof(EthernetHeader) + sizeof(EthercatHeader)}
        , next_datagram_{first_datagram_}
        , last_datagram_{next_datagram_}
        , datagram_counter_{0}
        , isDatagramAvailable_{false}
    {

    }


    int32_t Frame::freeSpace() const
    {
        return ETH_MTU_SIZE - sizeof(EthercatHeader) - (next_datagram_ - first_datagram_);
    }


    bool Frame::isFull() const
    {
        if (datagram_counter_ >= MAX_ETHERCAT_DATAGRAMS)
        {
            return true;
        }

        if (freeSpace() <= (sizeof(DatagramHeader) + sizeof(uint16_t)))
        {
            return true;
        }

        return false;
    }


    void Frame::addDatagram(uint8_t index, enum Command command, uint32_t address, void const* data, uint16_t data_size)
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
                }
            }
        }

        // clear working counter
        pos += data_size;
        std::memset(pos, 0, 2);

        // next datagram position
        pos += 2;

        header_->len += sizeof(DatagramHeader) + data_size + 2; // +2 for wkc
        last_datagram_ = reinterpret_cast<uint8_t*>(header);    // save last datagram header to finalize frame when ready
        next_datagram_ = pos;                                   // set next datagram
        ++datagram_counter_;                                    // one more datagram in the frame to be sent
    }


    void Frame::clear()
    {
        // reset context
        next_datagram_ = first_datagram_;
        last_datagram_ = first_datagram_;
        datagram_counter_ = 0;
        isDatagramAvailable_ = false;
    }


    int32_t Frame::finalize()
    {
        DatagramHeader* header = reinterpret_cast<DatagramHeader*>(last_datagram_);
        header->multiple = 0;           // no more datagram in this frame -> ready to be sent!
        uint8_t* pos = next_datagram_;  // save current position before resetting context (needed to handle padding)

        clear();

        int32_t to_write = header_->len + sizeof(EthernetHeader) + sizeof(EthercatHeader);
        if (to_write < ETH_MIN_SIZE)
        {
            // reset padding
            std::memset(pos, 0, ETH_MIN_SIZE - to_write);
            return ETH_MIN_SIZE;
        }
        return to_write;
    }


    std::tuple<DatagramHeader const*, uint8_t const*, uint16_t> Frame::nextDatagram()
    {
        DatagramHeader const* header = reinterpret_cast<DatagramHeader*>(next_datagram_);
        uint8_t* data = next_datagram_ + sizeof(DatagramHeader);
        uint16_t* wkc = reinterpret_cast<uint16_t*>(data + header->len);
        next_datagram_ = reinterpret_cast<uint8_t*>(wkc) + sizeof(uint16_t);

        if (header->multiple == 0)
        {
            // This was the last datagram of this frame: clear context for future usage
            clear();
        }

        return std::make_tuple(header, data, *wkc);
    }


    void Frame::read(std::shared_ptr<AbstractSocket> socket)
    {
        int32_t read = socket->read(frame_.data(), frame_.size());
        if (read < 0)
        {
            throw std::system_error(errno, std::generic_category());
        }

        // check if the frame is an EtherCAT one. if not, drop it and try again
        if (ethernet_->type != ETH_ETHERCAT_TYPE)
        {
            throw "Invalid frame type";
        }

        int32_t expected = header_->len + sizeof(EthernetHeader) + sizeof(EthercatHeader);
        header_->len = 0; // reset len for future usage
        if (expected < ETH_MIN_SIZE)
        {
            expected = ETH_MIN_SIZE;
        }
        if (read != expected)
        {
            throw "Wrong number of bytes read";
        }

        isDatagramAvailable_ = true;
    }


    void Frame::write(std::shared_ptr<AbstractSocket> socket)
    {
        int32_t toWrite = finalize();
        int32_t written = socket->write(frame_.data(), toWrite);

        if (written < 0)
        {
            throw std::system_error(errno, std::generic_category());
        }

        if (written != toWrite)
        {
            throw "Wrong number of bytes written";
        }
    }


    void Frame::writeThenRead(std::shared_ptr<AbstractSocket> socket)
    {
        write(socket);
        read(socket);
    }
}
