#include <gtest/gtest.h>
#include <unordered_map>
#include <regex>

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
    ASSERT_LT(280, output.size());

    testing::internal::CaptureStdout();
    printPDOs(slave);
    output = testing::internal::GetCapturedStdout();
    ASSERT_EQ(0, output.size()); // No PDO to print

    testing::internal::CaptureStdout();
    printf("%s", toString(slave.error_counters).c_str());
    output = testing::internal::GetCapturedStdout();
    ASSERT_LT(200, output.size());

    testing::internal::CaptureStdout();
    printf("%s", toString(slave.dl_status).c_str());
    output = testing::internal::GetCapturedStdout();
    ASSERT_LT(200, output.size());

    testing::internal::CaptureStdout();
    print(topology);
    output = testing::internal::GetCapturedStdout();
    ASSERT_LT(30, output.size());

    testing::internal::CaptureStdout();
    printESC(slave);
    output = testing::internal::GetCapturedStdout();
    ASSERT_LT(850, output.size());
}

TEST(Prints, slave_initialized_prints)
{
    Slave slave;
    slave.sii.eeprom =
    {
        //
        0x00000000,
        0x00000000,
        0x00000000,
        0x00000000,

        // identity
        0x00000000,
        0x00000000,
        0x00000000,
        0x00000000,

        // hardware delays
        0x00000000,
        0x00000000,

        // bootstrap mailbox
        0x00000000,
        0x00000000,

        // standard mailbox
        0x00000000,
        0x00000000,

        // reserved
        0x00000000,
        0x00000000,
        0x00000000,
        0x00000000,
        0x00000000,
        0x00000000,
        0x00000000,
        0x00000000,
        0x00000000,
        0x00000000,
        0x00000000,
        0x00000000,
        0x00000000,
        0x00000000,
        0x00000000,
        0x00000000,
        0x00000000,
        0x00000000,

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
    printf("%s", toString(slave.error_counters).c_str());
    output = testing::internal::GetCapturedStdout();
    ASSERT_LT(200, output.size());

    testing::internal::CaptureStdout();
    printf("%s", toString(slave.dl_status).c_str());
    output = testing::internal::GetCapturedStdout();
    ASSERT_LT(200, output.size());

    testing::internal::CaptureStdout();
    printf("%s", toString(*slave.sii.general).c_str());
    output = testing::internal::GetCapturedStdout();
    ASSERT_LT(200, output.size());
}

TEST(Prints, esc_port_desc)
{
    ASSERT_STREQ(portToString(0), "Not implemented");
    ASSERT_STREQ(portToString(1), "Not configured (SII EEPROM)");
    ASSERT_STREQ(portToString(2), "EBUS");
    ASSERT_STREQ(portToString(3), "MII");
}

TEST(Prints, esc_type)
{
    ASSERT_STREQ(typeToString(1),    "First terminals");
    ASSERT_STREQ(typeToString(2),    "ESC10, ESC20");
    ASSERT_STREQ(typeToString(3),    "First EK1100");
    ASSERT_STREQ(typeToString(4),    "IP Core");
    ASSERT_STREQ(typeToString(5),    "Internal FPGA");
    ASSERT_STREQ(typeToString(17),   "ET1100");
    ASSERT_STREQ(typeToString(18),   "ET1200");
    ASSERT_STREQ(typeToString(145),  "TMS320F2838x");
    ASSERT_STREQ(typeToString(152),  "XMC4800");
    ASSERT_STREQ(typeToString(192),  "LAN9252");
    ASSERT_STREQ(typeToString(0xff), "Unknown");
}

TEST(Prints, fmmu_type)
{
    ASSERT_STREQ(fmmuTypeToString(0), "Unused");
    ASSERT_STREQ(fmmuTypeToString(1), "Outputs (Master to Slave)");
    ASSERT_STREQ(fmmuTypeToString(2), "Inputs  (Slave to Master)");
    ASSERT_STREQ(fmmuTypeToString(3), "SyncM Status (Read Mailbox)");
}

TEST(Prints, esc_features_to_string)
{
    std::string features = featuresToString(0xfff);

    {
        std::regex regex("FMMU.*Byte-oriented", std::regex_constants::ECMAScript);
        ASSERT_TRUE(std::regex_search(features, regex));
    }

    {
        std::regex regex("Unused register access.*not supported", std::regex_constants::ECMAScript);
        ASSERT_TRUE(std::regex_search(features, regex));
    }

    {
        std::regex regex("Distributed clocks.*available", std::regex_constants::ECMAScript);
        ASSERT_TRUE(std::regex_search(features, regex));
    }

    {
        std::regex regex("Distributed clocks \\(width\\).*64 bits", std::regex_constants::ECMAScript);
        ASSERT_TRUE(std::regex_search(features, regex));
    }

    {
        std::regex regex("Low jitter EBUS.*available, jitter minimized", std::regex_constants::ECMAScript);
        ASSERT_TRUE(std::regex_search(features, regex));
    }

    {
        std::regex regex("Enhanced Link Detection EBUS.*available", std::regex_constants::ECMAScript);
        ASSERT_TRUE(std::regex_search(features, regex));
    }

    {
        std::regex regex("Enhanced Link Detection MII.*available", std::regex_constants::ECMAScript);
        ASSERT_TRUE(std::regex_search(features, regex));
    }

    {
        std::regex regex("Separate handling of FCS errors.*available", std::regex_constants::ECMAScript);
        ASSERT_TRUE(std::regex_search(features, regex));
    }

    {
        std::regex regex("Enhanced DC SYNC Activation.*available", std::regex_constants::ECMAScript);
        ASSERT_TRUE(std::regex_search(features, regex));
    }

    {
        std::regex regex("EtherCAT LRW support.*not available", std::regex_constants::ECMAScript);
        ASSERT_TRUE(std::regex_search(features, regex));
    }

    {
        std::regex regex("EtherCAT read/write support.*not available", std::regex_constants::ECMAScript);
        ASSERT_TRUE(std::regex_search(features, regex));
    }

    {
        std::regex regex("Fixed FMMU/SM configuration.*fixed", std::regex_constants::ECMAScript);
        ASSERT_TRUE(std::regex_search(features, regex));
    }

    features = featuresToString(0);

    {
        std::regex regex("FMMU.*Bit-oriented", std::regex_constants::ECMAScript);
        ASSERT_TRUE(std::regex_search(features, regex));
    }

    {
        std::regex regex("Unused register access.*allowed", std::regex_constants::ECMAScript);
        ASSERT_TRUE(std::regex_search(features, regex));
    }

    {
        std::regex regex("Distributed clocks.*not available", std::regex_constants::ECMAScript);
        ASSERT_TRUE(std::regex_search(features, regex));
    }

    {
        std::regex regex("Distributed clocks \\(width\\).*32 bits", std::regex_constants::ECMAScript);
        ASSERT_TRUE(std::regex_search(features, regex));
    }

    {
        std::regex regex("Low jitter EBUS.*not available, standard jitter", std::regex_constants::ECMAScript);
        ASSERT_TRUE(std::regex_search(features, regex));
    }

    {
        std::regex regex("Enhanced Link Detection EBUS.*available", std::regex_constants::ECMAScript);
        ASSERT_TRUE(std::regex_search(features, regex));
    }

    {
        std::regex regex("Enhanced Link Detection MII.*not available", std::regex_constants::ECMAScript);
        ASSERT_TRUE(std::regex_search(features, regex));
    }

    {
        std::regex regex("Separate handling of FCS errors.*not available", std::regex_constants::ECMAScript);
        ASSERT_TRUE(std::regex_search(features, regex));
    }

    {
        std::regex regex("Enhanced DC SYNC Activation.*not available", std::regex_constants::ECMAScript);
        ASSERT_TRUE(std::regex_search(features, regex));
    }

    {
        std::regex regex("EtherCAT LRW support.*available", std::regex_constants::ECMAScript);
        ASSERT_TRUE(std::regex_search(features, regex));
    }

    {
        std::regex regex("EtherCAT read/write support.*available", std::regex_constants::ECMAScript);
        ASSERT_TRUE(std::regex_search(features, regex));
    }

    {
        std::regex regex("Fixed FMMU/SM configuration.*variable", std::regex_constants::ECMAScript);
        ASSERT_TRUE(std::regex_search(features, regex));
    }
}

