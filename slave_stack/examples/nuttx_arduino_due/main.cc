
#include "nuttx/SPI.h"

#include "kickcat/Slave.h"

using namespace kickcat;


int main(int argc, char *argv[])
{
    printf("Hello \n");
    SPI spi_driver{};
    Lan9252 esc(spi_driver);

    Slave lan9252(esc);

//    std::vector<SyncManagerConfig> process_data_SM_conf;
//    process_data_SM_conf.emplace_back(0, 0x1000, 32, 0x64); // Process data out
//    process_data_SM_conf.emplace_back(1, 0x1200, 32, 0x20); // Process data in
//    printf("Size pd sm %i\n", process_data_SM_conf.size());

    printf("Before set config \n");
    lan9252.set_sm_config({{}}, {{0, 0x1000, 32, 0x64}, {1, 0x1200, 32, 0x20}});

//    lan9252.set_sm_config({{0, 0x1000, 32, 0x64}});
//    lan9252.init();


    printf("init done  \n");
    while(true)
    {
//        lan9252.routine();
        usleep(1000);
    }

    return 0;
}
