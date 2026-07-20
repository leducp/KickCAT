
#include "freedom_app.h"

#include "kickcat/nuttx/SPI.h"

#include <arch/board/board.h>
#include <nuttx/board.h>
#include <nuttx/sensors/fxos8700cq.h>
#include <nuttx/leds/userled.h>

#include <sys/ioctl.h>
#include <fcntl.h>
#include <cstdio>
#include <cstring>

using namespace kickcat;

namespace
{
    // LED mask: bit0=R, bit1=G, bit2=B
    constexpr uint8_t LED_R_BIT = 1 << 0;
    constexpr uint8_t LED_G_BIT = 1 << 1;
    constexpr uint8_t LED_B_BIT = 1 << 2;

    struct NuttxCtx
    {
        int sensor_fd;
        int led_fd;
    };

    // Process data source in OPERATIONAL: read the FXOS8700 accel into the TxPDO and
    // drive the userleds from the RxPDO.
    void nuttx_data_source(void* vctx, freedom::Pdo const& io)
    {
        NuttxCtx* ctx = static_cast<NuttxCtx*>(vctx);

        fxos8700cq_data sensor_data;
        if (read(ctx->sensor_fd, &sensor_data, sizeof(sensor_data)) == sizeof(sensor_data))
        {
            *io.ax = sensor_data.accel.x;
            *io.ay = sensor_data.accel.y;
            *io.az = sensor_data.accel.z;
            *io.mx = sensor_data.magn.x;
            *io.my = sensor_data.magn.y;
            *io.mz = sensor_data.magn.z;
        }

        userled_set_t led_set = 0;
        if (*io.led_r)
        {
            led_set |= LED_R_BIT;
        }
        if (*io.led_g)
        {
            led_set |= LED_G_BIT;
        }
        if (*io.led_b)
        {
            led_set |= LED_B_BIT;
        }

        if (ioctl(ctx->led_fd, ULEDIOC_SETALL, led_set) < 0)
        {
            printf("ERROR: ioctl(ULEDIOC_SETALL) failed: %s\n", strerror(errno));
        }
    }
}

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    std::shared_ptr<SPI> spi_driver = std::make_shared<SPI>();
    spi_driver->open("/dev/spi0", 0, 0, 10000000);

    // Init sensor
    int sensor_fd = open("/dev/accel0", O_RDONLY);
    if (sensor_fd < 0)
    {
        printf("Failed to open sensor device\n");
        return -1;
    }

    // Init userleds
    int led_fd = open("/dev/userleds", O_WRONLY);
    if (led_fd < 0)
    {
        printf("Failed to open LED driver\n");
        return -1;
    }

    NuttxCtx ctx;
    ctx.sensor_fd = sensor_fd;
    ctx.led_fd = led_fd;

    freedom::app_run(spi_driver, nuttx_data_source, &ctx);

    close(sensor_fd);
    close(led_fd);

    return 0;
}
