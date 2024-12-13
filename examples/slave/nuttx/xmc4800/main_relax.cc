
#include "kickcat/EEPROM/XMC4800EEPROM.h"
#include "kickcat/ESC/XMC4800.h"
#include "kickcat/OS/Time.h"
#include "kickcat/PDO.h"
#include "kickcat/slave/Slave.h"

int main(int, char*[])
{
    using namespace kickcat;

    printf("\n\n\n\n XMC hello relax\n");

    XMC4800 esc;
    XMC4800EEPROM eeprom;
    PDO pdo(&esc);
    slave::Slave slave(&esc, &pdo);

    eeprom.init();
    uint16_t al_status;
    uint16_t al_control;
    esc.read(reg::AL_STATUS, &al_status, sizeof(al_status));
    esc.read(reg::AL_CONTROL, &al_control, sizeof(al_control));
    printf("al_status %x al_control %x  \n", al_status, al_control);


    constexpr uint32_t pdo_size = 32;
    uint8_t buffer_in[pdo_size];
    uint8_t buffer_out[pdo_size];

    // init values
    for (uint32_t i = 0; i < pdo_size; ++i)
    {
        buffer_in[i]  = 3 * i;
        buffer_out[i] = 0xFF;
    }

    pdo.set_process_data_input(buffer_in);
    pdo.set_process_data_output(buffer_out);

    uint8_t esc_config;
    esc.read(reg::ESC_CONFIG, &esc_config, sizeof(esc_config));
    bool is_emulated = esc_config & PDI_EMULATION;
    printf("esc config 0x%x, is emulated %i \n", esc_config, is_emulated);

    uint8_t pdi_config;
    esc.read(reg::PDI_CONFIGURATION, &pdi_config, sizeof(pdi_config));
    printf("pdi config 0x%x \n", pdi_config);

    slave.start();

    while (true)
    {
        eeprom.process();
        slave.routine();
        if (slave.getState() == State::SAFE_OP)
        {
            if (buffer_out[1] != 0xFF)
            {
                slave.setOutputDataValid(true);
            }
        }
    }
    return 0;
}
