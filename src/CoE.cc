#include <cstring>

#include "Bus.h"

namespace kickcat
{
    void Bus::SDORequest(Slave& slave, uint16_t index, uint8_t subindex, bool CA, uint8_t request, void const* data, uint32_t size)
    {
        uint8_t buffer[256]; // TODO: adapt to mailbox size
        mailbox::Header* header   = reinterpret_cast<mailbox::Header*>(buffer);
        mailbox::ServiceData* coe = reinterpret_cast<mailbox::ServiceData*>(buffer + sizeof(mailbox::Header));
        uint8_t* payload          = reinterpret_cast<uint8_t*>(buffer + sizeof(mailbox::Header) + sizeof(mailbox::ServiceData));

        header->len      = 10;
        header->address  = 0; // master
        header->priority = 0; // unused
        header->channel  = 0;
        header->type     = mailbox::Type::CoE;
        header->count    = slave.mailbox.nextCounter();

        coe->number = 0;
        coe->service = CoE::Service::SDO_REQUEST;
        coe->complete_access = CA;
        coe->command         = request;
        coe->block_size      = 0;
        coe->transfer_type   = 0;
        coe->size_indicator  = 0;
        coe->index    = index;
        coe->subindex = subindex;

        if (request == CoE::SDO::request::DOWNLOAD)
        {
            if (size > (slave.mailbox.recv_size - 10))
            {
                printf("This download shall be segmented: I dunno how to do it. Abort");
                return;
            }

            if (size <= 4)
            {
                // expedited transfer
                coe->transfer_type  = 1;
                coe->size_indicator = 1;
                coe->block_size = 4 - size;
                std::memcpy(payload, data, size);
            }
            else
            {
                header->len += size;
                std::memcpy(payload, &size, sizeof(uint32_t));
                payload += sizeof(uint32_t);
                std::memcpy(payload, data, size);
            }
        }

        if (request == CoE::SDO::request::UPLOAD_SEGMENTED)
        {
            coe->complete_access = slave.mailbox.toggle; // CA bit is used for toggle bit in segmented transfer
        }

        addDatagram(Command::FPWR, createAddress(slave.address, slave.mailbox.recv_offset), buffer, slave.mailbox.recv_size);
        Error err = processFrames();
        if (err)
        {
            err.what();
            return;
        }
        auto [h, d, wkc] = nextDatagram<uint8_t>();
        if (wkc != 1)
        {
            printf("No answer from slave\n");
        }
    }

