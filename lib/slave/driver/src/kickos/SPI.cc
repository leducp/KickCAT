#include "kickcat/kickos/SPI.h"
#include "kickcat/Error.h"
#include "kickcat/debug.h"

#include <cstddef>

// KickOS DSPI0 transport (KickOS user/apps/k64dspi). Declared locally extern "C"
// so the KickCAT build keeps NO build dependency on the KickOS headers (used, not
// vendored) -- the symbols resolve when the slave application links the KickOS
// transport, the same seam OS/KickOS/Time.cc uses for kos_clock_now/kos_sleep_ns.
//   spi_transfer(tx, rx, len): blocking full-duplex; tx==nullptr shifts 0x00,
//     rx==nullptr discards. Returns bytes transferred, or <0 on error.
//   spi_enable_cs()/spi_disable_cs(): assert/release PCS0 via PUSHR.CONT so CS is
//     held across a CSR header+payload transfer pair (LAN9252 framing).
extern "C"
{
    int  spi_transfer(void* tx, void* rx, size_t len);
    void spi_enable_cs(void);
    void spi_disable_cs(void);
}

namespace kickcat
{
    SPI::~SPI()
    {
        close();
    }

    void SPI::open(std::string const& device, uint8_t CPOL, uint8_t CPHA, uint32_t baudrate)
    {
        // The DSPI master (clock gate, PORTD mux, CTAR0 mode/baud, AIPS slot open) is
        // brought up privileged by the KickOS app shim before this thread runs; the
        // unprivileged slave reaches SPI only through spi_transfer(). open() records
        // the negotiated settings for mode()/baudRate() and performs no MMIO. The
        // recorded CPOL/CPHA/baud MUST match the shim's CTAR0 boot constants.
        device_ = device;
        setMode(CPOL, CPHA);
        setBaudRate(baudrate);
        spi_info("Opened KickOS DSPI transport %s (baud=%u, CPOL=%u, CPHA=%u)\n",
                 device.c_str(), baudrate_, CPOL_, CPHA_);
    }

    void SPI::close()
    {
        // No fd/handle to release: the driver thread owns the DSPI window for the
        // image lifetime. Leave the bus with CS deasserted.
        spi_disable_cs();
    }

    void SPI::transfer(uint8_t const* data_write, uint8_t* data_read, uint32_t size)
    {
        // spi_transfer handles the null-buffer cases (read = null tx, write = null rx)
        // itself; the const_cast is sound because the driver only READS the tx buffer.
        int rc = spi_transfer(const_cast<uint8_t*>(data_write), data_read, size);
        if (rc < 0)
        {
            THROW_SYSTEM_ERROR_CODE("spi_transfer()", -rc);
        }
        if (static_cast<uint32_t>(rc) != size)
        {
            // A short transfer leaves the tail of data_read stale; it would be consumed as
            // LAN9252 register content. Treat anything but a full-length shift as a failure.
            THROW_ERROR("spi_transfer() short transfer");
        }
    }

    void SPI::enableChipSelect()
    {
        spi_enable_cs();
    }

    void SPI::disableChipSelect()
    {
        spi_disable_cs();
    }

    void SPI::setMode(uint8_t CPOL, uint8_t CPHA)
    {
        // Mode is a CTAR0 boot constant owned by the privileged shim; the unprivileged
        // path cannot re-touch it. Record for mode() only (must match the shim).
        CPOL_ = CPOL;
        CPHA_ = CPHA;
    }

    void SPI::setBaudRate(uint32_t baudrate)
    {
        // Baud is CTAR0 PBR/BR, owned by the shim; recorded here for baudRate() only.
        baudrate_ = baudrate;
    }
}
