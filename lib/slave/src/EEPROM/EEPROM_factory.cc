#include "kickcat/EEPROM/EEPROM_factory.h"

#include <cstring>

namespace kickcat
{
    void createMinimalEEPROM(eeprom::InfoEntry& eeprom)
    {
        memset(&eeprom, 0x00, sizeof(eeprom::InfoEntry));
        eeprom.pdi_control = 0x0280;
        eeprom.pdi_configuration = 0x6E00;
        eeprom.sync_impulse_length = 0xff;
        eeprom.pdi_configuration_2 = 0xff;
        eeprom.station_alias = 0;
        eeprom.crc = 0xb6;

        eeprom.vendor_id = 0x00;
        eeprom.product_code = 0x0;
        eeprom.revision_number = 0x0;
        eeprom.serial_number = 0;
        eeprom.execution_delay = 0;
        eeprom.port0_delay = 0;
        eeprom.port1_delay = 0;

        eeprom.bootstrap_receive_mailbox_offset = 0x0000;
        eeprom.bootstrap_receive_mailbox_size = 0;
        eeprom.bootstrap_send_mailbox_offset = 0x0000;
        eeprom.bootstrap_send_mailbox_size = 0;

        eeprom.standard_receive_mailbox_offset = 0x0000;
        eeprom.standard_receive_mailbox_size = 0;
        eeprom.standard_send_mailbox_offset = 0x0000;
        eeprom.standard_send_mailbox_size = 0;

        eeprom.mailbox_protocol = 0x0000;

        eeprom.size = 0x1f;
        eeprom.version = 0;
    }
}
