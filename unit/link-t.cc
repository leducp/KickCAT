#include <gtest/gtest.h>
#include "Link.h"
#include "Mocks.h"

using ::testing::Return;
using ::testing::_;
using ::testing::Invoke;
using ::testing::InSequence;

using namespace kickcat;

TEST(Link, add_datagram)
{
    std::shared_ptr<MockSocket> io = std::make_shared<MockSocket>();
    Link link{io};
    int32_t available_datagrams = 0;

    {
        InSequence s;

        EXPECT_CALL(*io, write(_,_))
        .WillOnce(Invoke([&available_datagrams](uint8_t const* data, int32_t data_size)
        {
            Frame frame(data, data_size);
            while(frame.isDatagramAvailable())
            {
                auto [header, d, wkc] = frame.nextDatagram();
                (void) d;
                (void) wkc;
                EXPECT_EQ(available_datagrams, header->index);
                available_datagrams++;
            }
            EXPECT_EQ(15, available_datagrams);

            return data_size;
        }));

        EXPECT_CALL(*io, write(_,_))
        .WillOnce(Invoke([&available_datagrams](uint8_t const* data, int32_t data_size)
        {
            Frame frame(data, data_size);
            while(frame.isDatagramAvailable())
            {
                auto [header, d, wkc] = frame.nextDatagram();
                (void) d;
                (void) wkc;
                EXPECT_EQ(available_datagrams, header->index);
                available_datagrams++;
            }
            EXPECT_EQ(20, available_datagrams);

            return data_size;
        }));
    }

    uint8_t data;
    for (int32_t i=0; i<20; ++i)
    {
        link.addDatagram(Command::BRD, 0, data, [](DatagramHeader const*, uint8_t const*, uint16_t){ return false; }, [](){});
    }

    link.finalizeDatagrams();
}
