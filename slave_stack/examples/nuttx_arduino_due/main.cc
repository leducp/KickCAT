
#include "nuttx/SPI.h"

#include "kickcat/Slave.h"

using namespace kickcat;


int main(int argc, char *argv[])
{
    SPI spi_driver{};
    Lan9252 esc(spi_driver);

    Slave lan9252(esc);

    uint32_t pdo_size = 32;

    uint8_t buffer_in[pdo_size];
    uint8_t buffer_out[pdo_size];

    for (uint32_t i=0; i < pdo_size; ++i)
    {
        buffer_in[i] = i;
    }

    SyncManagerConfig process_data_out(0, 0x1000, pdo_size, 0x64); // Process data out (master view)
    SyncManagerConfig process_data_in(1, 0x1200, pdo_size, 0x20); // Process data in (master view)

    lan9252.set_sm_mailbox_config({{}});
    lan9252.set_process_data_input(buffer_in, process_data_in);
    lan9252.set_process_data_output(buffer_out, process_data_out);

    lan9252.init();


    while(true)
    {
        lan9252.routine();
        for (uint8_t i = 0; i < pdo_size; ++i)
        {
            printf("%x", buffer_out[i]);
        }
        printf("\n");

        usleep(1000);
    }

    return 0;
}
