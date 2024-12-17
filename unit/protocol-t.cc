#include <gtest/gtest.h>
#include "kickcat/protocol.h"

using namespace kickcat;

// no real logic here, just check that code to string functions returns valid result (right formed C string with a few caracters)

TEST(Protocol, ALStatus_to_string)
{
    for (int32_t i = 0; i < UINT16_MAX; ++i) // AL status code is defined on 16bits
    {
        char const* text = ALStatus_to_string(i);
        ASSERT_EQ(5, strnlen(text, 5));
    }
}


TEST(Protocol, State_to_string)
{
    for (uint8_t i = 0; i < UINT8_MAX; ++i) // State code is defined on 8bits
    {
        char const* text = toString(static_cast<State>(i));
        ASSERT_EQ(4, strnlen(text, 4));
    }
}


TEST(Protocol, Command_to_string)
{
    for (uint8_t i = 0; i < UINT8_MAX; ++i) // Command code is defined on 8bits
    {
        char const* text = toString(static_cast<Command>(i));
        ASSERT_EQ(4, strnlen(text, 4));
    }
}


TEST(Protocol, SyncManageType_to_string)
{
    for (uint8_t i = 0; i < UINT8_MAX; ++i)
    {
        char const* text = toString(static_cast<SyncManagerType>(i));
        ASSERT_EQ(4, strnlen(text, 4));
    }
}

TEST(Protocol, mailbox_error_to_string)
{
    for (uint16_t i = 0; i < UINT16_MAX; ++i) // Mailbox error code is defined on 16bits
    {
        char const* text = mailbox::Error::toString(i);
        ASSERT_EQ(4, strnlen(text, 4));
    }
}

TEST(Protocol, DatagramHeader_to_string)
{
    DatagramHeader header;
    header.command = Command::LRW;
    header.index = 17;
    header.len   = 42;
    header.circulating = true;
    header.multiple = false;
    header.irq = EcatEvent::DL_STATUS;
    std::string result = toString(header);

    ASSERT_GT(result.size(), 90);
}


TEST(Protocol, hton)
{
    uint16_t host_16 = 0xCAFE;
    uint32_t host_32 = 0xCAFEDECA;
    uint32_t host_64 = 0;

    uint16_t network_16 = hton<uint16_t>(host_16);
    uint32_t network_32 = hton<uint32_t>(host_32);

    ASSERT_EQ(0xFECA,     network_16);
    ASSERT_EQ(0xCADEFECA, network_32);
    ASSERT_THROW(hton<uint64_t>(host_64), kickcat::Error);
}


TEST(Protocol, addressSM)
{
    ASSERT_EQ(reg::SYNC_MANAGER_0, addressSM(0));
    ASSERT_EQ(reg::SYNC_MANAGER_1, addressSM(1));
    ASSERT_EQ(reg::SYNC_MANAGER_2, addressSM(2));
    ASSERT_EQ(reg::SYNC_MANAGER_3, addressSM(3));
}


TEST(Protocol, address_management)
{
    uint32_t address = createAddress(3, reg::SYNC_MANAGER_0);
    auto [adp, ado] = extractAddress(address);
    ASSERT_EQ(adp, 3);
    ASSERT_EQ(ado, reg::SYNC_MANAGER_0);
}
