
#include "kickcat/ESC/Lan9252.h"
#include "kickcat/OS/Time.h"
#include "kickcat/PDO.h"
#include "kickcat/nuttx/SPI.h"
#include "kickcat/protocol.h"
#include "kickcat/slave/Slave.h"

#include <arch/board/board.h>
#include <nuttx/board.h>
#include <nuttx/sensors/fxos8700cq.h>
#include <nuttx/leds/userled.h>

#include <sys/ioctl.h>
#include <fcntl.h>
#include <cstdio>

using namespace kickcat;

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

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

    // The master can request less inputs/ouputs and these buffers are the space
    // that the slave app allocated to let the master play with the mapping.
    constexpr uint32_t PDO_MAX_SIZE = 16;

    uint8_t buffer_in[PDO_MAX_SIZE];
    uint8_t buffer_out[PDO_MAX_SIZE];

    // init values
    for (uint32_t i = 0; i < PDO_MAX_SIZE; ++i)
    {
        buffer_in[i] = i;
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

    // Init sensor
    int sensor_fd = open("/dev/accel0", O_RDONLY);
    if (sensor_fd < 0)
    {
        printf("Failed to open sensor device\n");
        return -1;
    }

    fxos8700cq_data sensor_data;

    // Init userleds
    int led_fd = open("/dev/userleds", O_WRONLY);
    if (led_fd < 0)
    {
        printf("Failed to open LED driver\n");
        return -1;
    }

    // LED mask: bit0=R, bit1=G, bit2=B
    constexpr uint8_t LED_R_BIT = 1 << 0;
    constexpr uint8_t LED_G_BIT = 1 << 1;
    constexpr uint8_t LED_B_BIT = 1 << 2;

    bool pdo_configured = false;

    int16_t *ax = nullptr;
    int16_t *ay = nullptr;
    int16_t *az = nullptr;
    int16_t *mx = nullptr;
    int16_t *my = nullptr;
    int16_t *mz = nullptr;

    uint8_t *led_r = nullptr;
    uint8_t *led_g = nullptr;
    uint8_t *led_b = nullptr;

    while (true)
    {
        slave.routine();

        const State state = slave.state();

        if (state == State::SAFE_OP)
        {
            auto &dict = mbx.getDictionary();

            slave.bind(0x6000, ax);
            slave.bind(0x6001, ay);
            slave.bind(0x6002, az);
            slave.bind(0x6003, mx);
            slave.bind(0x6004, my);
            slave.bind(0x6005, mz);
            slave.bind(0x7000, led_r);
            slave.bind(0x7001, led_g);
            slave.bind(0x7002, led_b);

            if (buffer_out[1] != 0xFF)
            {
                slave.validateOutputData();
            }
        }
        else if (state == State::OPERATIONAL)
        {

            if (read(sensor_fd, &sensor_data, sizeof(sensor_data)) == sizeof(sensor_data))
            {
                *ax = sensor_data.accel.x;
                *ay = sensor_data.accel.y;
                *az = sensor_data.accel.z;
                *mx = sensor_data.magn.x;
                *my = sensor_data.magn.y;
                *mz = sensor_data.magn.z;
            }

            userled_set_t led_set = 0;
            if (*led_r)
            {
                led_set |= LED_R_BIT;
            }
            if (*led_g)
            {
                led_set |= LED_G_BIT;
            }
            if (*led_b)
            {
                led_set |= LED_B_BIT;
            }

            if (ioctl(led_fd, ULEDIOC_SETALL, led_set) < 0)
            {
                printf("ERROR: ioctl(ULEDIOC_SETALL) failed: %s\n", strerror(errno));
            }
        }
    }

    close(sensor_fd);
    close(led_fd);

    return 0;
}
