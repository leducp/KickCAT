#ifndef KICKCAT_XMC4800_ESC_H
#define KICKCAT_XMC4800_ESC_H

#include "kickcat/AbstractESC.h"

namespace kickcat
{
    constexpr uint32_t ECAT0_BASE_ADDRESS = 0x54010000;
    constexpr uint32_t ECAT0_END_ADDRESS = 0x5401FFFF;


    class XMC4800 : public AbstractESC
    {
    public:
        XMC4800() = default;
        ~XMC4800() = default;

        hresult init() override;

        hresult read(uint16_t address, void* data, uint16_t size) override;

        hresult write(uint16_t address, void const* data, uint16_t size) override;

    private:
    };
}

#endif
