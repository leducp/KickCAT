#include "kickcat/EEPROM/XMC4800EEPROM.h"
#include "kickcat/Time.h"
#include "kickcat/ESC/XMC4800.h"

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

    uint16_t in_pdo_size = sizeof(input_PDO);
    uint16_t out_pdo_size = sizeof(output_PDO);

    SyncManagerConfig process_data_out = SYNC_MANAGER_PI_OUT(0, 0x1000, out_pdo_size); // Process data out (master view), address consistent with eeprom conf.
    SyncManagerConfig process_data_in = SYNC_MANAGER_PI_IN(1, 0x1200, in_pdo_size); // Process data in (master view), address consistent with eeprom conf.

    esc.set_mailbox_config({{}});
    esc.set_process_data_input(reinterpret_cast<uint8_t *>(&input_PDO), process_data_in);
    esc.set_process_data_output(reinterpret_cast<uint8_t *>(&output_PDO), process_data_out);

    uint8_t esc_config;
    reportError(esc.read(reg::ESC_CONFIG, &esc_config, sizeof(esc_config)));
    bool is_emulated = esc_config & PDI_EMULATION;
    printf("esc config 0x%x, is emulated %i \n", esc_config, is_emulated);

    uint8_t pdi_config;
    reportError(esc.read(reg::PDI_CONFIGURATION, &pdi_config, sizeof(pdi_config)));
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
    }
    return 0;
}
