#include <unistd.h>
#include <cstring>

#include "Bus.h"
#include "AbstractSocket.h"

#include <fstream> // debug

namespace kickcat
{
    Bus::Bus(std::shared_ptr<AbstractSocket> socket)
        : socket_{socket}
        , frames_{}
    {
        frames_.emplace_back(PRIMARY_IF_MAC);
    }


    uint16_t Bus::getSlavesOnNetwork()
    {
        return static_cast<uint16_t>(slaves_.size());
    }


    uint16_t Bus::broadcastRead(uint16_t ADO, uint16_t data_size)
    {
        frames_[0].addDatagram(idx_, Command::BRD, createAddress(0, ADO), nullptr, data_size);
        Error err = frames_[0].writeThenRead(socket_);
        if (err)
        {
            err.what();
            return 0;
        }

        auto [header, _, wkc] = frames_[0].nextDatagram();
        ++idx_; // one more frame sent
        return wkc;
    }


    uint16_t Bus::broadcastWrite(uint16_t ADO, void const* data, uint16_t data_size)
    {
        frames_[0].addDatagram(idx_, Command::BWR, createAddress(0, ADO), data, data_size);
        Error err = frames_[0].writeThenRead(socket_);
        if (err)
        {
            err.what();
            return 0;
        }

        auto [header, _, wkc] = frames_[0].nextDatagram();
        ++idx_; // one more frame sent
        return wkc;
    }


    void Bus::addDatagram(enum Command command, uint32_t address, void const* data, uint16_t data_size)
    {
        auto* frame = &frames_[current_frame_];
        if ((frame->datagramCounter() == MAX_ETHERCAT_DATAGRAMS) or (frame->freeSpace() < data_size))
        {
            ++current_frame_;
            frame = &frames_[current_frame_];
        }
        frame->addDatagram(idx_, command, address, data, data_size);
    }


    Error Bus::processFrames()
    {
        current_frame_ = 0; // reset frame position

        for (auto& frame : frames_)
        {
            if (frame.datagramCounter() == 0)
            {
                break;
            }
            Error err = frame.writeThenRead(socket_);
            if (err)
            {
                return err;
            }
        }

        return ESUCCESS;
    }


    Error Bus::detectSlaves()
    {
        // we dont really care about the type, we just want a working counter to detect the number of slaves
        uint16_t wkc = broadcastRead(reg::TYPE, 1);
        if (wkc == 0)
        {
            return EERROR("No slaves on the bus");
        }

        slaves_.resize(wkc);

        // Allocate frames
        int32_t needed_frames = (wkc * 2 / MAX_ETHERCAT_DATAGRAMS + 1) * 2; // at one frame is required. We need to be able to send two datagram per slave in a row (mailboxes check)
        frames_.reserve(needed_frames);
        for (int i = 1; i < needed_frames; ++i)
        {
            frames_.emplace_back(PRIMARY_IF_MAC);
        }

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
            addDatagram(Command::APWR, createAddress(0 - i, reg::STATION_ADDR), slaves_[i].address);
        }
        err = processFrames();
        if (err)
        {
            err.what();
            return err;
        }

        err = fetchEeprom();
        if (err)
        {
            err.what();
            return err;
        }

        err = configureMailboxes();
        if (err)
        {
            err.what();
            return err;
        }

        requestState(State::PRE_OP);
        usleep(10000); //TODO: wait for state

        for (auto& slave : slaves_)
        {
            State state = getCurrentState(slave);
            printf("Slave %d state is %s - %x\n", slave.address, toString(state), state);
        }

/*
        printf("\n\n try to read a SDO on one slave\n");
        char buffer[512];
        int32_t size = 512;
        readSDO(1, 0x1600, 0, true, buffer, &size);
        printf("read %d\n", size);
*/
        err = createMapping(nullptr);

        requestState(State::SAFE_OP);
        usleep(10000); //TODO: wait for state

        for (auto& slave : slaves_)
        {
            State state = getCurrentState(slave);
            printf("Slave %d state is %s - %x\n", slave.address, toString(state), state);
        }

