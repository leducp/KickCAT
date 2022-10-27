#ifndef KICKAT_FRAME_H
#define KICKAT_FRAME_H

#include <array>
#include <memory>

#include "protocol.h"


namespace kickcat
{
    // Definition of an Ethernet frame (maximal size)
    using EthernetFrame = std::array<uint8_t, ETH_MAX_SIZE>;

    class Frame
    {
    public:
        Frame();
        Frame(Frame&& other);
        Frame(uint8_t const* data, int32_t data_size);
        Frame(Frame const& other) = delete;
        Frame& operator=(Frame&& other);
        ~Frame() = default;

        /// \brief Add a datagram in the frame
        /// \warning Doesn't check anything - max datagram nor max size!
        /// \return true if full after adding datagram, false otherwise
        void addDatagram(uint8_t index, enum Command command, uint32_t address, void const* data, uint16_t data_size);

        void resetContext();
        void clear();

        /// \brief finalize the frame to sent it on the network - handle padding and multiple datagram flag
        /// \return the number of bytes to write on the network
        int32_t finalize();

        /// \brief helper to get access on next datagram
        /// \return a tuple with a pointer on the datagram header, a pointer on the datagram data, the working counter
        std::tuple<DatagramHeader const*, uint8_t*, uint16_t> nextDatagram();

        /// \return number of datagram already written in the frame
        int32_t datagramCounter() const { return datagram_counter_; }

        /// \return free space in bytes of the current frame
        int32_t freeSpace() const;

        /// \return true if the frame is full (no more datagram can be added: either there is not sufficient space or max datagram was reached)
        bool isFull() const;

        /// \return true if datagram can be extracted, false otherwise
        bool isDatagramAvailable() const { return is_datagram_available_; }
        void setIsDatagramAvailable() { is_datagram_available_ = true; }

        void setSourceMAC(uint8_t const src_mac[MAC_SIZE]);

        // helper to access raw frame (mostly for unit testing)
        uint8_t* data() { return frame_.data(); }
        EthercatHeader* header()   { return header_; }
        EthernetHeader* ethernet() { return ethernet_; }



        /// For writeThenRead
        uint8_t* last_datagram_{nullptr};   // Last **written** datagram
        uint8_t* next_datagram_{nullptr};   // Next datagram **to write** or **to read**
    private:
        EthernetFrame frame_;
        EthernetHeader* ethernet_;
        EthercatHeader* header_;
        uint8_t* first_datagram_;           // First datagram of the frame - immutable (but non const for move() semantic)
        int32_t datagram_counter_{0};       // number of datagram already written
        bool is_datagram_available_{false};
    };
}

#endif
