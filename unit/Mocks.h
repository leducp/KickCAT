#include <gmock/gmock.h>
#include <cstring>

#include "kickcat/AbstractSocket.h"
#include "kickcat/protocol.h"
#include "kickcat/Frame.h"


namespace kickcat
{
    class MockSocket : public AbstractSocket
    {
    public:
        MOCK_METHOD(void,    open,  (std::string const& interface, microseconds timeout), (override));
        MOCK_METHOD(void,    setTimeout,  (microseconds timeout), (override));
        MOCK_METHOD(void,    close, (), (noexcept));
        MOCK_METHOD(int32_t, read,  (uint8_t* frame, int32_t frame_size), (override));
        MOCK_METHOD(int32_t, write, (uint8_t const* frame, int32_t frame_size), (override));
    };


    class MockBus
    {
    public:
        template<typename T>
        void checkSendFrame(std::shared_ptr<MockSocket> io, Command cmd, T to_check, bool check_payload = true)
        {
            EXPECT_CALL(*io, write(::testing::_, ::testing::_))
            .WillOnce(::testing::Invoke([this, cmd, to_check, check_payload](uint8_t const* data, int32_t data_size)
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


        template<typename T>
        void handleReply(std::shared_ptr<MockSocket> io, std::vector<T> answers, uint16_t replied_wkc = 1)
        {
            EXPECT_CALL(*io, read(::testing::_, ::testing::_))
            .WillOnce(::testing::Invoke([this, replied_wkc, answers](uint8_t* data, int32_t)
            {
                auto it = answers.begin();
                uint16_t* wkc = reinterpret_cast<uint16_t*>(payload + header->len);
                DatagramHeader* current_header = header;                     // current header to check loop condition
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

        Frame inflight;
        uint8_t* datagram;
        DatagramHeader* header;
        uint8_t* payload;
    };
}
