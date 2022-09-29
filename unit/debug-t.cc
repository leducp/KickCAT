#include <gtest/gtest.h>
#include <cstring>

#include "kickcat/Link.h"
#include "kickcat/DebugHelpers.h"
#include "Mocks.h"

using ::testing::Return;
using ::testing::_;
using ::testing::Invoke;
using ::testing::InSequence;

using namespace kickcat;

class LinkDebug : public testing::Test
{
public:

    template<typename T>
    void handleReply(std::vector<T> answers, uint16_t replied_wkc = 1)
    {
        EXPECT_CALL(*io, read(_,_))
        .WillOnce(Invoke([this, replied_wkc, answers](uint8_t* data, int32_t)
        {
            auto it = answers.begin();
            uint16_t* wkc = reinterpret_cast<uint16_t*>(payload + header->len);
            DatagramHeader* current_header = header;                     // current heade rto check loop condition

            do
            {
                std::memcpy(payload, &(*it), sizeof(T));
                *wkc = replied_wkc;

                current_header = header;                                    // save current header
                ++it;                                                       // next payload
                datagram = reinterpret_cast<uint8_t*>(wkc) + 2;             // next datagram
                header = reinterpret_cast<DatagramHeader*>(datagram);       // next header
                payload = datagram + sizeof(DatagramHeader);                // next payload
                wkc = reinterpret_cast<uint16_t*>(payload + header->len);   // next wkc
            } while (current_header->multiple == 1);

            int32_t answer_size = inflight.finalize();
            std::memcpy(data, inflight.data(), answer_size);
            return answer_size;
        }));
    }

    template<typename T>
    void checkSendFrame(Command cmd, T to_check, bool check_payload = true)
    {
        EXPECT_CALL(*io, write(_,_))
        .WillOnce(Invoke([this, cmd, to_check, check_payload](uint8_t const* data, int32_t data_size)
        {
            // store datagram to forge the answer.
            Frame frame(data, data_size);
            inflight = std::move(frame);
            datagram = inflight.data() + sizeof(EthernetHeader) + sizeof(EthercatHeader);
            header = reinterpret_cast<DatagramHeader*>(datagram);
            payload = datagram + sizeof(DatagramHeader);

            if (check_payload)
            {
                EXPECT_EQ(0, std::memcmp(payload, &to_check, sizeof(T)));
            }

            EXPECT_EQ(cmd, header->command);
            return data_size;
        }));
    }

    void checkSendFrame(Command cmd)
    {
        uint8_t skip = 0;
        checkSendFrame(cmd, skip, false);
    }


protected:
    std::shared_ptr<MockSocket> io{ std::make_shared<MockSocket>() };
    Link link{ io };
    Frame inflight;

    uint8_t* datagram;
    DatagramHeader* header;
    uint8_t* payload;
};

TEST_F(LinkDebug, send_get_register)
{
    //Succesful read
    uint16_t value_read = 1;
    checkSendFrame(Command::FPRD);
    handleReply<uint16_t>({0xFF}, 1);
    sendGetRegister(link, 0x00, 0x110, value_read);
    ASSERT_EQ(value_read, 0xFF);

    //Invalid WKC
    checkSendFrame(Command::FPRD);
    handleReply<uint16_t>({0xFF}, 2);

    ASSERT_THROW(sendGetRegister(link, 0x00, 0x110, value_read), Error);
}

TEST_F(LinkDebug, send_write_register)
{
    // Succesful write
    checkSendFrame(Command::FPWR);
    handleReply<uint8_t>({0}, 1);
    sendWriteRegister(link, 0x00, 0x110, 0x00);

    //Invalid WKC
    uint16_t value_write = 0x00;
    checkSendFrame(Command::FPWR);
    handleReply<uint16_t>({0xFF}, 2);
    ASSERT_THROW(sendWriteRegister(link, 0x00, 0x110, value_write), Error);

    //Closed socket
    io->close();
    checkSendFrame(Command::FPWR);
    handleReply<uint16_t>({0xFF}, 2);
    ASSERT_THROW(sendWriteRegister(link, 0x00, 0x110, value_write), Error);

}
