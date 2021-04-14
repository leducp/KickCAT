#include "Bus.h"
#include "AbstractSocket.h"
#include <unistd.h>

#include <cstring>

namespace kickcat
{
    Bus::Bus(std::shared_ptr<AbstractSocket> socket)
        : socket_{socket}
        , frame_{PRIMARY_IF_MAC}
    {

    }


    uint16_t Bus::getSlavesOnNetwork()
    {
        return static_cast<uint16_t>(slaves_.size());
    }


    Error Bus::detectSlaves()
    {
         // we dont really care about the type, we just want a working counter to detect the number of slaves
        frame_.addDatagram(1, Command::BRD, createAddress(0, reg::TYPE), nullptr, 1);
        Error err = frame_.writeThenRead(socket_);
        if (err)
        {
            return err;
        }

        auto [header, data, wkc] = frame_.nextDatagram();
        slaves_.resize(wkc);
        printf("-*-*-*- %d slave detected on the network -*-*-*-\n", slaves_.size());
        return ESUCCESS;
    }


    Error Bus::init()
    {
        Error err = detectSlaves();
        if (err) { return err; }

        err = resetSlaves();
        if (err) { return err; }

        // set addresses
        for (int i = 0; i < slaves_.size(); ++i)
        {
            slaves_[i].address = 0x1000 + i;
            frame_.addDatagram(0x55, Command::APRW, createAddress(0 - i, reg::STATION_ADDR), slaves_[i].address);
        }
        err = frame_.writeThenRead(socket_);
        if (err)
        {
            return err;
        }


        err = fetchEeprom();
        if (err)
        {
            return err;
        }
/*
        //requestState(SM_STATE::INIT);
        //usleep(5000);
        requestState(State::PRE_OP);
        usleep(5000);
        //requestState(SM_STATE::SAFE_OP);
        //requestState(SM_STATE::OPERATIONAL);

        for (uint16_t slave = 0; slave < 3; ++slave)
        {
            State state = getCurrentState(slave);
            printf("Slave %d state is %s - %x\n", slave, toString(state), state);
        }

        requestState(State::INIT);
        usleep(5000);
        for (uint16_t slave = 0; slave < 3; ++slave)
        {
            State state = getCurrentState(slave);
            printf("Slave %d state is %s - %x\n", slave, toString(state), state);
        }
        */

        err += EERROR("Not implemented");
        return err;
    }


    Error Bus::requestState(State request)
    {
        uint16_t param = request | State::ACK;
        frame_.addDatagram(2, Command::BWR, createAddress(0, reg::AL_CONTROL), param);
        Error err = frame_.writeThenRead(socket_);
        if (err)
        {
            err += EERROR("");
            return err;
        }

        return ESUCCESS;
    }


    State Bus::getCurrentState(uint16_t slave)
    {
        frame_.addDatagram(0x55, Command::APRD, createAddress(0 - slave, reg::AL_STATUS), nullptr, 2);
        Error err = frame_.writeThenRead(socket_);
        if (err)
        {
            err.what();
            return State::INVALID;
        }

        auto [header, data, wkc] = frame_.nextDatagram();
        printf("--> data %x\n", data[0]);
        return State(data[0] & 0xF);
    }


    Error Bus::resetSlaves()
    {
        // buffer to reset them all
        uint8_t param[256];
        std::memset(param, 0, sizeof(param));

        // Set port to auto mode
        frame_.addDatagram(1, Command::BWR, createAddress(0, reg::ESC_DL_PORT), param, 1);
        Error err = frame_.writeThenRead(socket_);
        if (err)
        {
            err += EERROR("");
            return err;
        }

        // reset RX error counters
        frame_.addDatagram(2, Command::BWR, createAddress(0, reg::RX_ERROR), param, 8);
        err = frame_.writeThenRead(socket_);
        if (err)
        {
            err += EERROR("");
            return err;
        }

        // reset FMMU
        frame_.addDatagram(3, Command::BWR, createAddress(0, reg::FMMU), param, 256);
        err = frame_.writeThenRead(socket_);
        if (err)
        {
            err += EERROR("");
            return err;
        }

        // reset Sync Managers
        frame_.addDatagram(4, Command::BWR, createAddress(0, reg::SYNC_MANAGER), param, 128);
        err = frame_.writeThenRead(socket_);
        if (err)
        {
            err += EERROR("");
            return err;
        }

        // reset DC config
        frame_.addDatagram(5, Command::BWR, createAddress(0, reg::DC_SYSTEM_TIME), param, 8);
        err = frame_.writeThenRead(socket_);
        if (err)
        {
            err += EERROR("");
            return err;
        }

        frame_.addDatagram(6, Command::BWR, createAddress(0, reg::DC_SYNC_ACTIVATION), param, 1);
        err = frame_.writeThenRead(socket_);
        if (err)
        {
            err += EERROR("");
            return err;
        }

        uint16_t dc_param = 0x1000; // reset value
        frame_.addDatagram(7, Command::BWR, createAddress(0, reg::DC_SPEED_CNT_START), dc_param);
        err = frame_.writeThenRead(socket_);
        if (err)
        {
            err += EERROR("");
            return err;
        }

        dc_param = 0x0c00; // reset value
        frame_.addDatagram(8, Command::BWR, createAddress(0, reg::DC_TIME_FILTER), dc_param);
        err = frame_.writeThenRead(socket_);
        if (err)
        {
            err += EERROR("");
            return err;
        }

        // Request INIT state
        err = requestState(State::INIT);
        if (err)
        {
            err += EERROR("");
            return err;
        }

        // eeprom to master
        frame_.addDatagram(10, Command::BWR, createAddress(0, reg::EEPROM_CONFIG), param, 2);
        err = frame_.writeThenRead(socket_);
        if (err)
        {
            err += EERROR("");
            return err;
        }

        return ESUCCESS;
    }


