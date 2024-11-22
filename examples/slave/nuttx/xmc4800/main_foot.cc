#include "kickcat/EEPROM/XMC4800EEPROM.h"
#include "kickcat/OS/Time.h"
#include "kickcat/ESC/XMC4800.h"
#include "kickcat/CoE/OD.h"
#include "kickcat/Mailbox.h"
#include "kickcat/CoE/mailbox/response.h"

namespace foot
{
    struct imu
    {
        int16_t accelerometerX; // raw data
        int16_t accelerometerY;
        int16_t accelerometerZ;

        int16_t gyroscopeX; // raw data.
        int16_t gyroscopeY;
        int16_t gyroscopeZ;

        int16_t temperature; // Celsius degrees
    }__attribute__((packed));

    struct Input
    {
        uint16_t watchdog_counter;

        imu footIMU;

        uint16_t force_sensor0;
        uint16_t force_sensor1;
        uint16_t force_sensor2;
        uint16_t force_sensor3;
        uint16_t force_sensor_Vref;

        uint16_t boardStatus;
    } __attribute__((packed));

    struct Output
    {
        uint16_t watchdog_counter;
    } __attribute__((packed));
}

int main(int, char *[])
{
    using namespace kickcat;

    printf("\n\n\n\n XMC hello foot\n");

    XMC4800 esc;
    XMC4800EEPROM eeprom;

    esc.init();
    eeprom.init();
    uint16_t al_status;
    uint16_t al_control;
    esc.read(reg::AL_STATUS, &al_status, sizeof(al_status));
    esc.read(reg::AL_CONTROL, &al_control, sizeof(al_control));
    printf("al_status %x al_control %x  \n", al_status, al_control);

    foot::Input input_PDO;
    foot::Output output_PDO;

    mailbox::response::Mailbox mbx(&esc, 1024);
    auto dictionary = CoE::createOD();
    mbx.enableCoE(std::move(dictionary));

    esc.set_mailbox(&mbx);
    esc.set_process_data_input(reinterpret_cast<uint8_t *>(&input_PDO));
    esc.set_process_data_output(reinterpret_cast<uint8_t *>(&output_PDO));

    uint8_t esc_config;
    esc.read(reg::ESC_CONFIG, &esc_config, sizeof(esc_config));
    bool is_emulated = esc_config & PDI_EMULATION;
    printf("esc config 0x%x, is emulated %i \n", esc_config, is_emulated);

    uint8_t pdi_config;
    esc.read(reg::PDI_CONFIGURATION, &pdi_config, sizeof(pdi_config));
    printf("pdi config 0x%x \n", pdi_config);

    while(true)
    {
        eeprom.process();
        esc.routine();
        if (esc.al_status() & State::SAFE_OP)
        {
            if (output_PDO.watchdog_counter != 0x00)
            {
               esc.set_valid_output_data_received(true);
            }
        }

        mbx.receive();
        mbx.process();
        mbx.send();
    }
    return 0;
}
