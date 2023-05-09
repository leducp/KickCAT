#include "ESC.h"

#include <cstring>

namespace kickcat
{
    void ESC::processDatagram(DatagramHeader * header, uint8_t * data, uint16_t * wkc)
    {
        auto [position, offset] = extractAddress(header->address);
        switch (header->command)
        {
            case Command::NOP :
            {
                return;
            }

            case Command::APRD:
            {
                if (position == 0)
                {
                    processReadCommand(header, data, wkc, offset);
                }
                ++position;
                header->address = createAddress(position, offset);
                return;
            }
            case Command::APWR:
            {
                if (position == 0)
                {
                    processWriteCommand(header, data, wkc, offset);
                }
                ++position;
                header->address = createAddress(position, offset);
                return;
            }
            case Command::APRW:
            {
                if (position == 0)
                {
                    processReadWriteCommand(header, data, wkc, offset);
                }
                ++position;
                header->address = createAddress(position, offset);
                return;
            }
            case Command::FPRD:
            {
                if (position == *station_address_)
                {
                    processReadCommand(header, data, wkc, offset);
                }
                return;
            }
            case Command::FPWR:
            {
                if (position == *station_address_)
                {
                    processWriteCommand(header, data, wkc, offset);
                }
                return;
            }
            case Command::FPRW:
            {
                if (position == *station_address_)
                {
                    processReadWriteCommand(header, data, wkc, offset);
                }
                return;
            }
            case Command::BRD :
            {
                processReadCommand(header, data, wkc, offset);
                return;
            }
            case Command::BWR :
            {
                processWriteCommand(header, data, wkc, offset);
                return;
            }
            case Command::BRW :
            {
                processReadWriteCommand(header, data, wkc, offset);
                return;
            }
            case Command::LRD : { return; }
            case Command::LWR : { return; }
            case Command::LRW : { return; }
            case Command::ARMW: { return; }
            case Command::FRMW: { return; }
        }
    }


    void ESC::processReadCommand(DatagramHeader* header, uint8_t* data, uint16_t* wkc, uint16_t offset)
    {
        std::memcpy(data, registers_ + offset, header->len);
        *wkc += 1;
    }

    void ESC::processWriteCommand(DatagramHeader* header, uint8_t* data, uint16_t* wkc, uint16_t offset)
    {
        std::memcpy(registers_ + offset, data, header->len);
        *wkc += 1;
    }

    void ESC::processReadWriteCommand(DatagramHeader* header, uint8_t* data, uint16_t* wkc, uint16_t offset)
    {
        uint8_t swap[0x1000];

        std::memcpy(swap, registers_ + offset, header->len);
        std::memcpy(registers_ + offset, data, header->len);
        std::memcpy(data, swap, header->len);
        *wkc += 3;
    }
}