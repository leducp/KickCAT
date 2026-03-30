#include <gtest/gtest.h>

#include <cstring>

#include "kickcat/EEPROM/EEPROM_factory.h"

using namespace kickcat;

TEST(EEPROM_factory, createMinimalEEPROM)
{
    eeprom::InfoEntry eeprom;
    std::memset(&eeprom, 0xFF, sizeof(eeprom));

    createMinimalEEPROM(eeprom);

    ASSERT_EQ(0x0280, eeprom.pdi_control);
    ASSERT_EQ(0x6E00, eeprom.pdi_configuration);
    ASSERT_EQ(0xff,   eeprom.sync_impulse_length);
    ASSERT_EQ(0xff,   eeprom.pdi_configuration_2);
    ASSERT_EQ(0,      eeprom.station_alias);
    ASSERT_EQ(0,      eeprom.reserved1);
    ASSERT_EQ(0,      eeprom.reserved2);
    ASSERT_EQ(0xb6,   eeprom.crc);

    ASSERT_EQ(0u, eeprom.vendor_id);
    ASSERT_EQ(0u, eeprom.product_code);
    ASSERT_EQ(0u, eeprom.revision_number);
    ASSERT_EQ(0u, eeprom.serial_number);

    ASSERT_EQ(0,  eeprom.execution_delay);
    ASSERT_EQ(0,  eeprom.port0_delay);
    ASSERT_EQ(0,  eeprom.port1_delay);
    ASSERT_EQ(0,  eeprom.reserved3);

    ASSERT_EQ(0, eeprom.bootstrap_recv_mbx_offset);
    ASSERT_EQ(0, eeprom.bootstrap_recv_mbx_size);
    ASSERT_EQ(0, eeprom.bootstrap_send_mbx_offset);
    ASSERT_EQ(0, eeprom.bootstrap_send_mbx_size);

    ASSERT_EQ(0, eeprom.standard_recv_mbx_offset);
    ASSERT_EQ(0, eeprom.standard_recv_mbx_size);
    ASSERT_EQ(0, eeprom.standard_send_mbx_offset);
    ASSERT_EQ(0, eeprom.standard_send_mbx_size);

    ASSERT_EQ(0, eeprom.mailbox_protocol);
    for (size_t i = 0; i < 32; ++i)
    {
        ASSERT_EQ(0, eeprom.reserved4[i]);
    }

    ASSERT_EQ(0x1f, eeprom.size);
    ASSERT_EQ(0,    eeprom.version);
}