    bool Bus::areEepromReady()
    {
        bool ready = false;
        for (int i = 0; (i < 10) and not ready; ++i)
        {
            usleep(200);
            for (auto& slave : slaves_)
            {
                frame_.addDatagram(1, Command::FPRD, createAddress(slave.address, reg::EEPROM_CONTROL), nullptr, 2);
            }
            Error err = frame_.writeThenRead(socket_);
            if (err)
            {
                err.what();
                return false;
            }

            DatagramHeader const* header;
            bool ready = true;
            do
            {
                auto [h, data, wkc] = frame_.nextDatagram();
                header = h;
                uint16_t const* answer = reinterpret_cast<uint16_t const*>(data);
                if (wkc != 1)
                {
                    Error err = EERROR("no answer!");
                    err.what();
                }
                if(*answer & 0x8000)
                {
                    frame_.clear();
                    ready = false;
                    break;
                }
            } while(header->multiple);

            if (ready)
            {
                return true;
            }
        }

        return false;
    }

    Error Bus::readEeprom(uint16_t address, std::function<void(Slave&, uint32_t word)> apply)
    {
        // eeprom request
        struct Request
        {
            uint16_t command;
            uint16_t addressLow;
            uint16_t addressHigh;
        } __attribute__((__packed__));

        Request req;

        // Request specific address
        req = { EepromCommand::READ, address, 0 };
        frame_.addDatagram(1, Command::BWR, createAddress(0, reg::EEPROM_CONTROL), req);
        Error err = frame_.writeThenRead(socket_);
        if (err)
        {
            err += EERROR("");
            return err;
        }

        // wait for all eeprom to be ready
        if (not areEepromReady())
        {
            return EERROR("eeprom not ready - timeout");
        }

        // Read result
        for (auto& slave : slaves_)
        {
            frame_.addDatagram(1, Command::FPRD, createAddress(slave.address, reg::EEPROM_DATA), nullptr, 4);
        }
        err = frame_.writeThenRead(socket_);
        if (err)
        {
            return err;
        }

        // Extract result and store it
        for (auto& slave : slaves_)
        {
            auto [header, data, wkc] = frame_.nextDatagram();
            if (wkc != 1)
            {
                Error err = EERROR("no answer!");
                err.what();
            }
            uint32_t const* answer = reinterpret_cast<uint32_t const*>(data);
            apply(slave, *answer);
        }

        return ESUCCESS;
    }

    Error Bus::fetchEeprom()
    {
        // General slave info
        readEeprom(eeprom::VENDOR_ID,       [](Slave& s, uint32_t word) { s.vendor_id       = word; } );
        readEeprom(eeprom::PRODUCT_CODE,    [](Slave& s, uint32_t word) { s.product_code    = word; } );
        readEeprom(eeprom::REVISION_NUMBER, [](Slave& s, uint32_t word) { s.revision_number = word; } );
        readEeprom(eeprom::SERIAL_NUMBER,   [](Slave& s, uint32_t word) { s.serial_number   = word; } );

        // Mailbox info
        readEeprom(eeprom::STANDARD_MAILBOX + eeprom::RECV_MBO_OFFSET,
        [](Slave& s, uint32_t word) { s.mailbox_standard.recv_offset = word; s.mailbox_standard.recv_size = word >> 16; } );
        readEeprom(eeprom::STANDARD_MAILBOX + eeprom::SEND_MBO_OFFSET,
        [](Slave& s, uint32_t word) { s.mailbox_standard.send_offset = word; s.mailbox_standard.send_size = word >> 16; } );

        readEeprom(eeprom::MAILBOX_PROTOCOL, [](Slave& s, uint32_t word) { s.supported_mailbox = static_cast<MailboxProtocol>(word); });

        readEeprom(eeprom::EEPROM_SIZE,
        [](Slave& s, uint32_t word)
        {
            s.eeprom_size = (word & 0xFF) + 1; // 0 means 1024 bits
            s.eeprom_size *= 128;              // Kibit to bytes
            s.eeprom_version = word >> 16;
        });

        printSlavesInfo();

        return ESUCCESS;
    }


    void Bus::printSlavesInfo()
    {
        for (auto const& slave : slaves_)
        {
            printf("-*-*-*-*- slave 0x%04x -*-*-*-*-n", slave.address);
            printf("Vendor ID:       0x%08x\n", slave.vendor_id);
            printf("Product code:    0x%08x\n", slave.product_code);
            printf("Revision number: 0x%08x\n", slave.revision_number);
            printf("Serial number:   0x%08x\n", slave.serial_number);
            printf("mailbox in:  size %d - offset 0x%04x\n", slave.mailbox_standard.recv_size, slave.mailbox_standard.recv_offset);
            printf("mailbox out: size %d - offset 0x%04x\n", slave.mailbox_standard.send_size, slave.mailbox_standard.send_offset);
            printf("supported mailbox protocol: 0x%02x\n", slave.supported_mailbox);
            printf("EEPROM: size: %d - version 0x%02x\n",  slave.eeprom_size, slave.eeprom_version);
            printf("\n");
        }
    }
}
