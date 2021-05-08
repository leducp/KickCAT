#include <cstring>
//#include <algorithm>

#include "Bus.h"

namespace kickcat
{
    void Bus::readSDO(Slave& slave, uint16_t index, uint8_t subindex, Access CA, void* data, uint32_t* data_size)
    {
        if ((CA == Access::PARTIAL) or (CA == Access::COMPLETE))
        {
            auto sdo = slave.mailbox.createSDO(index, subindex, CA, CoE::SDO::request::UPLOAD, data, data_size);
            while (sdo->status() == MessageStatus::RUNNING)
            {
                processMessages();
                sleep(200us);
            }
            return;
        }

        // emulate complete access
        int32_t object_size = 0;
        uint32_t size = sizeof(object_size);
        auto sdo = slave.mailbox.createSDO(index, 0, false, CoE::SDO::request::UPLOAD, &object_size, &size);
        while (sdo->status() == MessageStatus::RUNNING)
        {
            processMessages();
            sleep(200us);
        }

        uint8_t* pos = reinterpret_cast<uint8_t*>(data);
        size = *data_size;
        uint32_t already_read = 0;
        for (int32_t i = 1; i <= object_size; ++i)
        {
            size = *data_size - already_read;
            if (size == 0)
            {
                THROW_ERROR("Error while reading SDO - client buffer too small");
            }

            sdo = slave.mailbox.createSDO(index, i, false, CoE::SDO::request::UPLOAD, pos, &size);
            while (sdo->status() == MessageStatus::RUNNING)
            {
                processMessages();
                sleep(200us);
            }

            if (sdo->status() != MessageStatus::SUCCESS)
            {
                THROW_ERROR("Error while reading SDO - emulated complete access");
            }

            pos += size;
            already_read += size;
        }

        *data_size = already_read;
    }


    void Bus::writeSDO(Slave& slave, uint16_t index, uint8_t subindex, bool CA, void* data, uint32_t data_size)
    {
        auto sdo = slave.mailbox.createSDO(index, subindex, CA, CoE::SDO::request::DOWNLOAD, data, &data_size);
        while (sdo->status() == MessageStatus::RUNNING)
        {
            processMessages();
            sleep(200us);
        }
    }
}