    void Bus::readSDO(Slave& slave, uint16_t index, uint8_t subindex, bool CA, void* data, uint32_t* data_size)
    {
        SDORequest(slave, index, subindex, CA, CoE::SDO::request::UPLOAD);
        if (not waitForMessage(slave, 20ms))
        {
            return;
        }

        addDatagram(Command::FPRD, createAddress(slave.address, slave.mailbox.send_offset), nullptr, slave.mailbox.send_size);
        Error err = processFrames();
        if (err)
        {
            err.what();
            return;
        }

        auto [h, buffer, wkc] = nextDatagram<uint8_t>();
        if (wkc != 1)
        {
            printf("No answer from slave again\n");
            return;
        }
        mailbox::Header const* header = reinterpret_cast<mailbox::Header const*>(buffer);
        mailbox::ServiceData const* coe = reinterpret_cast<mailbox::ServiceData const*>(buffer + sizeof(mailbox::Header));
        uint8_t const* payload = buffer + sizeof(mailbox::Header) + sizeof(mailbox::ServiceData);

        if (header->type == mailbox::Type::ERROR)
        {
            //TODO handle error properly
            printf("An error happened !");
            return;
        }

        if (header->type != mailbox::Type::CoE)
        {
            printf("Header type unexpected %d\n", header->type);
            return;
        }

        if (coe->service == CoE::Service::EMERGENCY)
        {
            printf("Houston, we've got a situation here\n");
            return;
        }

        if (coe->service == CoE::Service::SDO_REQUEST)
        {
            if (coe->command == CoE::SDO::request::ABORT)
            {
                uint32_t code = *reinterpret_cast<uint32_t const*>(payload);
                std::string_view text = CoE::SDO::abort_to_str(code);
                printf("Abort requested! code %08x - %.*s\n", code, text.size(), text.data());
                return;
            }
            printf("OK guy, this one answer, but miss the point: %x\n", coe->service);
            return;
        }

        if (coe->service != CoE::Service::SDO_RESPONSE)
        {
            printf("Not for us: maybe someone else could use this one");
            return;
        }

        if (coe->command != CoE::SDO::response::UPLOAD)
        {
            printf("Well, this one answer for another request than upload?\n");
            return;
        }

        if ((coe->index != index) or (coe->subindex != subindex))
        {
            printf("wrong index or subindex!\n");
            return;
        }

        if (coe->transfer_type == 1)
        {
            // expedited transfer
            int32_t size = 4 - coe->block_size;
            if(*data_size < size)
            {
                printf("Really? Not enough size in client buffer?\n");
                return;
            }
            printf("size: %d - blk %d - client %d\n", size, coe->block_size, *data_size);
            std::memcpy(data, payload, size);
            *data_size = size;
            return;
        }

        // standard or segmented transfer
        uint32_t size = *reinterpret_cast<uint32_t const*>(payload);
        payload += 4;

        if ((header->len - 10) >= size)
        {
            // standard
            if (*data_size < size)
            {
                printf("Really? Not enough size in client buffer?\n");
                return;
            }
            std::memcpy(data, payload, size);
            *data_size = size;
            return;
        }

        // segmented
        uint8_t* pos = reinterpret_cast<uint8_t*>(data);
        uint32_t complete_size = size;
        uint32_t already_written = 0;
        if (*data_size < complete_size)
        {
            printf("Really? Not enough size in client buffer?\n");
            return;
        }

        size = *reinterpret_cast<uint32_t const*>(payload);
        payload += 4;
        std::memcpy(pos, payload, size);
        pos += size;
        already_written += size;

        while (true)
        {
            if (not waitForMessage(slave, 20ms))
            {
                return;
            }

            SDORequest(slave, 0, 0, false, CoE::SDO::request::UPLOAD_SEGMENTED);
            if (not waitForMessage(slave, 20ms))
            {
                return;
            }

            addDatagram(Command::FPRD, createAddress(slave.address, slave.mailbox.send_offset), nullptr, slave.mailbox.send_size);
            err = processFrames();
            if (err)
            {
                err.what();
                return;
            }

            auto [h, segment, wkc] = nextDatagram<uint8_t>();
            if (wkc != 1)
            {
                printf("No answer from slave again\n");
                return;
            }

            header = reinterpret_cast<mailbox::Header const*>(segment);
            coe = reinterpret_cast<mailbox::ServiceData const*>(segment + sizeof(mailbox::Header));
            payload = segment + sizeof(mailbox::Header) + sizeof(mailbox::ServiceData);

            size = 0;
            if (header->len == 10)
            {
                size = 7 - (coe->block_size | (coe->size_indicator << 2));
            }
            else
            {
                size = *reinterpret_cast<uint32_t const*>(payload);
                payload += 4;
            }
            if (coe->complete_access != slave.mailbox.toggle)
            {
                printf("toggle bit is different!");
            }
            slave.mailbox.toggle = not slave.mailbox.toggle; // for next call

            if ((complete_size - already_written) < size)
            {
                printf("something wrong here: we are going to overflow, but this should happened!");
                return;
            }
            std::memcpy(pos, payload, size);
            pos += size;
            already_written += size;

            bool more_follow = coe->size_indicator;
            if (not more_follow)
            {
                slave.mailbox.toggle = false;
                *data_size = already_written;
                return; // finished!
            }
        }
    }


    void Bus::writeSDO(Slave& slave, uint16_t index, uint8_t subindex, bool CA, void const* data, uint32_t data_size)
    {
        SDORequest(slave, index, subindex, CA, CoE::SDO::request::DOWNLOAD, data, data_size);

        if (not waitForMessage(slave, 20ms))
        {
            return;
        }

        addDatagram(Command::FPRD, createAddress(slave.address, slave.mailbox.send_offset), nullptr, slave.mailbox.send_size);
        Error err = processFrames();
        if (err)
        {
            err.what();
            return;
        }

        auto [h, buffer, wkc] = nextDatagram<uint8_t>();
        if (wkc != 1)
        {
            printf("No answer from slave again\n");
            return;
        }
        mailbox::Header const* header = reinterpret_cast<mailbox::Header const*>(buffer);
        mailbox::ServiceData const* coe = reinterpret_cast<mailbox::ServiceData const*>(buffer + sizeof(mailbox::Header));
        uint8_t const* payload = buffer + sizeof(mailbox::Header) + sizeof(mailbox::ServiceData);

        if (header->type == mailbox::Type::ERROR)
        {
            //TODO handle error properly
            printf("An error happened !");
            return;
        }

        if (header->type != mailbox::Type::CoE)
        {
            printf("Header type unexpected %d\n", header->type);
            return;
        }

        if (coe->service == CoE::Service::EMERGENCY)
        {
            printf("Houston, we've got a situation here\n");
            return;
        }

        if (coe->service == CoE::Service::SDO_REQUEST)
        {
            if (coe->command == CoE::SDO::request::ABORT)
            {
                uint32_t code = *reinterpret_cast<uint32_t const*>(payload);
                std::string_view text = CoE::SDO::abort_to_str(code);
                printf("Abort requested! code %08x - %.*s\n", code, text.size(), text.data());
                return;
            }
            printf("OK guy, this one answer, but miss the point: %x\n", coe->service);
            return;
        }

        if (coe->service != CoE::Service::SDO_RESPONSE)
        {
            printf("Not for us: maybe sopmeone else could use this one");
            return;
        }

        if (coe->command != CoE::SDO::response::DOWNLOAD)
        {
            printf("Well, this one answer for another request than download?\n");
            return;
        }

        if ((coe->index != index) or (coe->subindex != subindex))
        {
            printf("wrong index or subindex!\n");
            return;
        }

        // OK!
    }

}
