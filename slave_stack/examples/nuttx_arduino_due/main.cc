
#include "nuttx/SPI.h"

#include "kickcat/Slave.h"

using namespace kickcat;


int main(int argc, char *argv[])
{
    SPI spi_driver{};
    Lan9252 esc(spi_driver);

    Slave lan9252(esc);

    SyncManagerConfig process_data_out(0, 0x1000, 32, 0x64); // Process data out
    SyncManagerConfig process_data_in(1, 0x1200, 32, 0x20); // Process data in

    lan9252.set_sm_config({{}}, {process_data_out, process_data_in});

    lan9252.init();


    while(true)
    {
        lan9252.routine();
        usleep(1000);
    }

    return 0;
}
