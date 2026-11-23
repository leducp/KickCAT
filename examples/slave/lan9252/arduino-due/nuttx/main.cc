
#include "kickcat/ESC/Lan9252.h"
#include "kickcat/OS/Time.h"
#include "kickcat/PDO.h"
#include "kickcat/nuttx/SPI.h"
#include "kickcat/protocol.h"
#include "kickcat/slave/Slave.h"

#include <arch/board/board.h>
#include <nuttx/board.h>


using namespace kickcat;


int main(int argc, char *argv[])
{
    (void) argc;
    (void) argv;
    std::shared_ptr<SPI> spi_driver = std::make_shared<SPI>();
    spi_driver->open("/dev/spi0", 0, 0, 10000000);

    Lan9252 esc = Lan9252(spi_driver);
    esc.init();
    PDO pdo(&esc);
    slave::Slave slave(&esc, &pdo);

    // The master can request less inputs/ouputs and these buffers are the space
    // that the slave app allocated to let the master play with the mapping.
    constexpr uint32_t PDO_MAX_SIZE = 32;

    uint8_t buffer_in[PDO_MAX_SIZE];
    uint8_t buffer_out[PDO_MAX_SIZE];

    // init values
    for (uint32_t i = 0; i < PDO_MAX_SIZE; ++i)
    {
        buffer_in[i] = i;
        buffer_out[i] = 0xFF;
    }

    pdo.setInput(buffer_in, PDO_MAX_SIZE);
    pdo.setOutput(buffer_out, PDO_MAX_SIZE);


    uint8_t esc_config;
    esc.read(reg::ESC_CONFIG, &esc_config, sizeof(esc_config));

    bool is_emulated = esc_config & PDI_EMULATION;
    printf("esc config 0x%x, is emulated %i \n", esc_config, is_emulated);

    uint8_t pdi_config;
    esc.read(reg::PDI_CONFIGURATION, &pdi_config, sizeof(pdi_config));
    printf("pdi config 0x%x \n", pdi_config);

    slave.start();

    // Validate once per master session: re-validating stale outputs after a watchdog drop flaps OP/SAFE_OP.
    bool outputs_validated = false;

    while (true)
    {
        slave.routine();
        // Print received data
    //    for (uint8_t i = 0; i < PDO_MAX_SIZE; ++i)
    //    {
    //        printf("%x", buffer_out[i]);
    //    }
    //    printf("\n");

        State const state = slave.state();
        if (state == State::INIT or state == State::PRE_OP)
        {
            outputs_validated = false;
        }

        if (state == State::SAFE_OP and not outputs_validated)
        {
            if (buffer_out[1] != 0xFF)
            {
                slave.validateOutputData();
                outputs_validated = true;
            }
        }
        sleep(1ms);
    }
    return 0;
}
