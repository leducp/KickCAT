
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
    int32_t rc = esc.init();
    if (rc < 0)
    {
        printf("error init %ld - %s\n", rc, strerror(-rc));
    }
    PDO pdo(&esc);
    slave::Slave slave(&esc, &pdo);

    constexpr uint32_t pdo_size = 16;

    uint8_t buffer_in[pdo_size];
    uint8_t buffer_out[pdo_size];

    // init values
    for (uint32_t i = 0; i < pdo_size; ++i)
    {
        buffer_in[i] = i;
        buffer_out[i] = 0xFF;
    }

    mailbox::response::Mailbox mbx(&esc, 1024);
    auto dictionary = CoE::createOD();
    mbx.enableCoE(std::move(dictionary));

    slave.setMailbox(&mbx);
    pdo.setInput(buffer_in);
    pdo.setOutput(buffer_out);

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
        slave.routine();
        // Print received data
        //for (uint8_t i = 0; i < pdo_size; ++i)
        //{
        //    printf("%x", buffer_out[i]);
        //}
        //printf("\r");

        if (slave.state() == State::SAFE_OP)
        {
            if (buffer_out[1] != 0xFF)
            {
                slave.validateOutputData();
            }
        }

        //TODO: update with freedom sensors
        buffer_in[0]++;
    }

    return 0;
}
