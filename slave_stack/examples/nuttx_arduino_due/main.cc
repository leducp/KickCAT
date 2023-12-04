
#include "nuttx/SPI.h"

#include "kickcat/Slave.h"

using namespace kickcat;


int main(int argc, char *argv[])
{
    std::shared_ptr<SPI> spi_driver = std::make_shared<SPI>();
    std::shared_ptr<Lan9252> esc = std::make_shared<Lan9252>(spi_driver);
    Slave slave(esc);

    constexpr uint32_t pdo_size = 32;

    uint8_t buffer_in[pdo_size];
    uint8_t buffer_out[pdo_size];

    // init values
    for (uint32_t i=0; i < pdo_size; ++i)
    {
        buffer_in[i] = i;
        buffer_out[i] = 0xFF;
    }

       //TODO macros for basic cases?
    SyncManagerConfig process_data_out(0, 0x1000, pdo_size, 0x64); // Process data out (master view)
    SyncManagerConfig process_data_in(1, 0x1200, pdo_size, 0x20); // Process data in (master view)

    slave.set_mailbox_config({{}});
    slave.set_process_data_input(buffer_in, process_data_in);
    slave.set_process_data_output(buffer_out, process_data_out);

    slave.init();

    uint8_t esc_config;
    reportError(esc->read(ESC_CONFIGURATION, &esc_config, sizeof(esc_config)));

    bool is_emulated = esc_config & PDI_EMULATION;
    printf("esc config 0x%x, is emulated %i \n", esc_config, is_emulated);

    while(true)
    {
        slave.routine();
        for (uint8_t i = 0; i < pdo_size; ++i)
        {
            printf("%x", buffer_out[i]);
        }
        printf("\n");

        if (slave.al_status() & ESM_SAFE_OP)
        {
            if (buffer_out[1] != 0xFF)
            {
                slave.set_valid_output_data_received(true);
            }
        }
        usleep(1000);
    }

    return 0;
}
