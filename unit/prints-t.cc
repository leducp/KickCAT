#include <gtest/gtest.h>
#include <unordered_map>

#include "kickcat/Slave.h"
#include "kickcat/Prints.h"

using namespace kickcat;

TEST(Prints, slave_uninitialized_prints)
{
    // Test to ensure that nothing explode when using printing helpers (especially when the slave is not initialized)
    // No check about the content (time consumming and unmaintainable).

    Slave slave;
    std::unordered_map<uint16_t, uint16_t> topology({{0, 0}, {1, 0}, {2, 1}});

    testing::internal::CaptureStdout();
    printInfo(slave);
    std::string output = testing::internal::GetCapturedStdout();
    ASSERT_LT(300, output.size());

    testing::internal::CaptureStdout();
    printPDOs(slave);
    output = testing::internal::GetCapturedStdout();
    ASSERT_EQ(0, output.size()); // No PDO to print

    testing::internal::CaptureStdout();
    printErrorCounters(slave);
    output = testing::internal::GetCapturedStdout();
    ASSERT_LT(200, output.size());

    testing::internal::CaptureStdout();
    printDLStatus(slave);
    output = testing::internal::GetCapturedStdout();
    ASSERT_LT(200, output.size());

    testing::internal::CaptureStdout();
    printGeneralEntry(slave);
    output = testing::internal::GetCapturedStdout();
    ASSERT_LT(30, output.size());

    testing::internal::CaptureStdout();
    printTopology(topology);
    output = testing::internal::GetCapturedStdout();
    ASSERT_LT(30, output.size());

}

TEST(Prints, slave_initialized_prints)
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
    // ensure that print infos displays more stuff when SII is loaded
    testing::internal::CaptureStdout();
    printInfo(slave);
    std::string output = testing::internal::GetCapturedStdout();
    ASSERT_LT(430, output.size());

    testing::internal::CaptureStdout();
    printPDOs(slave);
    output = testing::internal::GetCapturedStdout();
    ASSERT_LT(100, output.size()); // No PDO to print

    testing::internal::CaptureStdout();
    printErrorCounters(slave);
    output = testing::internal::GetCapturedStdout();
    ASSERT_LT(200, output.size());

    testing::internal::CaptureStdout();
    printDLStatus(slave);
    output = testing::internal::GetCapturedStdout();
    ASSERT_LT(200, output.size());

    testing::internal::CaptureStdout();
    printGeneralEntry(slave);
    output = testing::internal::GetCapturedStdout();
    ASSERT_LT(200, output.size());
}
