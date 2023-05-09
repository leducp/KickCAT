#include <cstring>

#include "Frame.h"
#include "AbstractSocket.h"

namespace kickcat
{
    Frame::Frame()
        : ethernet_{pointData<EthernetHeader>(frame_.data())}
        , header_  {pointData<EthercatHeader>(ethernet_)}
        , first_datagram_{pointData<uint8_t>(header_)}
    {
        resetContext();

        // cleanup memory
        std::memset(frame_.data(), 0, frame_.size());

        // prepare Ethernet header once for the all future communication
        std::memset(ethernet_->dst, 0xFF,    sizeof(ethernet_->dst));      // broadcast

        // type is EtherCAT
        ethernet_->type = ETH_ETHERCAT_TYPE;

        // Protocol is always EtherCAT DLPDU (Data Link Protocol Data Unit)
        header_->type = EthercatType::ETHERCAT;
        header_->len  = 0;
    }


    Frame::Frame(uint8_t const* data, int32_t data_size)
        : ethernet_ { pointData<EthernetHeader>(frame_.data())  }
        , header_   { pointData<EthercatHeader>(ethernet_)      }
        , first_datagram_{ pointData<uint8_t>(header_)          }
    {
        std::memcpy(frame_.data(), data, data_size);
        resetContext();
        is_datagram_available_ = true;
    }


    Frame::Frame(Frame&& other)
    {
        *this = std::move(other);
    }


    Frame& Frame::operator=(Frame&& other)
    {
        frame_ = std::move(other.frame_);
        ethernet_ = pointData<EthernetHeader>(frame_.data());
        header_   = pointData<EthercatHeader>(ethernet_);
        first_datagram_ = pointData<uint8_t>(header_);
        next_datagram_  = other.next_datagram_;
        last_datagram_  = other.last_datagram_;
        datagram_counter_ = other.datagram_counter_;
        is_datagram_available_ = other.is_datagram_available_;
        return *this;
    }


    int32_t Frame::freeSpace() const
    {
        int32_t size = static_cast<int32_t>(ETH_MTU_SIZE - sizeof(EthercatHeader) - (next_datagram_ - first_datagram_));
        return size;
    }


    bool Frame::isFull() const
    {
        if (datagram_counter_ >= MAX_ETHERCAT_DATAGRAMS)
        {
            return true;
        }

        if (freeSpace() < datagram_size(0)) // no more space for a datagram metadata ?
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
        header->len = data_size & 0x7ff;
        header->circulating = 0;
        header->multiple = 1;   // by default, consider that more datagrams will follow
        header->irq = 0;        // Clear IRQ field to ensure correct feedback (slaves OR event bits if not masked)

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

        header_->len += (sizeof(DatagramHeader) + data_size + 2) & 0x7ff; // +2 for wkc
        last_datagram_ = next_datagram_;                                  // save last datagram header to finalize frame when ready
        next_datagram_ = pos;                                             // set next datagram
        ++datagram_counter_;                                              // one more datagram in the frame to be sent
    }


    void Frame::resetContext()
    {
        next_datagram_ = first_datagram_;
        last_datagram_ = first_datagram_;
        datagram_counter_ = 0;
        is_datagram_available_ = false;
    }


    void Frame::clear()
    {
        header_->len = 0;
    }


    int32_t Frame::finalize()
    {
        DatagramHeader* header = reinterpret_cast<DatagramHeader*>(last_datagram_);
        header->multiple = 0;           // no more datagram in this frame -> ready to be sent!
        uint8_t* pos = next_datagram_;  // save current position before resetting context (needed to handle padding)

        resetContext();

        int32_t to_write = header_->len + sizeof(EthernetHeader) + sizeof(EthercatHeader);
        if (to_write < ETH_MIN_SIZE)
        {
            // reset padding
            std::memset(pos, 0, ETH_MIN_SIZE - to_write);
            return ETH_MIN_SIZE;
        }
        return to_write;
    }


    std::tuple<DatagramHeader const*, uint8_t*, uint16_t> Frame::nextDatagram()
    {
        DatagramHeader const* header = reinterpret_cast<DatagramHeader*>(next_datagram_);
        uint8_t* data = next_datagram_ + sizeof(DatagramHeader);
        uint8_t* wkc_addr = data + header->len;
        uint16_t wkc;
        std::memcpy(&wkc, wkc_addr, sizeof(uint16_t));
        next_datagram_ = wkc_addr + sizeof(uint16_t);

        if (header->multiple == 0)
        {
            // This was the last datagram of this frame: clear context for future usage
            resetContext();
        }

        return std::make_tuple(header, data, wkc);
    }


    std::tuple<DatagramHeader*, uint8_t*, uint16_t*> Frame::peekDatagram()
    {
        if (next_datagram_ == nullptr)
        {
            return std::make_tuple(nullptr, nullptr, nullptr);
        }

        DatagramHeader* header = reinterpret_cast<DatagramHeader*>(next_datagram_);
        uint8_t* data = next_datagram_ + sizeof(DatagramHeader);
        uint16_t* wkc = reinterpret_cast<uint16_t*>(data + header->len);
        next_datagram_ = data + header->len + sizeof(uint16_t);

        if (header->multiple == 0)
        {
            // This was the last datagram of this frame: cannot extract more
            next_datagram_ = nullptr;
        }

        return std::make_tuple(header, data, wkc);
    }


    void Frame::setSourceMAC(MAC const& src)
    {
        std::memcpy(ethernet_->src, src, sizeof(ethernet_->src));
    }


    int32_t readFrame(std::shared_ptr<AbstractSocket> socket, Frame& frame)
    {
        int32_t read = socket->read(frame.data(), ETH_MAX_SIZE);
        if (read < 0)
        {
            DEBUG_PRINT("read() failed\n");
            return read;
        }

        if (read == 0)
        {
            // Nothing to do : special case for SocketNull. Not possible on raw ethernet.
            return read;
        }

        // check if the frame is an EtherCAT one. if not, drop it and try again
        if (frame.ethernet()->type != ETH_ETHERCAT_TYPE)
        {
            DEBUG_PRINT("Invalid frame type\n");
            return -1;
        }

        int32_t expected = frame.header()->len + sizeof(EthernetHeader) + sizeof(EthercatHeader);
        frame.clear();
        if (expected < ETH_MIN_SIZE)
        {
            expected = ETH_MIN_SIZE;
        }
        if (read != expected)
        {
            DEBUG_PRINT("Wrong number of bytes read\n");
            return -1;
        }

        frame.setIsDatagramAvailable();
        return read;
    }


    int32_t writeFrame(std::shared_ptr<AbstractSocket> socket, Frame& frame, MAC const& src)
    {
        frame.setSourceMAC(src);
        int32_t toWrite = frame.finalize();
        int32_t written = socket->write(frame.data(), toWrite);
        frame.clear();

        if (written < 0)
        {
            return -1;
        }

        if (written != toWrite)
        {
            errno = EIO;
            return -1;
        }

        return written;
    }
}
