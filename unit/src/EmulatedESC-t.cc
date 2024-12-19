#include <gtest/gtest.h>

#include "kickcat/ESC/EmulatedESC.h"

using namespace kickcat;

TEST(EmulatedESC, pdi_read_write_registers)
{
    EmulatedESC esc;

    uint64_t const payload = 0xCAFE0000DECA0000;

    // Test that the PDI can read/write any address below 0x1000
    for (uint16_t address = 0; address < 0x1000; address += sizeof(uint64_t))
    {
        uint64_t read_test;

        ASSERT_EQ(esc.read(address, &read_test, sizeof(uint64_t)), sizeof(uint64_t));
        ASSERT_NE(read_test, payload);

        ASSERT_EQ(esc.write(address, &payload, sizeof(uint64_t)), sizeof(uint64_t));

        ASSERT_EQ(esc.read(address, &read_test, sizeof(uint64_t)), sizeof(uint64_t));
        ASSERT_EQ(read_test, payload);
    }
}


TEST(EmulatedESC, pdi_read_write_ram_no_sm)
{
    EmulatedESC esc;

    uint64_t const payload = 0xCAFE0000DECA0000;

    // Test that the PDI cannot read/write any address after 0x1000 if no SM initialized
    for (uint16_t address = 0x1000; address < 0xF000; address += sizeof(uint64_t))
    {
        uint64_t read_test = 0;

        ASSERT_EQ(esc.read(address, &read_test, sizeof(uint64_t)), -1);
        ASSERT_NE(read_test, payload);

        ASSERT_EQ(esc.write(address, &payload, sizeof(uint64_t)), -1);

        ASSERT_EQ(esc.read(address, &read_test, sizeof(uint64_t)), -1);
        ASSERT_NE(read_test, payload);
    }
}

TEST(EmulatedESC, ecat_aprd_apwr__egisters)
{
    EmulatedESC esc;

    uint64_t payload = 0xCAFE0000DECA0000;
    DatagramHeader header{Command::APRD, 0, 0, sizeof(uint64_t), 0, 0, 0, 0};

    // Test that the ECAT APxx can read/write any address below 0x1000
    for (uint16_t address = 0; address < 0x1000; address += sizeof(uint64_t))
    {
        uint64_t read_test;
        uint16_t wkc = 0;

        header.command = Command::APRD;
        header.address = createAddress(0, address);
        esc.processDatagram(&header, &read_test, &wkc);
        ASSERT_EQ(wkc, 1);
        ASSERT_EQ(header.address, createAddress(1, address)); // +1 on address position for each ESC processing
        ASSERT_NE(read_test, payload);

        header.command = Command::APWR;
        header.address = createAddress(0, address);
        esc.processDatagram(&header, &payload, &wkc);
        ASSERT_EQ(wkc, 2);
        ASSERT_EQ(header.address, createAddress(1, address)); // +1 on address position for each ESC processing

        header.command = Command::APRD;
        header.address = createAddress(0, address);
        esc.processDatagram(&header, &read_test, &wkc);
        ASSERT_EQ(wkc, 3);
        ASSERT_EQ(header.address, createAddress(1, address)); // +1 on address position for each ESC processing
        ASSERT_EQ(read_test, payload);
    }
}

TEST(EmulatedESC, ecat_apxx_fpxx_not_addressed)
{
    EmulatedESC esc;
    DatagramHeader header{Command::APRD, 0, 0, sizeof(uint64_t), 0, 0, 0, 0};

    // Test that the ECAT APxx and FPxx does nothing when the esc is not addressed
    uint16_t const address = reg::SYNC_MANAGER;
    uint64_t read_test;
    uint16_t wkc = 0;
    header.address = createAddress(1, address);

    header.command = Command::APRD;
    esc.processDatagram(&header, &read_test, &wkc);
    ASSERT_EQ(wkc, 0);
    ASSERT_EQ(header.address, createAddress(2, address));

    header.command = Command::APWR;
    esc.processDatagram(&header, &read_test, &wkc);
    ASSERT_EQ(wkc, 0);
    ASSERT_EQ(header.address, createAddress(3, address));

    header.command = Command::APRW;
    esc.processDatagram(&header, &read_test, &wkc);
    ASSERT_EQ(wkc, 0);
    ASSERT_EQ(header.address, createAddress(4, address));

    header.command = Command::FPRD;
    esc.processDatagram(&header, &read_test, &wkc);
    ASSERT_EQ(wkc, 0);
    ASSERT_EQ(header.address, createAddress(4, address));

    header.command = Command::FPWR;
    esc.processDatagram(&header, &read_test, &wkc);
    ASSERT_EQ(wkc, 0);
    ASSERT_EQ(header.address, createAddress(4, address));

    header.command = Command::FPRW;
    esc.processDatagram(&header, &read_test, &wkc);
    ASSERT_EQ(wkc, 0);
    ASSERT_EQ(header.address, createAddress(4, address));
}
