#ifndef KICKCAT_SIMULATION_ESC_H
#define KICKCAT_SIMULATION_ESC_H

#include "kickcat/protocol.h"

namespace kickcat
{
    class ESC
    {
    public:
        void processDatagram(DatagramHeader* header, uint8_t* data, uint16_t* wkc);

    private:
        uint8_t registers_[0x1000];
        uint16_t * const station_address_{reinterpret_cast<uint16_t*>(registers_ + reg::STATION_ADDR)};

        void processReadCommand     (DatagramHeader* header, uint8_t* data, uint16_t* wkc, uint16_t offset);
        void processWriteCommand    (DatagramHeader* header, uint8_t* data, uint16_t* wkc, uint16_t offset);
        void processReadWriteCommand(DatagramHeader* header, uint8_t* data, uint16_t* wkc, uint16_t offset);
    };
};

#endif
