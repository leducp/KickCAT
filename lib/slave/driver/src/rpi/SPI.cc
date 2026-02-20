#include "kickcat/rpi/SPI.h"
#include "kickcat/Error.h"
#include "kickcat/debug.h"
#include <algorithm>

namespace kickcat
{

    SPI::SPI() = default;

    SPI::~SPI()
    {
        close();
    }

    void SPI::close()
    {
        bcm2835_spi_end();
        bcm2835_close();
    }

    void SPI::open(std::string const &device, uint8_t CPOL, uint8_t CPHA, uint32_t baudrate)
    {
        if (!bcm2835_init())
        {
            THROW_SYSTEM_ERROR("bcm2835_init failed. Are you running as root?");
        }

        if (!bcm2835_spi_begin())
        {
            bcm2835_close();
            THROW_SYSTEM_ERROR("bcm2835_spi_begin failed. Are you running as root?");
        }

        device_ = device;

        // Set SPI bit order
        bcm2835_spi_setBitOrder(BCM2835_SPI_BIT_ORDER_MSBFIRST);

        // Disable management of CS pin, we will do it manually
        bcm2835_spi_chipSelect(BCM2835_SPI_CS_NONE);

        setMode(CPOL, CPHA);
        setBaudRate(baudrate);

        // Configure CS pin as output and set it high (inactive)
        bcm2835_gpio_fsel(chipSelect_, BCM2835_GPIO_FSEL_OUTP);
        bcm2835_gpio_write(chipSelect_, HIGH);

        spi_info("Configuring RPi SPI: baud=%u, CPOL=%u, CPHA=%u, CS_GPIO=%u", baudrate_, CPOL_, CPHA_, chipSelect_);
    }

    void SPI::transfer(uint8_t const *data_write, uint8_t *data_read, uint32_t size)
    {
        if (data_write != nullptr && data_read != nullptr)
        {
            bcm2835_spi_transfernb(const_cast<char *>(reinterpret_cast<char const *>(data_write)),
                                   reinterpret_cast<char *>(data_read),
                                   size);
        }
        else if (data_write != nullptr)
        {
            bcm2835_spi_writenb(const_cast<char *>(reinterpret_cast<char const *>(data_write)), size);
        }
        else if (data_read != nullptr)
        {
            // If we only want to read, we must send something (usually zeros)
            // Use a local buffer for chunked transfer to avoid large stack allocations
            uint8_t dummy[256] = {0};
            uint32_t remaining = size;
            uint8_t *read_ptr = data_read;
            while (remaining > 0)
            {
                uint32_t const chunk = std::min(remaining, static_cast<uint32_t>(sizeof(dummy)));
                bcm2835_spi_transfernb(reinterpret_cast<char *>(dummy), reinterpret_cast<char *>(read_ptr), chunk);
                remaining -= chunk;
                read_ptr += chunk;
            }
        }
    }

    void SPI::enableChipSelect()
    {
        bcm2835_gpio_write(chipSelect_, LOW);
    }

    void SPI::disableChipSelect()
    {
        bcm2835_gpio_write(chipSelect_, HIGH);
    }

    void SPI::setMode(uint8_t CPOL, uint8_t CPHA)
    {
        if (CPOL > 1 || CPHA > 1)
        {
            THROW_SYSTEM_ERROR("Invalid SPI mode");
        }

        bcm2835_spi_setDataMode(SPI_MODES[CPOL][CPHA]);
        CPOL_ = CPOL;
        CPHA_ = CPHA;
    }

    void SPI::setBaudRate(uint32_t baudrate)
    {
        uint32_t target_divider = (baudrate > 0) ? (CORE_CLOCK_HZ / baudrate) : 65536;

        auto it = CLOCK_TABLE.lower_bound(target_divider);

        uint32_t actual_divider = (it != CLOCK_TABLE.end()) ? it->first : 65536;
        uint16_t bcm_divider = (it != CLOCK_TABLE.end()) ? it->second : BCM2835_SPI_CLOCK_DIVIDER_65536;

        bcm2835_spi_setClockDivider(bcm_divider);
        baudrate_ = CORE_CLOCK_HZ / actual_divider;
    }
}
