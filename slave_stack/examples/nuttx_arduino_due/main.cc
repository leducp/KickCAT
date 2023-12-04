
#include "nuttx/SPI.h"

#include "kickcat/Slave.h"

using namespace kickcat;


int main(int argc, char *argv[])
{
    SPI spi_driver{};
    Lan9252 esc(spi_driver);

    Slave slave(esc);

    constexpr uint32_t pdo_size = 32;

    uint8_t buffer_in[pdo_size];
    uint8_t buffer_out[pdo_size];

    uint8_t buffer_safeop_out[pdo_size];

    // init values
    for (uint32_t i=0; i < pdo_size; ++i)
    {
        buffer_in[i] = i;
        buffer_out[i] = 0xFF;
        buffer_safeop_out[i] = 0;
    }

       //TODO macros for basic cases?
    SyncManagerConfig process_data_out(0, 0x1000, pdo_size, 0x64); // Process data out (master view)
    SyncManagerConfig process_data_in(1, 0x1200, pdo_size, 0x20); // Process data in (master view)

    slave.set_mailbox_config({{}});
    slave.set_process_data_input(buffer_in, process_data_in);
    slave.set_process_data_output(buffer_out, process_data_out);
    slave.set_process_data_output_safeop_check(buffer_safeop_out);

    slave.init();


    while(true)
    {
        slave.routine();
        for (uint8_t i = 0; i < pdo_size; ++i)
        {
            printf("%x", buffer_safeop_out[i]);
        }
        printf("\n");

        if (slave.al_status() & ESM_SAFE_OP)
        {
            if (buffer_safeop_out[1] != 0)
            {
                slave.set_valid_output_data_received(true);
            }
        }
        usleep(1000);
    }

    return 0;
}
