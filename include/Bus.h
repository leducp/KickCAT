#ifndef KICKCAT_BUS_H
#define KICKCAT_BUS_H

#include <memory>
#include <tuple>

#include "Error.h"
#include "protocol.h"

namespace kickcat
{
    class AbstractSocket;

    class Bus
    {
    public:
        Bus(std::unique_ptr<AbstractSocket> socket);
        ~Bus() = default;

        int32_t getSlavesOnNetwork();

    private:
        /// \return free space in bytes of the current frame
        int32_t freeSpace() const;

        /// \brief Add a datagram in the frame
        /// \warning Doesn't check anything - max datagram nor max size!
        void addDatagram(uint8_t index, enum Command command, uint16_t ADP, uint16_t ADO, void* data, uint16_t data_size);

        /// \brief finalize the frame to sent it on the network - handle padding and multiple datagram flag
        /// \return the number of bytes to write on the network
        int32_t finalizeFrame();

        /// \brief helper to get access on next datagram
        /// \return a tuple with a pointer on the datagram header, a pointer on the datagram data, the working counter
        std::tuple<DatagramHeader const*, uint8_t const*, uint16_t> readNextDatagram();


        std::unique_ptr<AbstractSocket> socket_;
        std::array<uint8_t, ETH_MAX_SIZE> frame_;
        EthercatHeader* header_;
        uint8_t* ethercat_payload_;
        uint8_t* current_pos_;

        DatagramHeader* last_datagram_;
        int32_t datagram_counter_{0};


        std::array<uint8_t, ETH_MAX_SIZE> frame_received_;
        EthercatHeader* header_recv_;
        uint8_t* ethercat_payload_recv_;
        uint8_t* current_pos_recv_;
    };
}

#endif
