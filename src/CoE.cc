#include <cstring>
#include <cmath>

#include "Bus.h"

namespace kickcat
{
    void Bus::waitForMessage(std::shared_ptr<AbstractMessage> message)
    {
        auto error_callback = [](DatagramState const& state)
        {
            THROW_ERROR_DATAGRAM("error while checking mailboxes", state);
        };

        while (message->status() == MessageStatus::RUNNING)
        {
            checkMailboxes(error_callback);
            processMessages(error_callback);
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
                THROW_ERROR_CODE("Error while reading SDO", sdo->status());
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
                THROW_ERROR_CODE("Error while reading SDO - emulated complete access", sdo->status());
            }

            pos += size;
            already_read += size;
        }

        *data_size = already_read;
    }


    void Bus::writeSDO(Slave& slave, uint16_t index, uint8_t subindex, bool CA, void* data, uint32_t data_size, nanoseconds timeout)
    {
        auto sdo = slave.mailbox.createSDO(index, subindex, CA, CoE::SDO::request::DOWNLOAD, data, &data_size, timeout);
        waitForMessage(sdo);
    }


    void Bus::getObjectDictionnaryList(Slave& slave, CoE::SDO::information::ListType type, void* data, uint32_t* data_size, nanoseconds timeout)
    {
        auto sdo = slave.mailbox.createSDOInfoGetODList(type, data, data_size, timeout);
        waitForMessage(sdo);
        if (sdo->status() != MessageStatus::SUCCESS)
        {
            THROW_ERROR_CODE("Error while get Object Dictionnary List", sdo->status());
        }

        std::vector<uint16_t> index_list(static_cast<uint16_t*>(data) + sizeof(type), static_cast<uint16_t*>(data) + *data_size/2);
        printf("Object dictionnary list: size: %li\n", index_list.size());

        for (auto const& index : index_list)
        {
            printf("index %x \n", index);
        }
    }


    void Bus::getObjectDescription(Slave& slave, uint16_t index, void* data, uint32_t* data_size, nanoseconds timeout)
    {
        auto sdo = slave.mailbox.createSDOInfoGetOD(index, data, data_size, timeout);
        waitForMessage(sdo);
        if (sdo->status() != MessageStatus::SUCCESS)
        {
            THROW_ERROR_CODE("Error while get Object Description", sdo->status());
        }

        CoE::SDO::information::ObjectDescription* description = reinterpret_cast<CoE::SDO::information::ObjectDescription*>(data);
        std::string name(static_cast<char*>(data) + sizeof(CoE::SDO::information::ObjectDescription), *data_size - sizeof(CoE::SDO::information::ObjectDescription));

        printf("Received object desc: %s ", toString(*description, name).c_str());
    }


    void Bus::getEntryDescription(Slave& slave, uint16_t index, uint8_t subindex, CoE::SDO::information::ValueInfo value_info,
                             void* data, uint32_t* data_size, nanoseconds timeout)
    {
        auto sdo = slave.mailbox.createSDOInfoGetED(index, subindex, value_info, data, data_size, timeout);
        waitForMessage(sdo);
        if (sdo->status() != MessageStatus::SUCCESS)
        {
            THROW_ERROR_CODE("Error while get Entry Description", sdo->status());
        }

        CoE::SDO::information::EntryDescription* description = reinterpret_cast<CoE::SDO::information::EntryDescription*>(data);
        printf("Received entry desc: %s \n", toString(*description, static_cast<uint8_t*>(data) + sizeof(CoE::SDO::information::EntryDescription),
                                                   *data_size - sizeof(CoE::SDO::information::EntryDescription)).c_str());
    }

    void Bus::getUnitDescription(Slave& slave, CoE::UnitType unit)
    {
        uint32_t numerator_index = CoE::UNIT_OFFSET + unit.numerator;
        uint32_t denominator_index = CoE::UNIT_OFFSET + unit.denominator;

        uint8_t subindex_name = 1;
        uint8_t subindex_symbol = 2;

        char buffer[256];
        uint32_t size;

        readSDO(slave, numerator_index, subindex_name, Access::PARTIAL, buffer, &size);
        std::string numerator_name(buffer, size);
        readSDO(slave, numerator_index, subindex_symbol, Access::PARTIAL, buffer, &size);
        std::string numerator_symbol(buffer, size);


        readSDO(slave, denominator_index, subindex_name, Access::PARTIAL, buffer, &size);
        std::string denominator_name(buffer, size);
        readSDO(slave, denominator_index, subindex_symbol, Access::PARTIAL, buffer, &size);
        std::string denominator_symbol(buffer, size);

        float_t prefix = std::pow(10, unit.prefix);
        printf("Unit description:\n  Prefix: %e %s / %s \n  name: %s / %s", prefix, numerator_symbol.c_str(), denominator_symbol.c_str(),
                numerator_name.c_str(), denominator_name.c_str());
    }
}
