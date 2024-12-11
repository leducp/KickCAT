#ifndef KICKCAT_XMC4800_ESC_H
#define KICKCAT_XMC4800_ESC_H

#include "kickcat/AbstractESC.h"

namespace kickcat
{
// LCOV_EXCL_START
    constexpr uint32_t ECAT0_BASE_ADDRESS = 0x54010000;
    constexpr uint32_t ECAT0_END_ADDRESS  = 0x5401FFFF;

    class XMC4800 final : public AbstractESC
    {
    public:
        XMC4800() = default;
        ~XMC4800() = default;


        int32_t read(uint16_t address, void* data, uint16_t size) override;
        int32_t write(uint16_t address, void const* data, uint16_t size) override;

    private:
        hresult init() override;
    };
// LCOV_EXCL_STOP
}

#endif
