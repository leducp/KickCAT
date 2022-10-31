#include <gmock/gmock.h>
#include <cstring>

#include "kickcat/AbstractSocket.h"
#include "kickcat/protocol.h"
#include "kickcat/Frame.h"


namespace kickcat
{
    struct FrameContext
    {
        Frame inflight;
        uint8_t* datagram;
        DatagramHeader const* header;
        uint8_t* payload;
    };

    template<typename T>
    struct DatagramCheck
    {
        Command cmd;
        T to_check;
        bool check_payload{true};
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
        MOCK_METHOD(void,    open,  (std::string const& interface, microseconds timeout), (override));
        MOCK_METHOD(void,    setTimeout,  (microseconds timeout), (override));
        MOCK_METHOD(void,    close, (), (noexcept));
        MOCK_METHOD(int32_t, read,  (uint8_t* frame, int32_t frame_size), (override));
        MOCK_METHOD(int32_t, write, (uint8_t const* frame, int32_t frame_size), (override));

        template<typename T>
        void checkSendFrame(Command cmd, T to_check, bool check_payload = true)
        {
            EXPECT_CALL(*this, write(::testing::_, ::testing::_))
            .WillOnce(::testing::Invoke([this, cmd, to_check, check_payload](uint8_t const* data, int32_t data_size)
            {
                // store datagram to forge the answer.
                Frame frame(data, data_size);
                context.inflight = std::move(frame);
                context.datagram = context.inflight.data() + sizeof(EthernetHeader) + sizeof(EthercatHeader);
                context.header = reinterpret_cast<DatagramHeader*>(context.datagram);
                context.payload = context.datagram + sizeof(DatagramHeader);

                if (check_payload)
                {
                    EXPECT_EQ(0, std::memcmp(context.payload, &to_check, sizeof(T)));
                }

                EXPECT_EQ(cmd, context.header->command);
                return data_size;
            }));
        }


        template<typename T>
        void checkSendFrameMultipleDTG(std::vector<DatagramCheck<T>> expected_datagrams)
        {
            EXPECT_CALL(*this, write(::testing::_, ::testing::_))
            .WillOnce(::testing::Invoke([this, expected_datagrams](uint8_t const* data, int32_t data_size)
            {
                // store datagram to forge the answer.
                Frame frame(data, data_size);
                context.inflight = std::move(frame);
                context.datagram = context.inflight.data() + sizeof(EthernetHeader) + sizeof(EthercatHeader);
                context.header = reinterpret_cast<DatagramHeader*>(context.datagram);
                context.payload = context.datagram + sizeof(DatagramHeader);

                int32_t i = 0;
                while (context.inflight.isDatagramAvailable())
                {
                    if (expected_datagrams[i].check_payload)
                    {
                        EXPECT_EQ(0, std::memcmp(context.payload, &expected_datagrams[i].to_check, sizeof(T)));
                    }
                    EXPECT_EQ(expected_datagrams[i].cmd, context.header->command);

                    std::tie(context.header, context.payload, std::ignore) = context.inflight.nextDatagram();
                    i++;
                }

                EXPECT_EQ(expected_datagrams.size(), i);
                return data_size;
            }));
        }


        template<typename T>
        void handleReply(std::vector<T> answers, uint16_t replied_wkc = 1)
        {
            EXPECT_CALL(*this, read(::testing::_, ::testing::_))
            .WillOnce(::testing::Invoke([this, replied_wkc, answers](uint8_t* data, int32_t)
            {
                auto it = answers.begin();
                uint16_t* wkc = reinterpret_cast<uint16_t*>(context.payload + context.header->len);

                DatagramHeader const* current_header = context.header;                     // current header to check loop condition
                do
                {
                    std::memcpy(context.payload, &(*it), sizeof(T));
                    *wkc = replied_wkc;


                    current_header = context.header;                                    // save current header
                    ++it;                                                       // next payload
                    context.datagram = reinterpret_cast<uint8_t*>(wkc) + 2;             // next datagram
                    context.header = reinterpret_cast<DatagramHeader*>(context.datagram);       // next header
                    context.payload = context.datagram + sizeof(DatagramHeader);                // next payload
                    wkc = reinterpret_cast<uint16_t*>(context.payload + context.header->len);   // next wkc
                } while (current_header->multiple == 1);


                int32_t answer_size = context.inflight.finalize();
                std::memcpy(data, context.inflight.data(), answer_size);
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

        // TODO QUEUE of frames
        FrameContext context;
    };
}
