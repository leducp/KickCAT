#ifndef KICKCAT_SLAVE_KICKOS_SPI_H_
#define KICKCAT_SLAVE_KICKOS_SPI_H_

#include "kickcat/AbstractSPI.h"
#include <string>

namespace kickcat
{
    // AbstractSPI over the KickOS K64F/DSPI0 unprivileged transport
    // (user/apps/k64dspi). transfer() forwards to the C symbol spi_transfer();
    // CS maps to spi_enable_cs()/spi_disable_cs() (DSPI PUSHR.CONT hold on PCS0).
    // The DSPI master itself is brought up privileged by the KickOS app shim before
    // the slave thread runs -- this class does no MMIO.
    class SPI final : public AbstractSPI
    {
    public:
        SPI() = default;
        virtual ~SPI();

        void open(std::string const& device, uint8_t CPOL, uint8_t CPHA, uint32_t baudrate) override;
        void close() override;

        void transfer(uint8_t const* data_write, uint8_t* data_read, uint32_t size) override;

        void enableChipSelect() override;
        void disableChipSelect() override;

        void setMode(uint8_t CPOL, uint8_t CPHA) override;
        void setBaudRate(uint32_t baudrate) override;
    };
}

#endif
