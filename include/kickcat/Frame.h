#ifndef KICKAT_FRAME_H
#define KICKAT_FRAME_H

#include <memory>

#include "protocol.h"


namespace kickcat
{
    class AbstractSocket;

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

        /// \brief   Reset the internal datagram pointers to beginning of the frame and be ready to read/write a new frame.
        /// \details This context is used to iterate through the datagrams in the frame.
        void resetContext();

        /// \brief   Set the length of the EtherCAT header to 0.
        /// \details It is used to know the size of data to write and to expect when reading.
        ///          It shall be cleared when done writing and when done reading, to increment the len properly
        ///          when reusing the frame to add new datagrams and not expect inconsistent frame size.
        void clear();

        /// \brief finalize the frame to sent it on the network - handle padding and multiple datagram flag
        /// \return the number of bytes to write on the network
        int32_t finalize();

        /// \brief helper to get access on next datagram
        /// \return a tuple with a pointer on the datagram header, a pointer on the datagram data, the working counter
        std::tuple<DatagramHeader const*, uint8_t*, uint16_t> nextDatagram();

        /// \brief helper to peek on a datagram while advancing internal pointer to the next one.
        /// \details This API is designed for network simulation.
        /// \return a tuple with a pointer on the datagram header, a pointer on the datagram data, a pointer on the working counter.
        ///         If return values are nullptr, it means that there is no more datagrams in the frame.
        std::tuple<DatagramHeader*, uint8_t*, uint16_t*> peekDatagram();

        /// \return number of datagram already written in the frame
        int32_t datagramCounter() const { return datagram_counter_; }

        /// \return free space in bytes of the current frame
        int32_t freeSpace() const;

        /// \return true if the frame is full (no more datagram can be added: either there is not sufficient space or max datagram was reached)
        bool isFull() const;

        /// \return true if datagram can be extracted, false otherwise
        bool isDatagramAvailable() const { return is_datagram_available_; }
        void setIsDatagramAvailable() { is_datagram_available_ = true; }

        void setSourceMAC(MAC const& src);

        // helper to access raw frame (mostly for unit testing)
        uint8_t* data() { return frame_.data(); }
        EthercatHeader* header()   { return header_; }
        EthernetHeader* ethernet() { return ethernet_; }

    private:
        EthernetFrame frame_;
        EthernetHeader* ethernet_;
        EthercatHeader* header_;
        uint8_t* first_datagram_;           // First datagram of the frame - immutable (but non const for move() semantic)
        uint8_t* last_datagram_{nullptr};   // Last **written** datagram
        uint8_t* next_datagram_{nullptr};   // Next datagram **to write** or **to read**
        int32_t datagram_counter_{0};       // number of datagram already written
        bool is_datagram_available_{false};
    };

    int32_t readFrame(std::shared_ptr<AbstractSocket> socket, Frame& frame);
    int32_t writeFrame(std::shared_ptr<AbstractSocket> socket, Frame& frame, MAC const& src);
}

#endif
