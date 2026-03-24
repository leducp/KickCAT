#include <gtest/gtest.h>
#include "kickcat/Slave.h"

using namespace kickcat;


TEST(Slave, parse_SII)
{
    Slave slave;
    std::vector<uint32_t> eeprom =
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

    slave.parseSII(reinterpret_cast<uint8_t const*>(eeprom.data()), eeprom.size() * sizeof(uint32_t));
    ASSERT_EQ(2, slave.sii.strings.size()); // 2 strings since 0 is unused
    ASSERT_EQ(4, slave.sii.fmmus.size());
    ASSERT_EQ(1, slave.sii.syncManagers.size());
    ASSERT_EQ(1, slave.sii.TxPDO.size());
    ASSERT_EQ(1, slave.sii.TxPDO[0].entries.size());
    ASSERT_EQ(1, slave.sii.RxPDO.size());
    ASSERT_EQ(2, slave.sii.RxPDO[0].entries.size());
}

TEST(Slave, countOpenPorts)
{
    Slave slave;
    std::memset(&slave.dl_status, 0, sizeof(DLStatus));

    ASSERT_EQ(0, slave.countOpenPorts());

    slave.dl_status.COM_port0 = 1;
    ASSERT_EQ(1, slave.countOpenPorts());

    slave.dl_status.COM_port1 = 1;
    ASSERT_EQ(2, slave.countOpenPorts());

    slave.dl_status.COM_port2 = 1;
    slave.dl_status.COM_port3 = 1;
    ASSERT_EQ(4, slave.countOpenPorts());
}

TEST(Slave, error_counters)
{
    ErrorCounters counters;
    std::memset(&counters, 0, sizeof(ErrorCounters));

    Slave slave;
    slave.error_counters = counters;
    ASSERT_EQ(0, slave.computeErrorCounters());
    ASSERT_EQ(0, slave.computeRelativeErrorCounters());

    counters.rx[0].invalid_frame = 3;
    counters.rx[1].physical_layer = 13;
    counters.lost_link[3] = 4;
    ASSERT_NE(0, std::memcmp(&counters, &slave.errorCounters(), sizeof(ErrorCounters)));
    slave.error_counters = counters;
    ASSERT_EQ(0, std::memcmp(&counters, &slave.errorCounters(), sizeof(ErrorCounters)));
    ASSERT_EQ(20, slave.computeErrorCounters());
    ASSERT_EQ(20, slave.computeRelativeErrorCounters());
    ASSERT_EQ(0, slave.computeRelativeErrorCounters());

    ASSERT_TRUE (slave.checkAbsoluteErrorCounters(19));
    ASSERT_FALSE(slave.checkAbsoluteErrorCounters(21));

}