        err += EERROR("Not implemented");
        return err;
    }


    Error Bus::requestState(State request)
    {
        uint16_t param = request | State::ACK;
        uint16_t wkc = broadcastWrite(reg::AL_CONTROL, &param, sizeof(param));
        if (wkc != slaves_.size())
        {
            printf("aie %d %d\n", wkc, slaves_.size());
            return EERROR("failed to request state");
        }

        return ESUCCESS;
    }


    State Bus::getCurrentState(Slave const& slave)
    {
        frames_[0].addDatagram(0x55, Command::FPRD, createAddress(slave.address, reg::AL_STATUS), nullptr, 2);
        Error err = frames_[0].writeThenRead(socket_);
        if (err)
        {
            err.what();
            return State::INVALID;
        }

        auto [header, data, wkc] = frames_[0].nextDatagram();
        printf("--> data %x\n", data[0]);
        return State(data[0] & 0xF);
    }


    Error Bus::resetSlaves()
    {
        // buffer to reset them all
        uint8_t param[256];
        std::memset(param, 0, sizeof(param));

        // Set port to auto mode
        broadcastWrite(reg::ESC_DL_PORT,        param, 1);

        // Reset slaves registers
        broadcastWrite(reg::RX_ERROR,           param, 8);
        broadcastWrite(reg::FMMU,               param, 256);
        broadcastWrite(reg::SYNC_MANAGER,       param, 128);
        broadcastWrite(reg::DC_SYSTEM_TIME,     param, 8);
        broadcastWrite(reg::DC_SYNC_ACTIVATION, param, 1);

        uint16_t dc_param = 0x1000; // reset value
        broadcastWrite(reg::DC_SPEED_CNT_START, &dc_param, sizeof(dc_param));

        dc_param = 0x0c00;          // reset value
        broadcastWrite(reg::DC_TIME_FILTER, &dc_param, sizeof(dc_param));

        // Request INIT state
        Error err = requestState(State::INIT);
        if (err)
        {
            err += EERROR("");
            return err;
        }

        // eeprom to master
        broadcastWrite(reg::EEPROM_CONFIG, param, 2);

        return ESUCCESS;
    }


    Error Bus::configureMailboxes()
    {
        for (auto& slave : slaves_)
        {
            if (slave.supported_mailbox)
            {
                // 0 is mailbox out, 1 is mailbox in - cf. default EtherCAT configuration if slave support a mailbox
                // NOTE: mailbox out -> master to slave - mailbox in -> slave to master
                SyncManager SM[2];
                SM[0].start_address = slave.mailbox.recv_offset;
                SM[0].length        = slave.mailbox.recv_size;
                SM[0].control       = 0x26; // 1 buffer - write access - PDI IRQ ON
                SM[0].status        = 0x00; // RO register
                SM[0].activate      = 0x01; // Sync Manager enable
                SM[0].pdi_control   = 0x00; // RO register
                SM[1].start_address = slave.mailbox.send_offset;
                SM[1].length        = slave.mailbox.send_size;
                SM[1].control       = 0x22; // 1 buffer - read access - PDI IRQ ON
                SM[1].status        = 0x00; // RO register
                SM[1].activate      = 0x01; // Sync Manager enable
                SM[1].pdi_control   = 0x00; // RO register

                addDatagram(Command::FPWR, createAddress(slave.address, reg::SYNC_MANAGER), SM);
            }
        }

        Error err = processFrames();
        if (err)
        {
            return err;
        }

        for (auto& slave : slaves_)
        {
            if (not slave.supported_mailbox)
            {
                continue;
            }

            auto [header, _, wkc] = nextDatagram<uint8_t>();
            if (wkc != 1)
            {
                err += EERROR("one slave didn't answer");
            }
        }

        return err;
    }


    Error Bus::detectMapping()
    {
        // Determines PI sizes for each slave
        for (auto& slave : slaves_)
        {
            if (slave.supported_mailbox & eeprom::MailboxProtocol::CoE)
            {
                // Slave support CAN over EtherCAT -> use mailbox/SDO to get mapping size
                uint8_t sm[64];
                int32_t sm_size = sizeof(sm);
                readSDO(slave, CoE::SM_COM_TYPE, 1, true, sm, &sm_size);

                for (int i = 0; i < sm_size; ++i)
                {
                    if (sm[i] > 2) // <=> SyncManagerType::Input or Output
                    {
                        Slave::PIMapping mapping;
                        mapping.sync_manager = i;
                        mapping.type = sm[i];
                        mapping.size = 0;

                        uint16_t mapped_index[64];
                        int32_t map_size = sizeof(mapped_index);
                        readSDO(slave, CoE::SM_CHANNEL + i, 1, true, mapped_index, &map_size);

                        for (int32_t j = 0; j < (map_size / 2); ++j)
                        {
                            uint8_t object[64];
                            int32_t object_size = sizeof(object);
                            readSDO(slave, mapped_index[j], 1, true, object, &object_size);

                            for (int32_t k = 0; k < object_size; k += 4)
                            {
                                mapping.size += object[k];
                            }
                        }

                        slave.mapping.push_back(mapping);
                    }
                }
            }
            else
            {
                // unsupported mailbox: use SII to get the mapping size
                Slave::PIMapping mapping;

                mapping.sync_manager = 0;
                mapping.size = 0;
                mapping.type = SyncManagerType::Output;
                for (auto const& pdo : slave.sii.RxPDO)
                {
                    mapping.size += pdo->bitlen;
                }
                slave.mapping.push_back(mapping);

                mapping.sync_manager = 1;
                mapping.size = 0;
                mapping.type = SyncManagerType::Input;
                for (auto const& pdo : slave.sii.TxPDO)
                {
                    mapping.size += pdo->bitlen;
                }
                slave.mapping.push_back(mapping);
            }
        }

        // Compute offset - overlap input and output
        // TODO: doesn't work if two SM are in used for the same direction
        int32_t offset = 0;
        for (auto& slave : slaves_)
        {
            for (auto& mapping : slave.mapping)
            {
                // compute byte size from bit size, round up
                mapping.bsize = mapping.size / 8;
                if (mapping.size % 8)
                {
                    mapping.bsize += 1;
                }

                mapping.offset = offset;
            }

            // get the biggest one.
            auto biggest_mapping = std::max_element(slave.mapping.begin(), slave.mapping.end(),
                [](Slave::PIMapping const& lhs, Slave::PIMapping const& rhs)
                {
                    return lhs.bsize < rhs.bsize;
                });
            int32_t size = biggest_mapping->bsize;

            offset += size;
        }

        return ESUCCESS;
    }


    Error Bus::createMapping(uint8_t* iomap)
    {
        // First we need to know:
        // - how many bits to map per slave
        // - which SM to use
        // - logical offset in the frame
        detectMapping();

        uint16_t frame_sent = 0;

        // Program Sync Managers and FMMUs
        for (auto& slave : slaves_)
        {
            for (auto& mapping : slave.mapping)
            {
                uint16_t physical_address = slave.sii.syncManagers_[mapping.sync_manager]->start_adress;

                SyncManager sm;
                FMMU fmmu;
                std::memset(&sm,   0, sizeof(SyncManager));
                std::memset(&fmmu, 0, sizeof(FMMU));

                uint16_t targeted_fmmu = reg::FMMU; // FMMU0 - outputs
                sm.control = 0x64;                  // 3 buffers - write access - PDI IRQ ON - wdg ON
                fmmu.type  = 2;                     // write access
                if (mapping.type == SyncManagerType::Input)
                {
                    sm.control = 0x20;              // 3 buffers - read acces - PDI IRQ ON
                    fmmu.type  = 1;                 // read access
                    targeted_fmmu += 0x10;          // FMMU1 - inputs (slave to master)
                }
                sm.start_address = physical_address;
                sm.length        = mapping.bsize;
                sm.status        = 0x00; // RO register
                sm.activate      = 0x01; // Sync Manager enable
                sm.pdi_control   = 0x00; // RO register

                addDatagram(Command::FPWR, createAddress(slave.address, reg::SYNC_MANAGER + mapping.sync_manager * 8), sm);
                frame_sent++;

                fmmu.logical_address    = mapping.offset;
                fmmu.length             = mapping.bsize;
                fmmu.logical_start_bit  = 0;   // we map every bits
                fmmu.logical_stop_bit   = 0x7; // we map every bits
                fmmu.physical_address   = physical_address;
                fmmu.physical_start_bit = 0;
                fmmu.activate           = 1;

                addDatagram(Command::FPWR, createAddress(slave.address, targeted_fmmu), fmmu);
                frame_sent++;
            }
        }

        Error err = processFrames();
        if (err)
        {
            err.what();
            return err;
        }

        for (int32_t i = 0; i < frame_sent; ++i)
        {
            auto [h, _, wkc] = nextDatagram<uint8_t>();
            if (wkc != 1)
            {
                err += EERROR("wrong wkc!");
            }
        }

        return EERROR("not implemented");
    }



    bool Bus::areEepromReady()
    {
        bool ready = false;
        for (int i = 0; (i < 10) and not ready; ++i)
        {
            usleep(200);
            for (auto& slave : slaves_)
            {
                addDatagram(Command::FPRD, createAddress(slave.address, reg::EEPROM_CONTROL), nullptr, 2);
            }
            Error err = processFrames();
            if (err)
            {
                err.what();
                return false;
            }

            DatagramHeader const* header;
            bool ready = true;
            do
            {
                auto [h, answer, wkc] = nextDatagram<uint16_t>();
                header = h;
                if (wkc != 1)
                {
                    Error err = EERROR("no answer!");
                    err.what();
                }
                if (*answer & 0x8000)
                {
                    ready = false;
                    for (auto& frame : frames_)
                    {
                        frame.clear();
                    }
                    break;
                }
            } while (header->multiple);

            if (ready)
            {
                return true;
            }
        }

        return false;
    }


    Error Bus::readEeprom(uint16_t address, std::vector<Slave*> const& slaves, std::function<void(Slave&, uint32_t word)> apply)
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
        req = { eeprom::Command::READ, address, 0 };
        uint16_t wkc = broadcastWrite(reg::EEPROM_CONTROL, &req, sizeof(req));
        if (wkc != slaves_.size())
        {
            return EERROR("wrong slave number");

        }

        // wait for all eeprom to be ready
        if (not areEepromReady())
        {
            return EERROR("eeprom not ready - timeout");
        }

        // Read result
        for (auto& slave : slaves)
        {
            addDatagram(Command::FPRD, createAddress(slave->address, reg::EEPROM_DATA), nullptr, 4);
        }
        Error err = processFrames();
        if (err)
        {
            return err;
        }

        // Extract result and store it
        for (auto& slave : slaves)
        {
            auto [header, answer, wkc] = nextDatagram<uint32_t>();
            if (wkc != 1)
            {
                Error err = EERROR("no answer!");
                err.what();
            }
            apply(*slave, *answer);
        }

        return ESUCCESS;
    }

    Error Bus::fetchEeprom()
    {
        std::vector<Slave*> slaves;
        for (auto& slave : slaves_)
        {
            slaves.push_back(&slave);
        }

        // General slave info
        readEeprom(eeprom::VENDOR_ID,       slaves, [](Slave& s, uint32_t word) { s.vendor_id       = word; } );
        readEeprom(eeprom::PRODUCT_CODE,    slaves, [](Slave& s, uint32_t word) { s.product_code    = word; } );
        readEeprom(eeprom::REVISION_NUMBER, slaves, [](Slave& s, uint32_t word) { s.revision_number = word; } );
        readEeprom(eeprom::SERIAL_NUMBER,   slaves, [](Slave& s, uint32_t word) { s.serial_number   = word; } );

        // Mailbox info
        readEeprom(eeprom::STANDARD_MAILBOX + eeprom::RECV_MBO_OFFSET, slaves,
        [](Slave& s, uint32_t word) { s.mailbox.recv_offset = word; s.mailbox.recv_size = word >> 16; } );
        readEeprom(eeprom::STANDARD_MAILBOX + eeprom::SEND_MBO_OFFSET, slaves,
        [](Slave& s, uint32_t word) { s.mailbox.send_offset = word; s.mailbox.send_size = word >> 16; } );

        readEeprom(eeprom::MAILBOX_PROTOCOL, slaves,
        [](Slave& s, uint32_t word) { s.supported_mailbox = static_cast<eeprom::MailboxProtocol>(word); });

        readEeprom(eeprom::EEPROM_SIZE, slaves,
        [](Slave& s, uint32_t word)
        {
            s.eeprom_size = (word & 0xFF) + 1;  // 0 means 1024 bits
            s.eeprom_size *= 128;               // Kibit to bytes
            s.eeprom_version = word >> 16;
        });

        // Get SII section
        int32_t pos = 0;
        while (not slaves.empty())
        {
            readEeprom(eeprom::START_CATEGORY + pos, slaves,
            [](Slave& s, uint32_t word)
            {
                s.sii.buffer.push_back(word);
            });

            pos += 2;

            slaves.erase(std::remove_if(slaves.begin(), slaves.end(),
            [](Slave* s)
            {
                return ((s->sii.buffer.back() >> 16) == eeprom::Category::End);
            }),
            slaves.end());
        }

        // Parse SII
        for (auto& slave : slaves_)
        {
            slave.parseSII();
        }

        printSlavesInfo();

        return ESUCCESS;
    }


    void Bus::printSlavesInfo()
    {
        for (auto const& slave : slaves_)
        {
            slave.printInfo();
            printf("\n");
        }
    }


    void Bus::checkMailboxes()
    {
        for (auto& slave : slaves_)
        {
            if (slave.supported_mailbox == 0)
            {
                continue;
            }
            addDatagram(Command::FPRD, createAddress(slave.address, reg::SYNC_MANAGER_0 + reg::SM_STATS), nullptr, 1);
            addDatagram(Command::FPRD, createAddress(slave.address, reg::SYNC_MANAGER_1 + reg::SM_STATS), nullptr, 1);
        }

        Error err = processFrames();
        if (err)
        {
            err.what();
            return;
        }

        for (auto& slave : slaves_)
        {
            if (slave.supported_mailbox == 0)
            {
                continue;
            }

            auto isFull = [this](bool stable_value)
            {
                auto [header, state, wkc] = nextDatagram<uint8_t>();
                if (wkc != 1)
                {
                    EERROR("error while reading mailboxes state").what();
                    return stable_value;
                }
                return ((*state & 0x08) == 0x08);
            };
            slave.mailbox.can_write = not isFull(true);
            slave.mailbox.can_read  = isFull(false);
        }
    }


    void Bus::readSDO(Slave& slave, uint16_t index, uint8_t subindex, bool CA, void* data, int32_t* data_size)
    {
        auto& mailbox = slave.mailbox;

        {
            uint8_t buffer[256]; // taille au pif
            mailbox::Header* header = reinterpret_cast<mailbox::Header*>(buffer);
            mailbox::ServiceData* coe = reinterpret_cast<mailbox::ServiceData*>(buffer + sizeof(mailbox::Header));

            header->len = 10;
            header->address = 0;  // master
            header->priority = 0; // unused
            header->channel  = 0;
            header->type     = mailbox::Type::CoE;

            // compute new counter - used as session handle
            uint8_t counter = slave.mailbox.counter++;
            if (counter > 7)
            {
                counter = 1;
            }
            slave.mailbox.counter = 1;

            coe->number  = counter;
            coe->service = CoE::Service::SDO_REQUEST;
            coe->complete_access = CA;
            coe->command = CoE::SDO::request::UPLOAD;
            coe->block_size     = 0;
            coe->transfer_type  = 0;
            coe->size_indicator = 0;
            coe->index = index;
            coe->subindex = subindex;

            addDatagram(Command::FPWR, createAddress(slave.address, mailbox.recv_offset), buffer, mailbox.recv_size);
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
                return;
            }
        }

        for (int i = 0; i < 10; ++i)
        {
            checkMailboxes();
            if (slave.mailbox.can_read)
            {
                break;
            }
            usleep(200);
        }

        if (not slave.mailbox.can_read)
        {
            printf("TIMEOUT !\n");
            return;
        }

        {
            addDatagram(Command::FPRD, createAddress(slave.address, mailbox.send_offset), nullptr, mailbox.send_size);
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

            if (coe->command == CoE::SDO::request::ABORT)
            {
                printf("Abort requested!\n");
                return;
            }

            if (coe->service != CoE::Service::SDO_RESPONSE)
            {
                printf("OK guy, this one answer, but miss the point\n");
                return;
            }

            uint8_t const* payload = buffer + sizeof(mailbox::Header) + sizeof(mailbox::ServiceData);
            if (coe->transfer_type == 1)
            {
                // expedited transfer
                int32_t size = 4 - coe->block_size;
                if(*data_size < size)
                {
                    printf("Really? Not enough size in client buffer?\n");
                    return;
                }
                std::memcpy(data, payload, size);
                *data_size = size;
                return;
            }

            // standard transfer
            uint32_t size = *reinterpret_cast<uint32_t const*>(payload);
            payload += 4;

            if ((header->len - 10 ) >= size)
            {
                if(*data_size < size)
                {
                    printf("Really? Not enough size in client buffer?\n");
                    return;
                }
                std::memcpy(data, payload, size);
                *data_size = size;
                return;
            }

            printf("Segmented transfer - sorry I dunno how to do it\n");
        }
    }
}
