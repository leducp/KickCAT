#include <gtest/gtest.h>
#include "kickcat/Slave.h"

using namespace kickcat;

TEST(Slave, prints)
{
    // Test to ensure that nothing explode when using printing helpers (especially when the slave is not initialized)
    // No check about the content (time consumming and unmaintainable).

    Slave slave;

    testing::internal::CaptureStdout();
    slave.printInfo();
    std::string output = testing::internal::GetCapturedStdout();
    ASSERT_LT(300, output.size());

    testing::internal::CaptureStdout();
    slave.printPDOs();
    output = testing::internal::GetCapturedStdout();
    ASSERT_EQ(0, output.size()); // No PDO to print

    testing::internal::CaptureStdout();
    slave.printErrorCounters();
    output = testing::internal::GetCapturedStdout();
    ASSERT_LT(200, output.size());
}


TEST(Slave, parse_SII)
{
    Slave slave;
    slave.sii.buffer =
    {
        // -- Strings
        0x0014000A,     // Section Strings, 40 bytes
        0x6f4c1f01,     // one string of 31 bytes
        0x206d6572,
        0x75737069,
        0x6f64206d,
        0x20726f6c,
        0x20746973,
        0x74656d61,
        0x6f63202c,
        0x0000006e,

        // -- DataTypes
        0x00000014,     // Section DataTypes, 0 bytes

        // -- General
        0x0010001E,     // Section General, 32 bytes
        0xA5A5A5A5,
        0xA5A5A5A5,
        0xA5A5A5A5,
        0xA5A5A5A5,
        0xA5A5A5A5,
        0xA5A5A5A5,
        0xA5A5A5A5,
        0xA5A5A5A5,

        // -- FMMU
        0x00020028,
        0xFFEEDDCC,

        // -- SyncManagers
        0x00040029,     // 8 bytes
        0x00112233,
        0x44556677,

        // -- TxPDO
        0x00080032,     // section TxPDO, 16 bytes
        0x00010000,     // one entry
        0x00000000,     // 'padding'
        0x00000000,
        0x0000FF00,     // 255 bits

        // -- RxPDO
        0x000C0033,     // section RxPDO, 24 bytes
        0x00020000,     // two entries
        0x00000000,     // 'padding'
        0x00000000,
        0x0000FF00,     // 255 bits
        0x00000000,
        0x00008000,     // 128 bits

        // -- DC
        0x0000003C,

        // -- NOOP
        0x00000000,
        0x00000000,

        // -- End
        0xFFFFFFFF
    };

    slave.parseSII();
    ASSERT_EQ(2, slave.sii.strings.size()); // 2 strings since 0 is unused
    ASSERT_EQ(4, slave.sii.fmmus_.size());
    ASSERT_EQ(1, slave.sii.syncManagers_.size());

    // ensure that print infos displays more stuff when SII is loaded
    testing::internal::CaptureStdout();
    slave.printInfo();
    std::string output = testing::internal::GetCapturedStdout();
    ASSERT_LT(430, output.size());

    testing::internal::CaptureStdout();
    slave.printPDOs();
    output = testing::internal::GetCapturedStdout();
    ASSERT_LT(100, output.size()); // No PDO to print
}

TEST(Slave, countOpenPorts)
{

    Slave slave;
    constexpr std::uint8_t mask0 = 1;
    constexpr std::uint8_t mask1 = 1 << 1;
    constexpr std::uint8_t mask2 = 1 << 2;
    constexpr std::uint8_t mask3 = 1 << 3;

    // Testing all combinations of open/closed ports
    // Enumerates all 4-bits combinations of 1's and 0's, and send n-th bit to n-th port by masking and shifting result to 0th postion
    for (uint8_t n = 0; n < 16; ++n)
    {   
        slave.dl_status.PL_port0 = (n & mask0);
        slave.dl_status.PL_port1 = (n & mask1) >> 1;
        slave.dl_status.PL_port2 = (n & mask2) >> 2;
        slave.dl_status.PL_port3 = (n & mask3) >> 3;

        ASSERT_EQ((n & mask0) + ((n & mask1) >> 1) + ((n & mask2) >> 2) + ((n & mask3) >> 3), slave.countOpenPorts());
    }
}