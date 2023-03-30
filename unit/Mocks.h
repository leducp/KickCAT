#include <gmock/gmock.h>
#include <cstring>
#include <queue>

#include "kickcat/protocol.h"
#include "kickcat/Frame.h"
#include "kickcat/AbstractSocket.h"
#include "kickcat/AbstractDiagSocket.h"

namespace kickcat
{
    struct FrameContext
    {
        Frame inflight;
        uint8_t* datagram;
        DatagramHeader* header;
        uint8_t* payload;
    };

    template<typename T>
    struct DatagramCheck
    {
        Command cmd{};
        T to_check{};
        bool check_payload{true};
        DatagramCheck() = default;
        DatagramCheck(Command cmd_, T to_check_, bool check_payload_ = true)
            : cmd(cmd_)
            , check_payload(check_payload_)
        {
            std::memcpy(&to_check, &to_check_, sizeof(T));
        }
    };

    class MockSocket : public AbstractSocket
    {
    public:
        MOCK_METHOD(void,    open,  (std::string const& interface), (override));
        MOCK_METHOD(void,    setTimeout,  (nanoseconds timeout), (override));
        MOCK_METHOD(void,    close, (), (noexcept));
        MOCK_METHOD(int32_t, read,  (uint8_t* frame, int32_t frame_size), (override));
        MOCK_METHOD(int32_t, write, (uint8_t const* frame, int32_t frame_size), (override));

        template<typename T>
        void checkSendFrame(std::vector<DatagramCheck<T>> expected_datagrams)
        {
            EXPECT_CALL(*this, write(::testing::_, ::testing::_))
            .WillOnce(::testing::Invoke([this, expected_datagrams](uint8_t const* data, int32_t data_size)
            {
                // store datagram to forge the answer for handle reply.
                contexts_.emplace();
                Frame frameContext(data, data_size);
                contexts_.back().inflight = std::move(frameContext);
                contexts_.back().datagram = contexts_.back().inflight.data() + sizeof(EthernetHeader) + sizeof(EthercatHeader);
                contexts_.back().header = reinterpret_cast<DatagramHeader*>(contexts_.back().datagram);
                contexts_.back().payload = contexts_.back().datagram + sizeof(DatagramHeader);

                // Check the content of the sent frame:
                Frame frameCheck(data, data_size);
                uint8_t* datagram = frameCheck.data() + sizeof(EthernetHeader) + sizeof(EthercatHeader);
                DatagramHeader const* header = reinterpret_cast<DatagramHeader*>(datagram);
                uint8_t* payload = datagram + sizeof(DatagramHeader);
                int32_t i = 0;
                while (frameCheck.isDatagramAvailable())
                {
                    if (expected_datagrams[i].check_payload)
                    {
                        EXPECT_EQ(0, std::memcmp(payload, &expected_datagrams[i].to_check, sizeof(T)));
                    }
                    EXPECT_EQ(expected_datagrams[i].cmd, header->command);

                    std::tie(header, payload, std::ignore) = frameCheck.nextDatagram();
                    i++;
                }
                EXPECT_EQ(expected_datagrams.size(), i);
                return data_size;
            }));
        }


        template<typename T>
        void handleReply(std::vector<T> answers, uint16_t replied_wkc = 1, uint16_t irq = 0)
        {
            EXPECT_CALL(*this, read(::testing::_, ::testing::_))
            .WillOnce(::testing::Invoke([this, replied_wkc, irq, answers](uint8_t* data, int32_t)
            {
                auto it = answers.begin();
                uint16_t* wkc = reinterpret_cast<uint16_t*>(contexts_.front().payload + contexts_.front().header->len);

                DatagramHeader const* current_header = contexts_.front().header;                     // current header to check loop condition
                do
                {
                    std::memcpy(contexts_.front().payload, &(*it), sizeof(T));
                    *wkc = replied_wkc;
                    contexts_.front().header->irq = irq;

                    current_header = contexts_.front().header;                  // save current header

                    ++it;                                                       // next payload
                    contexts_.front().datagram = reinterpret_cast<uint8_t*>(wkc) + 2;             // next datagram
                    contexts_.front().header = reinterpret_cast<DatagramHeader*>(contexts_.front().datagram);       // next header
                    contexts_.front().payload = contexts_.front().datagram + sizeof(DatagramHeader);                // next payload
                    wkc = reinterpret_cast<uint16_t*>(contexts_.front().payload + contexts_.front().header->len);   // next wkc
                } while (current_header->multiple == 1);

                int32_t answer_size = contexts_.front().inflight.finalize();
                std::memcpy(data, contexts_.front().inflight.data(), answer_size);
                contexts_.pop();
                return answer_size;
            }));
        }

        void readError()
        {
            EXPECT_CALL(*this, read(::testing::_, ::testing::_))
            .WillOnce(::testing::Invoke([](uint8_t* , int32_t)
            {
                return -1;
            }));
        }

        std::queue<FrameContext> contexts_;
    };


    class MockDiagSocket : public AbstractDiagSocket
    {
    public:
        MOCK_METHOD(void,    open,  (), (override));
        MOCK_METHOD(void,    close, (), (noexcept));
        MOCK_METHOD((std::tuple<int32_t, uint16_t>), recv,   (uint8_t* frame, int32_t frame_size), (override));
        MOCK_METHOD(int32_t, sendTo, (uint8_t const* frame, int32_t frame_size, uint16_t), (override));

        uint16_t nextIndex() { AbstractDiagSocket::nextIndex(); return index_; }
    };
}
