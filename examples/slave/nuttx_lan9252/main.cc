
#include "kickcat/nuttx/SPI.h"
#include "kickcat/ESC/Lan9252.h"
#include "kickcat/protocol.h"
#include "kickcat/Time.h"

#include <nuttx/board.h>
#include <arch/board/board.h>


using namespace kickcat;


int main(int argc, char *argv[])
{
    (void) argc;
    (void) argv;
    std::shared_ptr<SPI> spi_driver = std::make_shared<SPI>();
    Lan9252 esc = Lan9252(spi_driver);

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
    SyncManagerConfig process_data_out{0, 0x1000, pdo_size, 0x64}; // Process data out (master view)
    SyncManagerConfig process_data_in{1, 0x1200, pdo_size, 0x20}; // Process data in (master view)

    esc.set_mailbox_config({{}});
    esc.set_process_data_input(buffer_in, process_data_in);
    esc.set_process_data_output(buffer_out, process_data_out);

    esc.init();

    uint8_t esc_config;
    reportError(esc.read(reg::ESC_CONFIG, &esc_config, sizeof(esc_config)));

    bool is_emulated = esc_config & PDI_EMULATION;
    printf("esc config 0x%x, is emulated %i \n", esc_config, is_emulated);

    uint8_t pdi_config;
    reportError(esc.read(reg::PDI_CONFIGURATION, &pdi_config, sizeof(pdi_config)));
    printf("pdi config 0x%x \n", pdi_config);

    while(true)
    {
        esc.routine();
        // Print received data
    //    for (uint8_t i = 0; i < pdo_size; ++i)
    //    {
    //        printf("%x", buffer_out[i]);
    //    }
    //    printf("\n");

       if (esc.al_status() & State::SAFE_OP)
       {
           if (buffer_out[1] != 0xFF)
           {
               esc.set_valid_output_data_received(true);
           }
       }
       sleep(1ms);
    }
    return 0;
}
