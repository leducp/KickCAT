#include <cstring>

#include "Bus.h"
#include "AbstractSocket.h"


namespace kickcat
{
    Bus::Bus(std::shared_ptr<AbstractSocket> socket)
        : link_(socket)
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
        Frame frame;
        frame.addDatagram(0, Command::BRD, createAddress(0, ADO), nullptr, data_size);
        link_.writeThenRead(frame);

        auto [header, _, wkc] = frame.nextDatagram();
        return wkc;
    }


    uint16_t Bus::broadcastWrite(uint16_t ADO, void const* data, uint16_t data_size)
    {
        Frame frame;
        frame.addDatagram(0, Command::BWR, createAddress(0, ADO), data, data_size);
        link_.writeThenRead(frame);

        auto [header, _, wkc] = frame.nextDatagram();
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
        frame->addDatagram(0, command, address, data, data_size);
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
        auto error_callback = [&](){ THROW_ERROR("init error while cleaning slaves mailboxes"); };
        checkMailboxes(error_callback);
        processMessages(error_callback);
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
        uint8_t state = State::INVALID;

        addDatagram(Command::FPRD, createAddress(slave.address, reg::AL_STATUS), nullptr, 2);
        try
        {
            processFrames();

            auto [header, data, wkc] = nextDatagram<uint8_t>();
            state = data[0];
        }
        catch(...)
        {
            return State::INVALID;
        }

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
            THROW_ERROR_CODE("State transition error", *error);
        }
        return State(state);
    }


    void Bus::waitForState(State request, nanoseconds timeout)
    {
        nanoseconds now = since_epoch();

        while (true)
        {
            sleep(10ms);

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
            if (slave.is_static_mapping)
            {
                slave.input.size  = slave.input.bsize  * 8;
                slave.output.size = slave.output.bsize * 8;
                continue;
            }

            if (slave.supported_mailbox & eeprom::MailboxProtocol::CoE)
            {
                // Slave support CAN over EtherCAT -> use mailbox/SDO to get mapping size
                uint8_t sm[512];
                uint32_t sm_size = sizeof(sm);
                readSDO(slave, CoE::SM_COM_TYPE, 1, Access::EMULATE_COMPLETE, sm, &sm_size);

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

                    uint16_t mapped_index[128];
                    uint32_t map_size = sizeof(mapped_index);
                    readSDO(slave, CoE::SM_CHANNEL + i, 1, Access::EMULATE_COMPLETE, mapped_index, &map_size);

                    for (int32_t j = 0; j < (map_size / 2); ++j)
                    {
                        uint8_t object[512];
                        uint32_t object_size = sizeof(object);
                        readSDO(slave, mapped_index[j], 1, Access::EMULATE_COMPLETE, object, &object_size);

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


    void Bus::processFrames()
    {
        current_frame_ = 0; // reset frame position
        for (auto& frame : frames_)
        {
            if (frame.datagramCounter() == 0)
            {
                break;
            }

            link_.writeThenRead(frame);
        }
    }


    void Bus::sendLogicalRead(std::function<void()> const& error)
    {
        for (auto const& pi_frame : pi_frames_)
        {
            Frame frame;
            frame.addDatagram(0, Command::LRD, pi_frame.address, nullptr, pi_frame.size);

            std::function<bool(Frame&)> callback = [pi_frame](Frame& frame)
            {
                auto [_, data, wkc] = frame.nextDatagram();
                if (wkc != pi_frame.inputs.size())
                {
                    DEBUG_PRINT("Invalid working counter\n");
                    return false;
                }

                for (auto& input : pi_frame.inputs)
                {
                    std::memcpy(input.iomap, data + input.offset, input.size);
                }
                return true;
            };

            link_.addFrame(frame, callback, error);
        }
    }


    void Bus::processDataRead(std::function<void()> const& error)
    {
        sendLogicalRead(error);
        link_.processFrames();
    }


    void Bus::sendLogicalWrite(std::function<void()> const& error)
    {
        for (auto const& pi_frame : pi_frames_)
        {
            uint8_t buffer[MAX_ETHERCAT_PAYLOAD_SIZE];
            for (auto const& output : pi_frame.outputs)
            {
                std::memcpy(buffer + output.offset, output.iomap, output.size);
            }

            Frame frame;
            frame.addDatagram(0, Command::LWR, pi_frame.address, buffer, pi_frame.size);

            std::function<bool(Frame&)> callback = [pi_frame](Frame& frame)
            {
                auto [_, data, wkc] = frame.nextDatagram();

                if (wkc != pi_frame.outputs.size())
                {
                    DEBUG_PRINT("Invalid working counter\n");
                    return false;
                }
                return true;
            };
            link_.addFrame(frame, callback, error);
        }
    }


    void Bus::processDataWrite(std::function<void()> const& error)
    {
        sendLogicalWrite(error);
        link_.processFrames();
    }


    void Bus::sendLogicalReadWrite(std::function<void()> const& error)
    {
        for (auto const& pi_frame : pi_frames_)
        {
            uint8_t buffer[MAX_ETHERCAT_PAYLOAD_SIZE];
            for (auto const& output : pi_frame.outputs)
            {
                std::memcpy(buffer + output.offset, output.iomap, output.size);
            }

            Frame frame;
            frame.addDatagram(0, Command::LRW, pi_frame.address, buffer, pi_frame.size);

            std::function<bool(Frame&)> callback = [pi_frame](Frame& frame)
            {
                auto [_, data, wkc] = frame.nextDatagram();
                if (wkc != pi_frame.inputs.size())
                {
                    DEBUG_PRINT("Invalid working counter\n");
                    return false;
                }

                for (auto& input : pi_frame.inputs)
                {
                    std::memcpy(input.iomap, data + input.offset, input.size);
                }
                return true;
            };
            link_.addFrame(frame, callback, error);
        }
    }

    void Bus::processDataReadWrite(std::function<void()> const& error)
    {
        sendLogicalReadWrite(error);
        link_.processFrames();
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

            // Get SyncManager configuration from SII
            auto& sii_sm = slave.sii.syncManagers_[mapping.sync_manager];

            SyncManager sm;
            FMMU fmmu;
            std::memset(&sm,   0, sizeof(SyncManager));
            std::memset(&fmmu, 0, sizeof(FMMU));

            uint16_t targeted_fmmu = reg::FMMU; // FMMU0 - outputs
            sm.control = sii_sm->control_register;
            fmmu.type  = 2;                     // write access
            if (type == SyncManagerType::Input)
            {
                sm.control = 0x20;              // 3 buffers - read acces - PDI IRQ ON
                fmmu.type  = 1;                 // read access
                targeted_fmmu += 0x10;          // FMMU1 - inputs (slave to master)
            }

            sm.start_address = sii_sm->start_adress;
            sm.length        = mapping.bsize;
            sm.status        = 0x00; // RO register
            sm.activate      = 0x01; // Sync Manager enable
            sm.pdi_control   = 0x00; // RO register
            addDatagram(Command::FPWR, createAddress(slave.address, reg::SYNC_MANAGER + mapping.sync_manager * 8), sm);
            //printf("SM[%d] type %d - start address 0x%04x - length %d - flags: 0x%02x\n", mapping.sync_manager, type, sm.start_address, sm.length, sm.control);

            fmmu.logical_address    = mapping.address;
            fmmu.length             = mapping.bsize;
            fmmu.logical_start_bit  = 0;   // we map every bits
            fmmu.logical_stop_bit   = 0x7; // we map every bits
            fmmu.physical_address   = sii_sm->start_adress;
            fmmu.physical_start_bit = 0;
            fmmu.activate           = 1;
            addDatagram(Command::FPWR, createAddress(slave.address, targeted_fmmu), fmmu);
            //printf("slave %04x - size %d - ladd 0x%04x - paddr 0x%04x\n", slave.address, mapping.bsize, mapping.address, fmmu.physical_address);

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


    void Bus::sendMailboxesChecks(std::function<void()> const& error)
    {
        std::function<bool(Frame&)> callback = [this](Frame& frame)
        {
            auto isFull = [](uint8_t state, uint16_t wkc, bool stable_value)
            {
                if (wkc != 1)
                {
                    DEBUG_PRINT("Invalid working counter\n");
                    return stable_value;
                }
                return ((state & 0x08) == 0x08);
            };

            while (frame.isDatagramAvailable())
            {
                auto [header, state, wkc] = frame.nextDatagram();
                int32_t slave_index = header->address & 0xFFFF;
                int32_t mailbox     = header->address >> 16;
                auto& slave = slaves_.at(slave_index);

                if (mailbox == reg::SYNC_MANAGER_0 + reg::SM_STATS)
                {
                    slave.mailbox.can_write = not isFull(*state, wkc, true);
                }
                else
                {
                    slave.mailbox.can_read  = isFull(*state, wkc, false);
                }
            }
            return true;
        };

        Frame frame;
        for (auto& slave : slaves_)
        {
            if (slave.supported_mailbox == 0)
            {
                continue;
            }
            if (frame.addDatagram (0, Command::FPRD, createAddress(slave.address, reg::SYNC_MANAGER_0 + reg::SM_STATS), nullptr, 1))
            {
                link_.addFrame(frame, callback, error);
            }
            if (frame.addDatagram(0, Command::FPRD, createAddress(slave.address, reg::SYNC_MANAGER_1 + reg::SM_STATS), nullptr, 1))
            {
                link_.addFrame(frame, callback, error);
            }
        }

        if (frame.datagramCounter() != 0)
        {
            link_.addFrame(frame, callback, error);
        }
    }

    void Bus::checkMailboxes(std::function<void()> const& error)
    {
        sendMailboxesChecks(error);
        link_.processFrames();
    }


    void Bus::sendWriteMessages(std::function<void()> const& error)
    {
        std::function<bool(Frame&)> callback = [this](Frame& frame)
        {
            while (frame.isDatagramAvailable())
            {
                auto [_, __, wkc] = frame.nextDatagram();
                if (wkc != 1)
                {
                    DEBUG_PRINT("Invalid working counter\n");
                    return false;
                }
            }
            return true;
        };

        Frame frame;
        for (auto& slave : slaves_)
        {
            if ((slave.mailbox.can_write) and (not slave.mailbox.to_send.empty()))
            {
                // send one waiting message
                auto message = slave.mailbox.to_send.front();
                slave.mailbox.to_send.pop();
                if(frame.addDatagram(0, Command::FPWR, createAddress(slave.address, slave.mailbox.recv_offset), message->data().data(), message->data().size()))
                {
                    link_.addFrame(frame, callback, error);
                }

                // add message to processing queue if needed
                if (message->status() == MessageStatus::RUNNING)
                {
                    slave.mailbox.to_process.push_back(message);
                }
            }
        }

        if (frame.datagramCounter() != 0)
        {
            link_.addFrame(frame, callback, error);
        }
    }

    void Bus::sendReadMessages(std::function<void()> const& error)
    {
        std::function<bool(Frame&)> callback = [this](Frame& frame)
        {
            bool result = true;
            while (frame.isDatagramAvailable())
            {
                auto [header, data, wkc] = frame.nextDatagram();
                int32_t slave_index = header->address & 0xFFFF;
                auto& slave = slaves_.at(slave_index);
                if (wkc != 1)
                {
                    DEBUG_PRINT("Invalid working counter for slave %d\n", slave_index);
                    result = false;
                    continue;
                }

                if (not slave.mailbox.receive(data))
                {
                    DEBUG_PRINT("Slave %d: receive a message but didn't process it\n", slave_index);
                    result = false;
                }
            }
            return result;
        };

        Frame frame;
        for (auto& slave : slaves_)
        {
            if (slave.mailbox.can_read)
            {
                // retrieve waiting message
                if(frame.addDatagram(0, Command::FPRD, createAddress(slave.address, slave.mailbox.send_offset), nullptr, slave.mailbox.send_size))
                {
                    link_.addFrame(frame, callback, error);
                }
            }
        }

        if (frame.datagramCounter() != 0)
        {
            link_.addFrame(frame, callback, error);
        }
    }


    void Bus::processMessages(std::function<void()> const& error)
    {
        sendWriteMessages(error);
        sendReadMessages(error);
        link_.processFrames();
    }


    void Bus::finalizeAwaitingFrames()
    {
        link_.processFrames();
    }


    void Bus::clearErrorCounters()
    {
        uint16_t clear_param[20]; // Note: value is not taken into account by the slave and result will always be zero
        uint16_t wkc = broadcastWrite(reg::ERROR_COUNTERS, clear_param, 20);
        if (wkc != slaves_.size())
        {
            THROW_ERROR("Invalid working counter");
        }
    }

    void Bus::sendrefreshErrorCounters(std::function<void()> const& error)
    {
        std::function<bool(Frame&)> callback = [this](Frame& frame)
        {
            bool result = true;
            while (frame.isDatagramAvailable())
            {
                auto [header, data, wkc] = frame.nextDatagram();
                int32_t slave_index = header->address & 0xFFFF;
                auto& slave = slaves_.at(slave_index);
                if (wkc != 1)
                {
                    DEBUG_PRINT("Invalid working counter for slave %d\n", slave_index);
                    result = false;
                    continue;
                }

                std::memcpy(&slave.error_counters, data, sizeof(ErrorCounters));
            }
            return result;
        };

        Frame frame;
        for (auto& slave : slaves_)
        {
            if (frame.addDatagram(0, Command::FPRD, createAddress(slave.address, reg::ERROR_COUNTERS), nullptr, sizeof(ErrorCounters)))
            {
                link_.addFrame(frame, callback, error);
            }
        }

        if (frame.datagramCounter() != 0)
        {
            link_.addFrame(frame, callback, error);
        }
    }

}
