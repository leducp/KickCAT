
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

        if (slave.state() == State::PRE_OP and pdo_configured)
        {
            pdo_configured = false;
        }

        if (slave.state() == State::SAFE_OP and not pdo_configured)
        {
            auto &dict = mbx.getDictionary();

            auto [obj, entry] = CoE::findObject(dict, 0x6000, 0);
            ax = static_cast<int16_t *>(entry->data);
            auto [obj1, entry1] = CoE::findObject(dict, 0x6001, 0);
            ay = static_cast<int16_t *>(entry1->data);
            auto [obj2, entry2] = CoE::findObject(dict, 0x6002, 0);
            az = static_cast<int16_t *>(entry2->data);
            auto [obj3, entry3] = CoE::findObject(dict, 0x6003, 0);
            mx = static_cast<int16_t *>(entry3->data);
            auto [obj4, entry4] = CoE::findObject(dict, 0x6004, 0);
            my = static_cast<int16_t *>(entry4->data);
            auto [obj5, entry5] = CoE::findObject(dict, 0x6005, 0);
            mz = static_cast<int16_t *>(entry5->data);
            auto [obj6, entry6] = CoE::findObject(dict, 0x7000, 0);
            led_r = static_cast<uint8_t *>(entry6->data);
            auto [obj7, entry7] = CoE::findObject(dict, 0x7001, 0);
            led_g = static_cast<uint8_t *>(entry7->data);
            auto [obj8, entry8] = CoE::findObject(dict, 0x7002, 0);
            led_b = static_cast<uint8_t *>(entry8->data);

            pdo.configureMapping(dict);

            pdo_configured = true;
        }

        if (slave.state() == State::SAFE_OP and buffer_out[0] != 0xFF)
        {
            slave.validateOutputData();
        }

        if (pdo_configured)
        {
            // Read sensors and fill buffer_in
            if (read(sensor_fd, &sensor_data, sizeof(sensor_data)) == sizeof(sensor_data))
            {
                *ax = sensor_data.accel.x;
                *ay = sensor_data.accel.y;
                *az = sensor_data.accel.z;
                *mx = sensor_data.magn.x;
                *my = sensor_data.magn.y;
                *mz = sensor_data.magn.z;
            }

            // Update LEDs based on output PDO
            userled_set_t led_set = 0;
            led_set |= LED_R_BIT;
            led_set |= LED_G_BIT;
            led_set |= LED_B_BIT;

            int ret = ioctl(led_fd, ULEDIOC_SETALL, led_set);
            if (ret < 0)
            {
                int errcode = errno;
                printf("ERROR: ioctl(ULEDIOC_SETALL) failed: %d\n", errcode);
            }
        }
    }

    close(sensor_fd);
    close(led_fd);

    return 0;
}
