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
        if (address == reg::DC_SYSTEM_TIME)
        {
            continue;   // live register: ECAT reads return the local copy of the system time
        }

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
        if (address == reg::DC_SYSTEM_TIME)
        {
            continue;   // live register: ECAT reads return the local copy of the system time
        }

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

    // Test that the ECAT Bxx can read/write any address below 0x1000.
    // ETG.1000.4: broadcast reads OR the memory into the frame and every
    // broadcast command increments ADP.
    for (uint16_t address = 0; address < 0x1000; address += sizeof(uint64_t))
    {
        if (address == reg::DC_SYSTEM_TIME)
        {
            continue;   // live register: ECAT reads return the local copy of the system time
        }

        uint64_t read_test = 0;
        uint16_t wkc = 0;

        header.command = Command::BRD;
        header.address = createAddress(0, address);
        esc.processDatagram(&header, &read_test, &wkc);
        ASSERT_EQ(wkc, 1);
        ASSERT_EQ(header.address, createAddress(1, address)); // +1 on address position for each ESC processing
        ASSERT_NE(read_test, payload);

        header.command = Command::BWR;
        header.address = createAddress(0, address);
        esc.processDatagram(&header, &payload, &wkc);
        ASSERT_EQ(wkc, 2);
        ASSERT_EQ(header.address, createAddress(1, address));

        read_test = 0xDEAD0000BEEF0000;
        header.command = Command::BRW;
        header.address = createAddress(0, address);
        esc.processDatagram(&header, &read_test, &wkc);
        ASSERT_EQ(wkc, 5);
        ASSERT_EQ(header.address, createAddress(1, address));
        ASSERT_EQ(read_test, 0xDEAD0000BEEF0000 | payload);   // frame = request data OR memory

        read_test = 0;
        header.command = Command::BRD;
        header.address = createAddress(0, address);
        esc.processDatagram(&header, &read_test, &wkc);
        ASSERT_EQ(wkc, 6);
        ASSERT_EQ(header.address, createAddress(1, address));
        ASSERT_EQ(read_test, 0xDEAD0000BEEF0000);             // BRW wrote the data as received
    }
}

