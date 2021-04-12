#ifndef KICKAT_FRAME_H
#define KICKAT_FRAME_H

#include <array>
#include <memory>

#include "protocol.h"
#include "AbstractSocket.h"


namespace kickcat
{
    using EthernetFrame = std::array<uint8_t, ETH_MAX_SIZE>;

    class Frame
    {
    public:
        Frame(uint8_t const src_mac[6]);
        ~Frame() = default;

        /// \brief Add a datagram in the frame
        /// \warning Doesn't check anything - max datagram nor max size!
        void addDatagram(uint8_t index, enum Command command, uint32_t address, void const* data, uint16_t data_size);

        template<typename T>
        void addDatagram(uint8_t index, enum Command command, uint32_t address, T const& data)
        {
            addDatagram(index, command, address, &data, sizeof(data));
        }

        /// \brief finalize the frame to sent it on the network - handle padding and multiple datagram flag
        /// \return the number of bytes to write on the network
        int32_t finalize();

        /// \brief helper to get access on next datagram
        /// \return a tuple with a pointer on the datagram header, a pointer on the datagram data, the working counter
        std::tuple<DatagramHeader const*, uint8_t const*, uint16_t> nextDatagram();

        EthernetFrame const& frame() const { return frame_; }

        /// \return number of datagram already written in the frame
        int32_t datagramCounter() const { return datagram_counter_; }

        /// \return free space in bytes of the current frame
        int32_t freeSpace() const;

        /// \return true if the frame is full (no more datagram can be added: either there is not sufficient space or max datagram was reached)
        bool isFull() const;

        // handle bus access
        Error read(std::shared_ptr<AbstractSocket> socket);
        Error write(std::shared_ptr<AbstractSocket> socket);
        Error writeThenRead(std::shared_ptr<AbstractSocket> socket);

    private:
        EthernetFrame frame_;
        EthernetHeader* const ethernet_;
        EthercatHeader* const header_;
        uint8_t* const first_datagram_; // First datagram of the frame - immutable
        uint8_t* last_datagram_;        // Last **written** datagram
        uint8_t* next_datagram_;        // Next datagram **to write** or **to read**
        int32_t datagram_counter_;      // number of datagram already written
    };
}

#endif
