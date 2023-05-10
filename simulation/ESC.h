#ifndef KICKCAT_SIMULATION_ESC_H
#define KICKCAT_SIMULATION_ESC_H

#include "kickcat/protocol.h"
#include <vector>

namespace kickcat
{
    class ESC
    {
    public:
        ESC(std::string const& eeprom);

        void processDatagram(DatagramHeader* header, uint8_t* data, uint16_t* wkc);

    private:
        uint8_t registers_[0x1000];
        uint16_t* const station_address_;

        std::vector<uint16_t> eeprom_; // EEPPROM addressing is word/16 bits
        nanoseconds eeprom_busy_latency{0ns};

        void processReadCommand     (DatagramHeader* header, uint8_t* data, uint16_t* wkc, uint16_t offset);
        void processWriteCommand    (DatagramHeader* header, uint8_t* data, uint16_t* wkc, uint16_t offset);
        void processReadWriteCommand(DatagramHeader* header, uint8_t* data, uint16_t* wkc, uint16_t offset);
    };
};

#endif
