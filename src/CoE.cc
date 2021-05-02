#include <cstring>

#include "Bus.h"

namespace kickcat
{
    void Bus::readSDO(Slave& slave, uint16_t index, uint8_t subindex, bool CA, void* data, uint32_t* data_size)
    {
        auto sdo = slave.mailbox.createSDO(index, subindex, CA, CoE::SDO::request::UPLOAD, data, data_size);
        while (sdo->status() == MessageStatus::RUNNING)
        {
            processMessages();
            sleep(200us);
        }
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
