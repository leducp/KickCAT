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
         // we dont really care about the type, we just want a working counter to detect the number of slaves
        frame_.addDatagram(1, Command::BRD, createAddress(0, reg::TYPE), nullptr, 1);

        Error err = frame_.writeThenRead(socket_);
        if (err) { err.what(); }
        auto [header, data, wkc] = frame_.nextDatagram();

        return wkc;
    }


    Error Bus::init()
    {
        Error err = resetSlaves();

        err += EERROR("Not implemented");
        return err;
    }


    Error Bus::requestState(SM_STATE request)
    {
        uint8_t param = request | SM_STATE::ACK;
        frame_.addDatagram(1, Command::BWR, createAddress(0, reg::AL_CONTROL), param);
        Error err = frame_.writeThenRead(socket_);
        if (err)
        {
            err += EERROR("");
            return err;
        }

        return ESUCCESS;
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

        // what else ?

        // Request INIT state
        err = requestState(SM_STATE::INIT);
        if (err)
        {
            err += EERROR("");
            return err;
        }

        // eeprom to PDI
        // eeprom to master


        return EERROR("not implemented");
    }
}