TEST(EmulatedESC, ecat_brd_or_merges_across_slaves)
{
    // Two slaves answering the same BRD: the master must see the OR of both
    // memories (a faulted slave's AL_STATUS bits cannot be masked by a healthy
    // slave answering later), and each slave increments ADP.
    EmulatedESC esc_faulted;
    EmulatedESC esc_healthy;

    uint16_t al_status = State::SAFE_OP | AL_STATUS_ERR_IND;
    esc_faulted.write(reg::AL_STATUS, &al_status, sizeof(al_status));
    al_status = State::OPERATIONAL;
    esc_healthy.write(reg::AL_STATUS, &al_status, sizeof(al_status));

    // Keep the device-emulation AL_STATUS mirror from overwriting the values above.
    uint16_t al_control = State::SAFE_OP | AL_STATUS_ERR_IND;
    esc_faulted.write(reg::AL_CONTROL, &al_control, sizeof(al_control));
    al_control = State::OPERATIONAL;
    esc_healthy.write(reg::AL_CONTROL, &al_control, sizeof(al_control));

    DatagramHeader header{Command::BRD, 0, createAddress(0, reg::AL_STATUS), sizeof(uint16_t), 0, 0, 0, 0};
    uint16_t read_test = 0;
    uint16_t wkc = 0;
    esc_faulted.processDatagram(&header, &read_test, &wkc);
    esc_healthy.processDatagram(&header, &read_test, &wkc);

    ASSERT_EQ(wkc, 2);
    ASSERT_EQ(header.address, createAddress(2, reg::AL_STATUS));
    ASSERT_EQ(read_test, State::SAFE_OP | State::OPERATIONAL | AL_STATUS_ERR_IND);
    ASSERT_NE(read_test & AL_STATUS_ERR_IND, 0);
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

    // Alias addressing is ignored until the master sets DL control 0x100[24].
    header.command = Command::FPRD;
    header.address = createAddress(alias, 0x0040);
    header.len = sizeof(uint64_t);
    uint64_t read_test = 0;
    wkc = 0;
    esc.processDatagram(&header, &read_test, &wkc);
    ASSERT_EQ(wkc, 0);

    // Once enabled, FPRD addressed by the station alias is answered like the station address.
    uint8_t alias_enable = 0x01;
    esc.write(reg::ESC_DL_ALIAS, &alias_enable, sizeof(alias_enable));
    header.address = createAddress(alias, 0x0040);
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

    // FRMW addressed to the slave reads its DC system time (the reference clock)
    // and increments the working counter - this is what static drift compensation
    // relies on. The register is live: the read returns the local copy of the
    // system time (local clock + system time offset).
    int64_t const offset = 0x1000000000ll;
    esc.write(reg::DC_SYSTEM_TIME_OFFSET, &offset, sizeof(offset));

    uint64_t before = static_cast<uint64_t>(esc.localSystemTime().count());

    header.command = Command::FRMW;
    header.address = createAddress(station, reg::DC_SYSTEM_TIME);
    header.len = sizeof(uint64_t);
    uint64_t read_back = 0;
    wkc = 0;
    esc.processDatagram(&header, &read_back, &wkc);
    ASSERT_EQ(wkc, 1);

    uint64_t after = static_cast<uint64_t>(esc.localSystemTime().count());
    ASSERT_LE(before, read_back);   // mocked clock is strictly increasing
    ASSERT_LE(read_back, after);
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

namespace
{
    // Drive the exact Bus::writeEeprom sequence: FPWR data word, then FPWR the
    // WRITE|WR_EN request until the control register no longer reports ERROR_CMD.
    bool masterWriteEepromWord(EmulatedESC& esc, uint16_t station, uint16_t address, uint16_t word)
    {
        DatagramHeader header{Command::FPWR, 0, createAddress(station, reg::EEPROM_DATA), sizeof(word), 0, 0, 0, 0};
        uint16_t wkc = 0;
        esc.processDatagram(&header, &word, &wkc);
        if (wkc != 1)
        {
            return false;
        }

        for (int retry = 0; retry < 20; ++retry)
        {
            eeprom::Request req{static_cast<uint16_t>(eeprom::Control::WRITE | eeprom::Control::WR_EN), address, 0};
            header.command = Command::FPWR;
            header.address = createAddress(station, reg::EEPROM_CONTROL);
            header.len     = sizeof(req);
            wkc = 0;
            esc.processDatagram(&header, &req, &wkc);
            if (wkc != 1)
            {
                return false;
            }

            uint16_t control = 0;
            header.command = Command::FPRD;
            header.address = createAddress(station, reg::EEPROM_CONTROL);
            header.len     = sizeof(control);
            wkc = 0;
            esc.processDatagram(&header, &control, &wkc);
            if (wkc != 1)
            {
                return false;
            }
            if (not (control & eeprom::Control::ERROR_CMD))
            {
                return true;
            }
        }
        return false;
    }

    // READ command then fetch the data register (lower 32 bits = 2 words).
    uint32_t masterReadEepromWords(EmulatedESC& esc, uint16_t station, uint16_t address)
    {
        eeprom::Request req{eeprom::Control::READ, address, 0};
        DatagramHeader header{Command::FPWR, 0, createAddress(station, reg::EEPROM_CONTROL), sizeof(req), 0, 0, 0, 0};
        uint16_t wkc = 0;
        esc.processDatagram(&header, &req, &wkc);
        EXPECT_EQ(wkc, 1);

        uint32_t data = 0;
        header.command = Command::FPRD;
        header.address = createAddress(station, reg::EEPROM_DATA);
        header.len     = sizeof(data);
        wkc = 0;
        esc.processDatagram(&header, &data, &wkc);
        EXPECT_EQ(wkc, 1);
        return data;
    }
}

TEST(EmulatedESC, ecat_eeprom_master_write_sequence)
{
    EmulatedESC esc;
    std::vector<uint16_t> image(8);
    for (size_t i = 0; i < image.size(); ++i)
    {
        image[i] = static_cast<uint16_t>(0x1100 + i);
    }
    esc.loadEeprom(image);

    uint16_t const station = 0x1234;
    uint16_t wkc = 0;
    DatagramHeader header{Command::APWR, 0, createAddress(0, reg::STATION_ADDR), sizeof(station), 0, 0, 0, 0};
    uint16_t address_payload = station;
    esc.processDatagram(&header, &address_payload, &wkc);
    ASSERT_EQ(wkc, 1);

    // Conformant master write (WRITE with WR_EN) must land in the EEPROM.
    ASSERT_TRUE(masterWriteEepromWord(esc, station, 5, 0xCAFE));
    ASSERT_TRUE(masterWriteEepromWord(esc, station, 6, 0xDECA));
    ASSERT_EQ(masterReadEepromWords(esc, station, 5), 0xDECACAFEu);

    // Writing past the loaded image must grow it (word-addressed) without
    // corrupting the heap; the word at the new end of the EEPROM is reachable.
    ASSERT_TRUE(masterWriteEepromWord(esc, station, 20, 0xBEEF));
    ASSERT_TRUE(masterWriteEepromWord(esc, station, 21, 0xDEAD));
    ASSERT_EQ(masterReadEepromWords(esc, station, 20), 0xDEADBEEFu);

    // The gap created by the resize reads as an unwritten EEPROM, not as zeros.
    ASSERT_EQ(masterReadEepromWords(esc, station, 10), 0xFFFFFFFFu);

    // A WRITE command without WR_EN is refused: error bit 14 raised, data untouched.
    uint16_t word = 0x5555;
    header.command = Command::FPWR;
    header.address = createAddress(station, reg::EEPROM_DATA);
    header.len     = sizeof(word);
    wkc = 0;
    esc.processDatagram(&header, &word, &wkc);

    eeprom::Request req{eeprom::Control::WRITE, 2, 0};
    header.address = createAddress(station, reg::EEPROM_CONTROL);
    header.len     = sizeof(req);
    esc.processDatagram(&header, &req, &wkc);

    uint16_t control = 0;
    header.command = Command::FPRD;
    header.address = createAddress(station, reg::EEPROM_CONTROL);
    header.len     = sizeof(control);
    esc.processDatagram(&header, &control, &wkc);
    ASSERT_NE(control & eeprom::Control::ERROR_WR_EN, 0);
    ASSERT_EQ(masterReadEepromWords(esc, station, 2), 0x11031102u); // image words 2 and 3 untouched
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

static void configureOutputFmmuAndEnterSafeOP(EmulatedESC& esc, uint8_t fmmu_type)
{
    uint8_t pre_op = State::PRE_OP;
    esc.write(reg::AL_STATUS, &pre_op, 1);
    uint8_t safe_op = State::SAFE_OP;
    esc.write(reg::AL_CONTROL, &safe_op, 1);

    fmmu::Register fmmu;
    memset(&fmmu, 0, sizeof(fmmu::Register));
    fmmu.type             = fmmu_type;   // 2 = output (master->slave), 1 = input
    fmmu.logical_address  = 0x2000;
    fmmu.length           = 1;
    fmmu.logical_stop_bit = 0x7;
    fmmu.physical_address = 0x1000;
    fmmu.activate         = 1;
    esc.write(reg::FMMU + 0x00, &fmmu, sizeof(fmmu::Register));
}

TEST(EmulatedESC, watchdog)
{
    EmulatedESC esc;
    uint16_t wkc = 0;
    DatagramHeader header{Command::NOP, 0, 0, 0, 0, 0, 0, 0};

    // The process-data watchdog monitors outputs, so an output FMMU must be configured (and
    // the PRE_OP->SAFE_OP transition run) before it is armed.
    configureOutputFmmuAndEnterSafeOP(esc, 2);

    // Divider: (2498 + 2) * 40ns = 100us. Watchdog Time PDO: 100 * 100us = 10ms.
    uint16_t divider = 2498;
    esc.write(reg::WDG_DIVIDER, &divider, 2);
    uint16_t wdg_time = 100;
    esc.write(reg::WDG_TIME_PDO, &wdg_time, 2);

    esc.processDatagram(&header, nullptr, &wkc);  // runs configureFmmus + arms the watchdog

    uint8_t safe_op = State::SAFE_OP;             // settle AL_STATUS so configureFmmus runs only once
    esc.write(reg::AL_STATUS, &safe_op, 1);

    uint16_t status = 0;
    esc.read(reg::WDOG_STATUS, &status, 2);
    EXPECT_EQ(status & 0x01, 1);  // SPEC: bit 0 of 0x0440 is 1 if OK, 0 if expired

    // the mocked clock advances 1ms per call in unit tests; 20 cycles exceeds the 10ms window.
    for (int i = 0; i < 20; ++i)
    {
        esc.processDatagram(&header, nullptr, &wkc);
    }

    esc.read(reg::WDOG_STATUS, &status, 2);
    EXPECT_EQ(status & 0x01, 0);

    uint8_t counter = 0;
    esc.read(reg::WDOG_COUNTER_PDO, &counter, 1);
    EXPECT_GT(counter, 0);
}

TEST(EmulatedESC, watchdog_device_emulation_drops_to_safe_op_via_al_status)
{
    EmulatedESC esc;
    // Word 0 high byte is the ESC configuration: bit 0 enables device emulation.
    esc.loadEeprom(std::vector<uint16_t>{0x0104, 0, 0, 0, 0});

    uint16_t wkc = 0;
    DatagramHeader header{Command::NOP, 0, 0, 0, 0, 0, 0, 0};

    configureOutputFmmuAndEnterSafeOP(esc, 2);

    uint16_t divider = 2498;
    esc.write(reg::WDG_DIVIDER, &divider, 2);
    uint16_t wdg_time = 100;
    esc.write(reg::WDG_TIME_PDO, &wdg_time, 2);

    esc.processDatagram(&header, nullptr, &wkc);  // runs configureFmmus + arms the watchdog

    uint8_t op = State::OPERATIONAL;
    esc.write(reg::AL_CONTROL, &op, 1);
    esc.processDatagram(&header, nullptr, &wkc);  // device emulation mirrors AL_CONTROL

    uint8_t al_status = 0;
    esc.read(reg::AL_STATUS, &al_status, 1);
    ASSERT_EQ(State::OPERATIONAL, al_status);

    // the mocked clock advances 1ms per call in unit tests; 30 cycles exceed the 10ms window.
    for (int i = 0; i < 30; ++i)
    {
        esc.processDatagram(&header, nullptr, &wkc);
    }

    // The fallback is driven through AL_STATUS; AL_CONTROL (0x120) is master-owned
    // and shall never be modified by the ESC.
    esc.read(reg::AL_STATUS, &al_status, 1);
    ASSERT_EQ(State::SAFE_OP | AL_STATUS_ERR_IND, al_status);
    uint16_t al_status_code = 0;
    esc.read(reg::AL_STATUS_CODE, &al_status_code, 2);
    ASSERT_EQ(SYNC_MANAGER_WATCHDOG, al_status_code);
    uint8_t al_control = 0;
    esc.read(reg::AL_CONTROL, &al_control, 1);
    ASSERT_EQ(State::OPERATIONAL, al_control);

    // The error indication holds against the mirror until the master acks it.
    esc.processDatagram(&header, nullptr, &wkc);
    esc.read(reg::AL_STATUS, &al_status, 1);
    ASSERT_EQ(State::SAFE_OP | AL_STATUS_ERR_IND, al_status);

    uint8_t ack = State::SAFE_OP | AL_CONTROL_ERR_ACK;
    esc.write(reg::AL_CONTROL, &ack, 1);
    esc.processDatagram(&header, nullptr, &wkc);
    esc.read(reg::AL_STATUS, &al_status, 1);
    ASSERT_EQ(State::SAFE_OP, al_status);
}

TEST(EmulatedESC, watchdog_inactive_without_outputs)
{
    // An input-only terminal has no output FMMU, so the process-data watchdog never fires
    // (it monitors output delivery). Without this, every digital-input slave bounces to SAFE_OP.
    EmulatedESC esc;
    uint16_t wkc = 0;
    DatagramHeader header{Command::NOP, 0, 0, 0, 0, 0, 0, 0};

    configureOutputFmmuAndEnterSafeOP(esc, 1);  // input FMMU only

    uint16_t divider = 2498;
    esc.write(reg::WDG_DIVIDER, &divider, 2);
    uint16_t wdg_time = 100;
    esc.write(reg::WDG_TIME_PDO, &wdg_time, 2);

    for (int i = 0; i < 30; ++i)
    {
        esc.processDatagram(&header, nullptr, &wkc);
    }

    uint16_t status = 0;
    esc.read(reg::WDOG_STATUS, &status, 2);
    EXPECT_EQ(status & 0x01, 1);  // still healthy: no outputs to monitor

    uint8_t counter = 0;
    esc.read(reg::WDOG_COUNTER_PDO, &counter, 1);
    EXPECT_EQ(counter, 0);
}

