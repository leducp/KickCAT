
#include "kickcat/ESC/Lan9252.h"
#include "kickcat/OS/Time.h"
#include "kickcat/PDO.h"
#include "kickcat/nuttx/SPI.h"
#include "kickcat/protocol.h"
#include "kickcat/slave/Slave.h"

#include <arch/board/board.h>
#include <nuttx/board.h>
#include <nuttx/sensors/fxos8700cq.h>

#include <fcntl.h>
#include <cstdio>

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

    // init sensor
    int fd = open("/dev/accel0", O_RDONLY);
    if (fd < 0)
    {
        printf("Failed to open sensor device\n");
        return -1;
    }

    fxos8700cq_data sensor_data;


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

        // Read sensor data
        if (read(fd, &sensor_data, sizeof(sensor_data)) == sizeof(sensor_data))
        {
            // Convert to int16_t and fill PDO buffer
            buffer_in[0] = sensor_data.accel.x & 0xFF;
            buffer_in[1] = (sensor_data.accel.x >> 8) & 0xFF;

            buffer_in[2] = sensor_data.accel.y & 0xFF;
            buffer_in[3] = (sensor_data.accel.y >> 8) & 0xFF;

            buffer_in[4] = sensor_data.accel.z & 0xFF;
            buffer_in[5] = (sensor_data.accel.z >> 8) & 0xFF;

            buffer_in[6] = sensor_data.magn.x & 0xFF;
            buffer_in[7] = (sensor_data.magn.x >> 8) & 0xFF;

            buffer_in[8] = sensor_data.magn.y & 0xFF;
            buffer_in[9] = (sensor_data.magn.y >> 8) & 0xFF;

            buffer_in[10] = sensor_data.magn.z & 0xFF;
            buffer_in[11] = (sensor_data.magn.z >> 8) & 0xFF;
        }
    }

    close(fd);

    return 0;
}
