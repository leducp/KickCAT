#include <gtest/gtest.h>

#include "kickcat/ESC/EmulatedESC.h"
#include "kickcat/LoopbackSocket.h"

using namespace kickcat;

TEST(EmulatedESC, init)
{
    EmulatedESC esc;
    ASSERT_EQ(esc.init(), 0);
}

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

        ASSERT_EQ(esc.read(address, &read_test, sizeof(uint64_t)), -EACCES);
        ASSERT_NE(read_test, payload);

        ASSERT_EQ(esc.write(address, &payload, sizeof(uint64_t)), -EACCES);

        ASSERT_EQ(esc.read(address, &read_test, sizeof(uint64_t)), -EACCES);
        ASSERT_NE(read_test, payload);
    }
}

TEST(EmulatedESC, ecat_aprd_apwr_registers)
{
    EmulatedESC esc;

    uint64_t payload = 0xCAFE0000DECA0000;
    DatagramHeader header{Command::APRD, 0, 0, sizeof(uint64_t), 0, 0, 0, 0};

    // Test that the ECAT APxx can read/write any address below 0x1000
    for (uint16_t address = 0; address < 0x1000; address += sizeof(uint64_t))
    {
        uint64_t read_test = 0;
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

        read_test = 0xDEAD0000BEEF0000;
        header.command = Command::APRW;
        header.address = createAddress(0, address);
        esc.processDatagram(&header, &read_test, &wkc);
        ASSERT_EQ(wkc, 5);
        ASSERT_EQ(header.address, createAddress(1, address)); // +1 on address position for each ESC processing
        ASSERT_EQ(read_test, payload);

        header.command = Command::APRD;
        header.address = createAddress(0, address);
        esc.processDatagram(&header, &read_test, &wkc);
        ASSERT_EQ(wkc, 6);
        ASSERT_EQ(header.address, createAddress(1, address)); // +1 on address position for each ESC processing
        ASSERT_EQ(read_test, 0xDEAD0000BEEF0000);
    }
}

TEST(EmulatedESC, ecat_fprd_fpwr_registers)
{
    EmulatedESC esc;

    uint16_t slave_address = 0x1000;
    uint16_t wkc = 0;
    DatagramHeader header{Command::APWR, 0, 0, sizeof(uint16_t), 0, 0, 0, 0};

    header.address = createAddress(0, reg::STATION_ADDR);
    esc.processDatagram(&header, &slave_address, &wkc);
    ASSERT_EQ(wkc, 1);

    // Test that the ECAT FPxx can read/write any address below 0x1000 when addressed
    // Note that we skip the first 0x0020 area to avoid messing with the station address
    uint64_t payload = 0xCAFE0000DECA0000;
    for (uint16_t address = 0x0020; address < 0x1000; address += sizeof(uint64_t))
    {
        header.address = createAddress(slave_address, address);
        header.len = sizeof(uint64_t);
        uint64_t read_test = 0;
        wkc = 0;

        header.command = Command::FPRD;
        esc.processDatagram(&header, &read_test, &wkc);
        ASSERT_EQ(wkc, 1);
        ASSERT_NE(read_test, payload);

        header.command = Command::FPWR;
        esc.processDatagram(&header, &payload, &wkc);
        ASSERT_EQ(wkc, 2);

        read_test = 0xDEAD0000BEEF0000;
        header.command = Command::FPRW;
        esc.processDatagram(&header, &read_test, &wkc);
        ASSERT_EQ(wkc, 5);
        ASSERT_EQ(read_test, payload);

        header.command = Command::FPRD;
        esc.processDatagram(&header, &read_test, &wkc);
        ASSERT_EQ(wkc, 6);
        ASSERT_EQ(read_test, 0xDEAD0000BEEF0000);
    }
}

