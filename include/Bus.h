#ifndef KICKCAT_BUS_H
#define KICKCAT_BUS_H

#include <memory>
#include <tuple>
#include <vector>
#include <functional>

#include "Error.h"
#include "Frame.h"

namespace kickcat
{
    class AbstractSocket;

    struct Slave
    {
        uint16_t address;

        uint32_t vendor_id;
        uint32_t product_code;
        uint32_t revision_number;
        uint32_t serial_number;

        struct mailbox
        {
            uint16_t recv_offset;
            uint16_t recv_size;
            uint16_t send_offset;
            uint16_t send_size;
        };
        mailbox mailbox_booststrap;
        mailbox mailbox_standard;
        MailboxProtocol supported_mailbox;

        uint32_t eeprom_size; // in bytes
        uint16_t eeprom_version;
    };

    class Bus
    {
    public:
        Bus(std::shared_ptr<AbstractSocket> socket);
        ~Bus() = default;

        Error init();
        Error requestState(State request);
        State getCurrentState(uint16_t slave);

        uint16_t getSlavesOnNetwork();

        void printSlavesInfo();

    private:
        Error detectSlaves();
        Error resetSlaves();

        Error fetchEeprom();
        bool areEepromReady();
        Error readEeprom(uint16_t address, std::function<void(Slave&, uint32_t word)> apply);

        std::shared_ptr<AbstractSocket> socket_;
        Frame frame_;
        std::vector<Slave> slaves_;
    };
}

#endif
