#include <gtest/gtest.h>
#include <cstring>
#include "Mocks.h"

#include "kickcat/LinkRedundancy.h"

using ::testing::_;
using ::testing::Invoke;

using namespace kickcat;

class LinkTest : public testing::Test
{
public:
    void reportRedundancy()
    {
        printf("Redundancy has been activated\n");
        is_redundancy_activated = true;
    }

protected:
    std::shared_ptr<MockSocket> io_nominal{ std::make_shared<MockSocket>() };
    std::shared_ptr<MockSocket> io_redundancy{ std::make_shared<MockSocket>() };
    LinkRedundancy link{ io_nominal, io_redundancy, std::bind(&LinkTest::reportRedundancy, this), PRIMARY_IF_MAC, SECONDARY_IF_MAC};

    bool is_redundancy_activated{false};
};


TEST_F(LinkTest, writeThenRead_NomOK_RedOK)
{
    Frame frame;

    EXPECT_CALL(*io_nominal, write(_,_))
    .WillOnce(Invoke([&](uint8_t const*, int32_t)
    {
        return ETH_MIN_SIZE;
    }));

    EXPECT_CALL(*io_redundancy, read(_,_))
    .WillOnce(Invoke([&](uint8_t const*, int32_t)
    {
        ASSERT_EQ(frame.ethernet()->src_mac, PRIMARY_IF_MAC);
        return ETH_MIN_SIZE;
    }));


    link.writeThenRead(frame);
}
