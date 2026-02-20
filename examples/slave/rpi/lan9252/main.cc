#include "kickcat/ESC/Lan9252.h"
#include "kickcat/OS/Time.h"
#include "kickcat/PDO.h"
#include "kickcat/rpi/SPI.h"
#include "kickcat/protocol.h"
#include "kickcat/slave/Slave.h"
#include "kickcat/CoE/OD.h"

#include <cstdio>
#include <unistd.h>
#include <memory>

using namespace kickcat;

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    printf("Starting KickCAT Raspberry Pi Slave Counter Example (LAN9252)\n");

    std::shared_ptr<SPI> spi_driver = std::make_shared<SPI>();
    spi_driver->setChipSelect(8); // Default CE0
    spi_driver->open("bcm2835-spi", 0, 0, 10000000);

    Lan9252 esc(spi_driver);
    int32_t rc = esc.init();
    if (rc < 0)
    {
        printf("Error initializing Lan9252: %d\n", rc);
        return rc;
    }

    PDO pdo(&esc);
    slave::Slave slave(&esc, &pdo);

    constexpr uint32_t PDO_MAX_SIZE = 4;
    uint8_t buffer_in[PDO_MAX_SIZE];
    uint8_t buffer_out[PDO_MAX_SIZE];

    for (uint32_t i = 0; i < PDO_MAX_SIZE; ++i)
    {
        buffer_in[i] = 0;
        buffer_out[i] = 0xFF;
    }

    mailbox::response::Mailbox mbx(&esc, 1024);
    auto dictionary = CoE::createOD();
    mbx.enableCoE(std::move(dictionary));

    slave.setMailbox(&mbx);
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

    // Variables for toggling pattern
    uint32_t iteration_counter = 0;
    uint8_t current_value = 0x11;  // Start with 0x11

    constexpr uint32_t ITER = 1000; // Number of iterations before updating input buffer
    

    while (true)
    {
        slave.routine();

        if (slave.state() == State::SAFE_OP)
        {
            if (buffer_out[0] != 0xFF)
            {
                slave.validateOutputData();
            }
        }

        // Update input buffer every ITER iterations
        iteration_counter++;
        if (iteration_counter >= ITER)
        {
            iteration_counter = 0;
            
            // Fill buffer with current value
            for (uint32_t i = 0; i < PDO_MAX_SIZE; ++i)
            {
                buffer_in[i] = current_value;
            }
            
            // Move to next value: 0x11 -> 0x22 -> 0x33 -> ... -> 0xFF -> 0x00 -> 0x11
            if (current_value == 0xFF)
            {
                current_value = 0x00;
            }
            else
            {
                current_value += 0x11;
            }            
        }

        usleep(250); // 250 microsecond sleep to avoid 100% CPU usage
    }

    return 0;
}
