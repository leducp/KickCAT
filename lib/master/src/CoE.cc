#include <cstring>

#include "Bus.h"

namespace kickcat
{
    using namespace mailbox::request;

    void Bus::waitForMessage(std::shared_ptr<AbstractMessage> message)
    {
        auto error_callback_check = [](DatagramState const& state)
        {
            THROW_ERROR_DATAGRAM("error while checking mailboxes", state);
        };

        auto error_callback_process = [](DatagramState const& state)
        {
            THROW_ERROR_DATAGRAM("error while process mailboxes", state);
        };

        while (message->status() == MessageStatus::RUNNING)
        {
            checkMailboxes(error_callback_check);
            processMessages(error_callback_process);
            sleep(tiny_wait);
        }

        if (message->status() == MessageStatus::TIMEDOUT)
        {
            THROW_ERROR("Error while reading SDO - Timeout");
        }
    }


    void Bus::readSDO(Slave& slave, uint16_t index, uint8_t subindex, Access CA, void* data, uint32_t* data_size, nanoseconds timeout)
    {
        if ((CA == Access::PARTIAL) or (CA == Access::COMPLETE))
        {
            auto sdo = slave.mailbox.createSDO(index, subindex, CA, CoE::SDO::request::UPLOAD, data, data_size, timeout);
            waitForMessage(sdo);
            if (sdo->status() != MessageStatus::SUCCESS)
            {
                THROW_ERROR_CODE("Error while reading SDO", error::category::CoE, sdo->status());
            }
            return;
        }

        // emulate complete access
        int32_t object_size = 0;
        uint32_t size = sizeof(object_size);
        auto sdo = slave.mailbox.createSDO(index, 0, false, CoE::SDO::request::UPLOAD, &object_size, &size, timeout);
        waitForMessage(sdo);

        uint8_t* pos = reinterpret_cast<uint8_t*>(data);
        size = *data_size;
        uint32_t already_read = 0;
        for (uint8_t i = 1; i <= object_size; ++i)
        {
            size = *data_size - already_read;
            if (size == 0)
            {
                THROW_ERROR("Error while reading SDO - client buffer too small");
            }

            sdo = slave.mailbox.createSDO(index, i, false, CoE::SDO::request::UPLOAD, pos, &size, timeout);
            waitForMessage(sdo);

            if (sdo->status() != MessageStatus::SUCCESS)
            {
                THROW_ERROR_CODE("Error while reading SDO - emulated complete access", error::category::CoE, sdo->status());
            }

            pos += size;
            already_read += size;
        }

        *data_size = already_read;
    }


    void Bus::writeSDO(Slave& slave, uint16_t index, uint8_t subindex, Access CA, void const* data, uint32_t data_size, nanoseconds timeout)
    {
        if ((CA == Access::PARTIAL) or (CA == Access::COMPLETE))
        {
            auto sdo = slave.mailbox.createSDO(index, subindex, CA, CoE::SDO::request::DOWNLOAD, const_cast<void*>(data), &data_size, timeout);
            waitForMessage(sdo);
            if (sdo->status() != MessageStatus::SUCCESS)
            {
                THROW_ERROR_CODE("Error while writing SDO", error::category::CoE, sdo->status());
            }
            return;
        }

        THROW_SYSTEM_ERROR_CODE("Emulated complete access not supported for write", ENOTSUP);
    }

    void mapPDO(Bus& bus, Slave& slave, uint16_t pdo_map, uint32_t const* mapping, uint8_t mapping_count, uint16_t sm_map)
    {
        uint8_t zeroU8 = 0;

        // Unmap previous registers, setting 0 in PDO_MAP subindex 0
        bus.writeSDO(slave, pdo_map, 0, Bus::Access::PARTIAL, &zeroU8, sizeof(zeroU8));

        // Modify mapping, setting register address in PDO's subindexes
        for (uint8_t i = 0; i < mapping_count; ++i)
        {
            bus.writeSDO(slave, pdo_map, i + 1, Bus::Access::PARTIAL, mapping + i, sizeof(uint32_t));
        }

        // Enable mapping by setting number of registers in PDO_MAP subindex 0
        bus.writeSDO(slave, pdo_map, 0, Bus::Access::PARTIAL, &mapping_count, sizeof(mapping_count));

        // Set PDO mapping to SM
        // Unmap previous mappings, setting 0 in SM_MAP subindex 0
        bus.writeSDO(slave, sm_map, 0, Bus::Access::PARTIAL, &zeroU8, sizeof(zeroU8));

        // Write first mapping (PDO_map) address in SM_MAP subindex 1
        bus.writeSDO(slave, sm_map, 1, Bus::Access::PARTIAL, &pdo_map, sizeof(pdo_map));

        // Save mapping count in SM (here only one PDO_MAP)
        uint8_t pdoMapSize = 1;
        bus.writeSDO(slave, sm_map, 0, Bus::Access::PARTIAL, &pdoMapSize, sizeof(pdoMapSize));
    }
}
