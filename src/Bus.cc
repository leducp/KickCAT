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
        frames_[0].writeThenRead(socket_);

        auto [header, _, wkc] = frames_[0].nextDatagram();
        ++idx_; // one more frame sent
        return wkc;
    }


    uint16_t Bus::broadcastWrite(uint16_t ADO, void const* data, uint16_t data_size)
    {
        frames_[0].addDatagram(idx_, Command::BWR, createAddress(0, ADO), data, data_size);
        frames_[0].writeThenRead(socket_);

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


    void Bus::processFrames()
    {
        current_frame_ = 0; // reset frame position

        for (auto& frame : frames_)
        {
            if (frame.datagramCounter() == 0)
            {
                break;
            }
            frame.writeThenRead(socket_);
        }
    }


    void Bus::detectSlaves()
    {
        // we dont really care about the type, we just want a working counter to detect the number of slaves
        uint16_t wkc = broadcastRead(reg::TYPE, 1);
        if (wkc == 0)
        {
            THROW_ERROR("Invalid working counter");
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
    }


    void Bus::init()
    {
        detectSlaves();
        resetSlaves();
        setAddresses();

        requestState(State::INIT);
        waitForState(State::INIT, 5000ms);

        fetchEeprom();
        configureMailboxes();

        requestState(State::PRE_OP);
        waitForState(State::PRE_OP, 3000ms);

        // clear mailboxes
        processMessages();
    }


    void Bus::requestState(State request)
    {
        uint16_t param = request | State::ACK;
        uint16_t wkc = broadcastWrite(reg::AL_CONTROL, &param, sizeof(param));
        if (wkc != slaves_.size())
        {
            THROW_ERROR("Invalid working counter");
        }
    }


    State Bus::getCurrentState(Slave const& slave)
    {
        addDatagram(Command::FPRD, createAddress(slave.address, reg::AL_STATUS), nullptr, 2);
        try
        {
            processFrames();

            auto [header, data, wkc] = nextDatagram<uint8_t>();
            uint8_t state = data[0];

            // error indicator flag set: check status code
            if (state & 0x10)
            {
                addDatagram(Command::FPRD, createAddress(slave.address, reg::AL_STATUS_CODE), nullptr, 2);
                try
                {
                    processFrames();
                }
                catch (...)
                {
                    return State::INVALID;
                }

                auto [h, error, w] = nextDatagram<uint16_t>();
                if (*error == 0)
                {
                    // no real error -> filter the flag
                    return State(state & 0xF);
                }
            }
            return State(state);
        }
        catch(...)
        {
            return State::INVALID;
        }
    }


    void Bus::waitForState(State request, nanoseconds timeout)
    {
        nanoseconds now = since_epoch();

        while (true)
        {
            bool is_state_reached = true;
            for (auto& slave : slaves_)
            {
                State state = getCurrentState(slave);
                if (state != request)
                {
                    is_state_reached = false;
                    break;
                }
            }

            if (is_state_reached)
            {
                return;
            }

            if (elapsed_time(now) > timeout)
            {
                THROW_ERROR("Timeout");
            }

            sleep(1ms);
        }
    }


    void Bus::resetSlaves()
    {
        // buffer to reset them all
        uint8_t param[256];
        std::memset(param, 0, sizeof(param));

        // Set port to auto mode
        broadcastWrite(reg::ESC_DL_PORT,        param, 1);

        // Reset slaves registers
        clearErrorCounters();
        broadcastWrite(reg::FMMU,               param, 256);
        broadcastWrite(reg::SYNC_MANAGER,       param, 128);
        broadcastWrite(reg::DC_SYSTEM_TIME,     param, 8);
        broadcastWrite(reg::DC_SYNC_ACTIVATION, param, 1);

        uint16_t dc_param = 0x1000; // reset value
        broadcastWrite(reg::DC_SPEED_CNT_START, &dc_param, sizeof(dc_param));

        dc_param = 0x0c00;          // reset value
        broadcastWrite(reg::DC_TIME_FILTER, &dc_param, sizeof(dc_param));

        // eeprom to master
        broadcastWrite(reg::EEPROM_CONFIG, param, 2);
    }


    void Bus::setAddresses()
    {
        for (int i = 0; i < slaves_.size(); ++i)
        {
            slaves_[i].address = i;
            addDatagram(Command::APWR, createAddress(0 - i, reg::STATION_ADDR), slaves_[i].address);
        }

        processFrames();
    }


    void Bus::configureMailboxes()
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

        processFrames();
        for (auto& slave : slaves_)
        {
            if (not slave.supported_mailbox)
            {
                continue;
            }

            auto [header, _, wkc] = nextDatagram<uint8_t>();
            if (wkc != 1)
            {
                THROW_ERROR("Invalid working counter");
            }
        }
    }


    void Bus::detectMapping()
    {
        // helper: compute byte size from bit size, round up
        auto bits_to_bytes = [](int32_t bits) -> int32_t
        {
            int32_t bytes = bits / 8;
            if (bits % 8)
            {
                bytes += 1;
            }
            return bytes;
        };

        // Determines PI sizes for each slave
        for (auto& slave : slaves_)
        {
            if (slave.supported_mailbox & eeprom::MailboxProtocol::CoE)
            {
                // Slave support CAN over EtherCAT -> use mailbox/SDO to get mapping size
                uint8_t sm[64];
                uint32_t sm_size = sizeof(sm);
                readSDO(slave, CoE::SM_COM_TYPE, 1, true, sm, &sm_size);

                for (int i = 0; i < sm_size; ++i)
                {
                    //TODO we support only one input and one output per slave for now
                    if (sm[i] <= 2) // mailboxes
                    {
                        continue;
                    }

                    Slave::PIMapping* mapping = &slave.input;
                    if (sm[i] == SyncManagerType::Output)
                    {
                        mapping = &slave.output;
                    }
                    mapping->sync_manager = i;
                    mapping->size = 0;

                    uint16_t mapped_index[64];
                    uint32_t map_size = sizeof(mapped_index);
                    readSDO(slave, CoE::SM_CHANNEL + i, 1, true, mapped_index, &map_size);

                    for (int32_t j = 0; j < (map_size / 2); ++j)
                    {
                        uint8_t object[64];
                        uint32_t object_size = sizeof(object);
                        readSDO(slave, mapped_index[j], 1, true, object, &object_size);

                        for (int32_t k = 0; k < object_size; k += 4)
                        {
                            mapping->size += object[k];
                        }
                    }

                    mapping->bsize = bits_to_bytes(mapping->size);
                }
            }
            else
            {
                // unsupported mailbox: use SII to get the mapping size
                Slave::PIMapping* mapping = &slave.output;
                mapping->sync_manager = 0;
                mapping->size = 0;
                for (auto const& pdo : slave.sii.RxPDO)
                {
                    mapping->size += pdo->bitlen;
                }
                mapping->bsize = bits_to_bytes(mapping->size);

                mapping = &slave.input;
                mapping->sync_manager = 1;
                mapping->size = 0;
                for (auto const& pdo : slave.sii.TxPDO)
                {
                    mapping->size += pdo->bitlen;
                }
                mapping->bsize = bits_to_bytes(mapping->size);
            }
        }
    }


    void Bus::createMapping(uint8_t* iomap)
    {
        // First we need to know:
        // - how many bits to map per slave
        // - which SM to use
        // - logical offset in the frame
        detectMapping();

        // Second step: create 'block I/O' lists for read and write op
        // Note A: offset computing will overlap input and output in the frame (better density and compatibility, more works for master)
        // Note B: a frame cannot handle more than 1486 bytes
        pi_frames_.resize(1);
        pi_frames_[0].address = 0;
        uint32_t address = 0;
        for (auto& slave : slaves_)
        {
            // get the biggest one.
            int32_t size = std::max(slave.input.bsize, slave.output.bsize);
            if ((address + size) > (pi_frames_.size() * MAX_ETHERCAT_PAYLOAD_SIZE)) // do we overflow current frame ?
            {
                pi_frames_.back().size = address - pi_frames_.back().address; // frame size = current address - frame address

                // current size will overflow the frame at the current offset: set in on the next frame
                address = pi_frames_.size() * MAX_ETHERCAT_PAYLOAD_SIZE;
                pi_frames_.push_back({address, 0});
            }

            // create block IO entries
            pi_frames_.back().inputs.push_back ({nullptr, address - pi_frames_.back().address, slave.input.bsize,  &slave});
            pi_frames_.back().outputs.push_back({nullptr, address - pi_frames_.back().address, slave.output.bsize, &slave});

            // save mapping offset (need to configure slave FMMU)
            slave.input.address  = address;
            slave.output.address = address;

            // update offset
            address += size;
        }

        // update last frame size
        pi_frames_.back().size = address - pi_frames_.back().address;

        // Third step: associate client buffer address to block IO and slaves
        // Note: inputs are mapped first, outputs second
        uint8_t* pos = iomap;
        for (auto& frame : pi_frames_)
        {
            for (auto& bio : frame.inputs)
            {
                bio.iomap = pos;
                bio.slave->input.data = pos;
                pos += bio.size;
            }
        }
        for (auto& frame : pi_frames_)
        {
            for (auto& bio : frame.outputs)
            {
                bio.iomap = pos;
                bio.slave->output.data = pos;
                pos += bio.size;
            }
        }

        // Fourth step: program FMMUs and SyncManagers
        configureFMMUs();
    }


    void Bus::processDataRead()
    {
        for (auto const& frame : pi_frames_)
        {
            addDatagram(Command::LRD, frame.address, nullptr, frame.size);
        }

        processFrames();

        for (auto const& frame : pi_frames_)
        {
            auto [header, data, wkc] = nextDatagram<uint8_t>();

            if (wkc != frame.inputs.size())
            {
                THROW_ERROR("Invalid working counter");
            }

            for (auto& input : frame.inputs)
            {
                std::memcpy(input.iomap, data + input.offset, input.size);
            }
        }
    }


    void Bus::processDataWrite()
    {
        for (auto const& frame : pi_frames_)
        {
            uint8_t buffer[MAX_ETHERCAT_PAYLOAD_SIZE];
            for (auto const& output : frame.outputs)
            {
                std::memcpy(buffer + output.offset, output.iomap, output.size);
            }

            addDatagram(Command::LWR, frame.address, buffer, frame.size);
        }

        processFrames();

        for (auto const& frame : pi_frames_)
        {
            auto [header, data, wkc] = nextDatagram<uint8_t>();

            if (wkc != frame.outputs.size())
            {
                THROW_ERROR("Invalid working counter");
            }
        }
    }


    void Bus::configureFMMUs()
    {
        auto prepareDatagrams = [this](Slave& slave, Slave::PIMapping& mapping, SyncManagerType type)
        {
            if (mapping.bsize == 0)
            {
                // there is nothing to do for this mapping
                return 0;
            }

            uint16_t physical_address = slave.sii.syncManagers_[mapping.sync_manager]->start_adress;

            SyncManager sm;
            FMMU fmmu;
            std::memset(&sm,   0, sizeof(SyncManager));
            std::memset(&fmmu, 0, sizeof(FMMU));

            uint16_t targeted_fmmu = reg::FMMU; // FMMU0 - outputs
            sm.control = 0x64;                  // 3 buffers - write access - PDI IRQ ON - wdg ON
            fmmu.type  = 2;                     // write access
            if (type == SyncManagerType::Input)
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

            fmmu.logical_address    = mapping.address;
            fmmu.length             = mapping.bsize;
            fmmu.logical_start_bit  = 0;   // we map every bits
            fmmu.logical_stop_bit   = 0x7; // we map every bits
            fmmu.physical_address   = physical_address;
            fmmu.physical_start_bit = 0;
            fmmu.activate           = 1;
            addDatagram(Command::FPWR, createAddress(slave.address, targeted_fmmu), fmmu);
            //printf("slave %04x - size %d - ladd 0x%04x - paddr 0x%04x\n", slave.address, mapping.bsize, mapping.address, physical_address);

            return 2; // number of datagrams
        };

        uint16_t frame_sent = 0;
        for (auto& slave : slaves_)
        {
            frame_sent += prepareDatagrams(slave, slave.input,  SyncManagerType::Input);
            frame_sent += prepareDatagrams(slave, slave.output, SyncManagerType::Output);
        }

        processFrames();
        for (int32_t i = 0; i < frame_sent; ++i)
        {
            auto [h, _, wkc] = nextDatagram<uint8_t>();
            if (wkc != 1)
            {
                THROW_ERROR("Invalid working counter");
            }
        }
    }


    bool Bus::areEepromReady()
    {
        bool ready = false;
        for (int i = 0; (i < 10) and not ready; ++i)
        {
            sleep(200us);
            for (auto& slave : slaves_)
            {
                addDatagram(Command::FPRD, createAddress(slave.address, reg::EEPROM_CONTROL), nullptr, 2);
            }

            try
            {
                processFrames();
            }
            catch (...)
            {
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
                    return false;
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


    void Bus::readEeprom(uint16_t address, std::vector<Slave*> const& slaves, std::function<void(Slave&, uint32_t word)> apply)
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
            THROW_ERROR("Invalid working counter");
        }

        // wait for all eeprom to be ready
        if (not areEepromReady())
        {
            THROW_ERROR("Timeout");
        }

        // Read result
        for (auto& slave : slaves)
        {
            addDatagram(Command::FPRD, createAddress(slave->address, reg::EEPROM_DATA), nullptr, 4);
        }
        processFrames();

        // Extract result and store it
        for (auto& slave : slaves)
        {
            auto [header, answer, wkc] = nextDatagram<uint32_t>();
            if (wkc != 1)
            {
                THROW_ERROR("Invalid working counter");
            }
            apply(*slave, *answer);
        }
    }


    void Bus::fetchEeprom()
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

        processFrames();
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
                    return stable_value;
                }
                return ((*state & 0x08) == 0x08);
            };
            slave.mailbox.can_write = not isFull(true);
            slave.mailbox.can_read  = isFull(false);
        }
    }


    bool Bus::waitForMessage(Slave& slave, nanoseconds timeout)
    {
        nanoseconds now = since_epoch();
        while (true)
        {
            checkMailboxes();
            if (slave.mailbox.can_read)
            {
                return true;
            }
            sleep(200us);

            if (elapsed_time(now) > timeout)
            {
                return false;
            }
        }
    }

    void Bus::clearErrorCounters()
    {
        uint16_t clear_param[20]; // Note: value is not taken into acocunt by the slave and result will always be zero
        uint16_t wkc = broadcastWrite(reg::ERROR_COUNTERS, clear_param, 20);
        if (wkc != slaves_.size())
        {
            THROW_ERROR("Invalid working counter");
        }
    }

    void Bus::refreshErrorCounters()
    {
        for (auto& slave : slaves_)
        {
            addDatagram(Command::FPRD, createAddress(slave.address, reg::ERROR_COUNTERS), slave.error_counters);
        }

        processFrames();
        for (auto& slave : slaves_)
        {
            auto [h, answer, wkc] = nextDatagram<ErrorCounters>();
            if (wkc != 1)
            {
                THROW_ERROR("Invalid working counter");
            }

            slave.error_counters = *answer;
        }
    }

    void Bus::processMessages()
    {
        if (check_loop_)
        {
            checkMailboxes();
            check_loop_ = false;
            return;
        }
        check_loop_ = true;

        int32_t datagram_sent = 0;
        for (auto& slave : slaves_)
        {
            if ((slave.mailbox.can_write) and (not slave.mailbox.to_send.empty()))
            {
                // send one waiting message
                auto message = slave.mailbox.to_send.front();
                slave.mailbox.to_send.pop();
                addDatagram(Command::FPWR, createAddress(slave.address, slave.mailbox.recv_offset), message->data().data(), message->data().size());
                datagram_sent++;

                // add message to processing queue if needed
                if (message->status() == MessageStatus::RUNNING)
                {
                    slave.mailbox.to_process.push_back(message);
                }
            }

            if (slave.mailbox.can_read)
            {
                // retrieve waiting message
                addDatagram(Command::FPRD, createAddress(slave.address, slave.mailbox.send_offset), nullptr, slave.mailbox.send_size);
                datagram_sent++;
            }
        }

        processFrames();
        for (int32_t i = 0; i < datagram_sent; ++i)
        {
            auto [h, data, wkc] = nextDatagram<uint8_t>();
            if (wkc != 1)
            {
                THROW_ERROR("Invalid working counter");
            }

            if (h->command == Command::FPRD)
            {
                int32_t slave_index = (h->address & 0xFFFF);
                auto& slave = slaves_.at(slave_index);

                if (not slave.mailbox.receive(data))
                {
                    printf("Slave %d: receive a message but didn't process it\n", slave_index);
                }
            }
        }
    }
}