TEST(EmulatedESC, ecat_apxx_fpxx_not_addressed)
{
    EmulatedESC esc;
    DatagramHeader header{Command::APRD, 0, 0, sizeof(uint64_t), 0, 0, 0, 0};

    // Test that the ECAT APxx and FPxx does nothing when the esc is not addressed
    uint16_t const address = reg::SYNC_MANAGER;
    uint64_t read_test = 0;
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

TEST(EmulatedESC, ecat_bxx_registers)
{
    EmulatedESC esc;

    uint64_t payload = 0xCAFE0000DECA0000;
    DatagramHeader header{Command::BRD, 0, 0, sizeof(uint64_t), 0, 0, 0, 0};

    // Test that the ECAT Bxx can read/write any address below 0x1000
    for (uint16_t address = 0; address < 0x1000; address += sizeof(uint64_t))
    {
        header.address = createAddress(0, address);

        uint64_t read_test = 0;
        uint16_t wkc = 0;

        header.command = Command::BRD;
        esc.processDatagram(&header, &read_test, &wkc);
        ASSERT_EQ(wkc, 1);
        ASSERT_NE(read_test, payload);

        header.command = Command::BWR;
        esc.processDatagram(&header, &payload, &wkc);
        ASSERT_EQ(wkc, 2);

        read_test = 0xDEAD0000BEEF0000;
        header.command = Command::BRW;
        esc.processDatagram(&header, &read_test, &wkc);
        ASSERT_EQ(wkc, 5);
        ASSERT_EQ(read_test, payload);

        header.command = Command::BRD;
        esc.processDatagram(&header, &read_test, &wkc);
        ASSERT_EQ(wkc, 6);
        ASSERT_EQ(read_test, 0xDEAD0000BEEF0000);
    }
}

TEST(EmulatedESC, ecat_PDOs)
{
    EmulatedESC esc;


    //Note: the PDO conf from PDI is possible here because the emulator do not implement control accesses on a per register basis (yet)
    uint8_t current = State::PRE_OP;
    esc.write(reg::AL_STATUS, &current, 1);

    uint8_t next = State::SAFE_OP;
    esc.write(reg::AL_CONTROL, &next, 1);

    fmmu::Register fmmu;
    memset(&fmmu, 0, sizeof(fmmu::Register));

    fmmu.type  = 2;                // write access
    fmmu.logical_address    = 0x2000;
    fmmu.length             = 10;
    fmmu.logical_start_bit  = 0;   // we map every bits
    fmmu.logical_stop_bit   = 0x7; // we map every bits
    fmmu.physical_address   = 0x3000;
    fmmu.physical_start_bit = 0;
    fmmu.activate           = 1;
    esc.write(reg::FMMU + 0x00, &fmmu, sizeof(fmmu::Register));

    fmmu.type  = 1;                 // read access
    fmmu.logical_address    = 0x200A;
    fmmu.length             = 10;
    fmmu.logical_start_bit  = 0;   // we map every bits
    fmmu.logical_stop_bit   = 0x7; // we map every bits
    fmmu.physical_address   = 0x300A;
    fmmu.physical_start_bit = 0;
    fmmu.activate           = 1;
    esc.write(reg::FMMU + 0x10, &fmmu, sizeof(fmmu::Register));

    // run internal logic
    DatagramHeader header{Command::BRD, 0, 0, sizeof(uint64_t), 0, 0, 0, 0};
    uint64_t read_test;
    uint16_t wkc;
    esc.processDatagram(&header, &read_test, &wkc);

    // slave -> master
    uint64_t payload = 0xCAFE0000DECA0000;
    esc.write(0x300A, &payload, sizeof(uint64_t));

    header.command = Command::LRD;
    header.address = 0x200A;
    wkc = 0;
    esc.processDatagram(&header, &read_test, &wkc);
    ASSERT_EQ(wkc, 1);
    ASSERT_EQ(read_test, payload);

    // master -> slave
    payload = 0xDEAD0000BEEF0000;

    header.command = Command::LWR;
    header.address = 0x2000;
    wkc = 0;
    esc.processDatagram(&header, &payload, &wkc);
    ASSERT_EQ(wkc, 1);

    esc.read(0x3000, &read_test, sizeof(uint64_t));
    ASSERT_EQ(read_test, payload);
}

TEST(EmulatedESC, ecat_fpxx_matches_station_alias)
{
    EmulatedESC esc;
    uint16_t wkc = 0;
    DatagramHeader header{Command::APWR, 0, 0, sizeof(uint16_t), 0, 0, 0, 0};

    uint16_t station = 0x1000;
    header.address = createAddress(0, reg::STATION_ADDR);
    esc.processDatagram(&header, &station, &wkc);

    uint16_t alias = 0xABCD;
    esc.write(reg::STATION_ALIAS, &alias, sizeof(alias));

    // FPRD addressed by the station alias is answered like the station address.
    header.command = Command::FPRD;
    header.address = createAddress(alias, 0x0040);
    header.len = sizeof(uint64_t);
    uint64_t read_test = 0;
    wkc = 0;
    esc.processDatagram(&header, &read_test, &wkc);
    ASSERT_EQ(wkc, 1);

    // A non-matching position is still ignored.
    header.address = createAddress(0x7777, 0x0040);
    wkc = 0;
    esc.processDatagram(&header, &read_test, &wkc);
    ASSERT_EQ(wkc, 0);
}

TEST(EmulatedESC, ecat_fmmu_out_of_bounds_is_skipped)
{
    EmulatedESC esc;

    uint8_t current = State::PRE_OP;
    esc.write(reg::AL_STATUS, &current, 1);
    uint8_t next = State::SAFE_OP;
    esc.write(reg::AL_CONTROL, &next, 1);

    fmmu::Register fmmu;
    memset(&fmmu, 0, sizeof(fmmu::Register));
    fmmu.type  = 1;                 // read access
    fmmu.logical_address  = 0x2000;
    fmmu.length           = 16;
    fmmu.logical_stop_bit = 0x7;
    fmmu.physical_address = 0xFFF8;   // 0xFFF8 + 16 runs past the 0x10000 memory map
    fmmu.activate         = 1;
    esc.write(reg::FMMU + 0x00, &fmmu, sizeof(fmmu::Register));

    // Configuration must not produce an out-of-bounds pointer, and the bad FMMU
    // must not be mapped: a logical read on its address gets no working counter.
    DatagramHeader header{Command::BRD, 0, 0, sizeof(uint64_t), 0, 0, 0, 0};
    uint64_t buf = 0;
    uint16_t wkc = 0;
    esc.processDatagram(&header, &buf, &wkc);

    header.command = Command::LRD;
    header.address = 0x2000;
    wkc = 0;
    esc.processDatagram(&header, &buf, &wkc);
    ASSERT_EQ(wkc, 0);
}

TEST(EmulatedESC, ecat_fmmu_maps_register_bit)
{
    // A single-bit FMMU into register space (physical < 0x1000) is how a master maps
    // an SM mailbox-status bit into the logical image; the generic engine handles it
    // with no dedicated path.
    EmulatedESC esc;

    uint8_t current = State::PRE_OP;
    esc.write(reg::AL_STATUS, &current, 1);
    uint8_t next = State::SAFE_OP;
    esc.write(reg::AL_CONTROL, &next, 1);

    fmmu::Register fmmu;
    memset(&fmmu, 0, sizeof(fmmu::Register));
    fmmu.type  = 1;                 // read access (slave -> master)
    fmmu.logical_address    = 0x2000;
    fmmu.length             = 1;
    fmmu.logical_start_bit  = 2;    // land the bit at logical bit 2
    fmmu.logical_stop_bit   = 2;
    fmmu.physical_address   = reg::SYNC_MANAGER_1 + reg::SM_STATS;
    fmmu.physical_start_bit = 3;    // SM mailbox-full status bit
    fmmu.activate           = 1;
    esc.write(reg::FMMU + 0x00, &fmmu, sizeof(fmmu::Register));

    DatagramHeader header{Command::BRD, 0, 0, sizeof(uint64_t), 0, 0, 0, 0};
    uint64_t buf = 0;
    uint16_t wkc = 0;
    esc.processDatagram(&header, &buf, &wkc);

    // Set the source register bit, then read it through the FMMU.
    uint8_t status = 0;
    esc.read(reg::SYNC_MANAGER_1 + reg::SM_STATS, &status, 1);
    status |= (1 << 3);
    esc.write(reg::SYNC_MANAGER_1 + reg::SM_STATS, &status, 1);

    uint8_t frame = 0x00;
    header.command = Command::LRD;
    header.address = 0x2000;
    header.len     = 1;
    wkc = 0;
    esc.processDatagram(&header, &frame, &wkc);
    ASSERT_EQ(wkc, 1);
    ASSERT_EQ(frame & (1 << 2), (1 << 2));   // register bit 3 -> logical bit 2

    // Clear it and confirm the mapped bit follows.
    status &= ~(1 << 3);
    esc.write(reg::SYNC_MANAGER_1 + reg::SM_STATS, &status, 1);
    frame = 0xFF;
    header.address = 0x2000;
    wkc = 0;
    esc.processDatagram(&header, &frame, &wkc);
    ASSERT_EQ(wkc, 1);
    ASSERT_EQ(frame & (1 << 2), 0);
}

TEST(EmulatedESC, ecat_frmw_reads_reference_clock)
{
    EmulatedESC esc;
    uint16_t wkc = 0;
    DatagramHeader header{Command::APWR, 0, 0, sizeof(uint16_t), 0, 0, 0, 0};

    uint16_t station = 0x1001;
    header.address = createAddress(0, reg::STATION_ADDR);
    esc.processDatagram(&header, &station, &wkc);

    uint64_t systime = 0x1122334455667788ull;
    esc.write(reg::DC_SYSTEM_TIME, &systime, sizeof(systime));

    // FRMW addressed to the slave reads its DC system time (the reference clock)
    // and increments the working counter - this is what static drift compensation
    // relies on.
    header.command = Command::FRMW;
    header.address = createAddress(station, reg::DC_SYSTEM_TIME);
    header.len = sizeof(uint64_t);
    uint64_t read_back = 0;
    wkc = 0;
    esc.processDatagram(&header, &read_back, &wkc);
    ASSERT_EQ(wkc, 1);
    ASSERT_EQ(read_back, systime);
}

TEST(EmulatedESC, ecat_eeprom_reload_reapplies_config)
{
    EmulatedESC esc;
    std::vector<uint16_t> image(128, 0);
    image[4] = 0xBEEF;                 // station alias word in the EEPROM
    esc.loadEeprom(image);

    uint16_t alias = 0;
    esc.read(reg::STATION_ALIAS, &alias, sizeof(alias));
    ASSERT_EQ(alias, 0xBEEF);

    // Corrupt the alias register; a Reload command must restore it from the EEPROM.
    uint16_t junk = 0x1234;
    esc.write(reg::STATION_ALIAS, &junk, sizeof(junk));

    uint16_t reload = eeprom::Control::RELOAD;
    esc.write(reg::EEPROM_CONTROL, &reload, sizeof(reload));

    uint16_t wkc = 0;
    DatagramHeader nop{Command::NOP, 0, 0, 0, 0, 0, 0, 0};
    esc.processDatagram(&nop, nullptr, &wkc);   // runs the internal logic -> RELOAD

    esc.read(reg::STATION_ALIAS, &alias, sizeof(alias));
    ASSERT_EQ(alias, 0xBEEF);
}

TEST(LoopbackSocket, runs_frame_through_slave_and_ticks)
{
    EmulatedESC esc;
    int ticks = 0;
    LoopbackSocket sock({&esc}, [&]() { ticks++; });

    Frame frame;
    uint16_t value = 0;
    frame.addDatagram(0, Command::BRD, createAddress(0, reg::TYPE), &value, sizeof(value));
    int32_t size = frame.finalize();

    uint8_t buffer[ETH_MAX_SIZE];
    ASSERT_EQ(sock.write(frame.data(), size), size);
    ASSERT_EQ(ticks, 1);                              // slave application advanced once

    ASSERT_EQ(sock.read(buffer, sizeof(buffer)), size);  // processed frame returned once
    ASSERT_EQ(sock.read(buffer, sizeof(buffer)), 0);     // nothing pending afterwards

    // The slave answered: the working counter is incremented in the returned datagram.
    Frame response;
    std::memcpy(response.data(), buffer, size);
    auto [header, data, wkc] = response.peekDatagram();
    ASSERT_NE(header, nullptr);
    ASSERT_EQ(*wkc, 1);
}

TEST(EmulatedESC, watchdog)
{
    EmulatedESC esc;
    uint16_t wkc = 0;
    DatagramHeader header{Command::NOP, 0, 0, 0, 0, 0, 0, 0};

    // Configure Watchdog
    // Divider: 100us (2498 + 2) * 40ns = 2500 * 40ns = 100,000ns = 100us
    uint16_t divider = 2498;
    esc.write(reg::WDG_DIVIDER, &divider, 2);

    // Watchdog Time PDO: 100 units of 100us = 10ms
    uint16_t wdg_time = 100;
    esc.write(reg::WDG_TIME_PDO, &wdg_time, 2);

    // Trigger processInternalLogic
    esc.processDatagram(&header, nullptr, &wkc);

    uint16_t status = 0;
    esc.read(reg::WDOG_STATUS, &status, 2);

    // SPEC: Bit 0 of 0x0440 is 1 if OK, 0 if expired.
    EXPECT_EQ(status & 0x01, 1);

    // Advance time beyond 10ms
    // since_epoch() increments by 1ms per call in unit tests.
    for(int i = 0; i < 20; ++i)
    {
        esc.processDatagram(&header, nullptr, &wkc);
    }

    esc.read(reg::WDOG_STATUS, &status, 2);
    EXPECT_EQ(status & 0x01, 0);

    uint8_t counter = 0;
    esc.read(reg::WDOG_COUNTER_PDO, &counter, 1);
    EXPECT_GT(counter, 0);
}

