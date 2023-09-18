#include <cstring>

#include "Bus.h"
#include "AbstractSocket.h"
#include "Prints.h"

namespace kickcat
{
    Bus::Bus(std::shared_ptr<Link> link)
    : link_(link)
    {
    }


    int32_t Bus::detectedSlaves() const
    {
        return static_cast<int32_t>(slaves_.size());
    }


    uint16_t Bus::broadcastRead(uint16_t ADO, uint16_t data_size)
    {
        Frame frame;
        frame.addDatagram(0, Command::BRD, createAddress(0, ADO), nullptr, data_size);
        link_->writeThenRead(frame);
        auto [header, _, wkc] = frame.nextDatagram();
        return wkc;
    }


    uint16_t Bus::broadcastWrite(uint16_t ADO, void const* data, uint16_t data_size)
    {
        Frame frame;
        frame.addDatagram(0, Command::BWR, createAddress(0, ADO), data, data_size);
        link_->writeThenRead(frame);

        auto [header, _, wkc] = frame.nextDatagram();
        return wkc;
    }


    int32_t Bus::detectSlaves()
    {
        // we dont really care about the type, we just want a working counter to detect the number of slaves
        uint16_t wkc = 0;
        wkc = broadcastRead(reg::TYPE, 1);
        slaves_.resize(wkc);
        DEBUG_PRINT("%lu slave detected on the network\n", slaves_.size());
        return detectedSlaves();
    }


    void Bus::enableIRQ(enum EcatEvent event, std::function<void()> callback)
    {
        link_->attachEcatEventCallback(event, callback);

        irq_mask_ |= event;
        uint16_t wkc = broadcastWrite(reg::ECAT_EVENT_MASK, &irq_mask_, 2);
        if (wkc != slaves_.size())
        {
            THROW_ERROR("Invalid working counter");
        }
    }


    void Bus::disableIRQ(enum EcatEvent event)
    {
        link_->attachEcatEventCallback(event, {});

        irq_mask_ &= ~event;
        uint16_t wkc = broadcastWrite(reg::ECAT_EVENT_MASK, &irq_mask_, 2);
        if (wkc != slaves_.size())
        {
            THROW_ERROR("Invalid working counter");
        }
    }


    void Bus::init(nanoseconds watchdogTimePDIO)
    {
        if (detectSlaves() == 0)
        {
            THROW_ERROR("No slave detected");
        }
        resetSlaves(watchdogTimePDIO);
        setAddresses();

        requestState(State::INIT);
        waitForState(State::INIT, 5000ms);

        fetchEeprom();
        configureMailboxes();

        requestState(State::PRE_OP);
        waitForState(State::PRE_OP, 3000ms);

        // clear mailboxes
        auto error_callback = [](DatagramState const& state){ THROW_ERROR_DATAGRAM("init error while cleaning slaves mailboxes", state); };
        checkMailboxes(error_callback);
        processMessages(error_callback);

        // create CoE emergency reception callback
        for (auto& slave : slaves_)
        {
            if (slave.supported_mailbox & eeprom::MailboxProtocol::CoE)
            {
                auto emg = std::make_shared<EmergencyMessage>(slave.mailbox);
                slave.mailbox.to_process.push_back(emg);
            }
        }
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


    void Bus::sendGetALStatus(Slave& slave, std::function<void(DatagramState const&)> const& error)
    {
        slave.al_status = State::INVALID;
        auto process = [&slave](DatagramHeader const*, uint8_t const* data, uint16_t wkc)
        {
            if (wkc != 1)
            {
                return DatagramState::INVALID_WKC;
            }

            slave.al_status = data[0];
            slave.al_status_code = *reinterpret_cast<uint16_t const*>(data + 4);
            return DatagramState::OK;
        };

        link_->addDatagram(Command::FPRD, createAddress(slave.address, reg::AL_STATUS), nullptr, 6, process, error);
    }


    State Bus::getCurrentState(Slave& slave)
    {
        auto error = [](DatagramState const& state)
        {
            DEBUG_PRINT("Error while trying to get slave state (%s).\n", toString(state));
        };

        sendGetALStatus(slave, error);
        link_->processDatagrams();

        // error indicator flag set: check status code
        if (slave.al_status & 0x10)
        {
            if (slave.al_status_code == 0)
            {
                return State(slave.al_status & 0xF);
            }
            THROW_ERROR_CODE("State transition error", slave.al_status_code);
        }
        return State(slave.al_status);
    }


    void Bus::waitForState(State request, nanoseconds timeout, std::function<void()> background_task)
    {
        nanoseconds now = since_epoch();

        while (true)
        {
            background_task();

            sleep(big_wait);

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


    void Bus::resetSlaves(nanoseconds watchdog)
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
        broadcastWrite(reg::ECAT_EVENT_MASK,    param, 2);

        uint16_t dc_param = 0x1000; // reset value
        broadcastWrite(reg::DC_SPEED_CNT_START, &dc_param, sizeof(dc_param));

        dc_param = 0x0c00;          // reset value
        broadcastWrite(reg::DC_TIME_FILTER, &dc_param, sizeof(dc_param));

        // reset ECAT Event registers
        broadcastRead(reg::LATCH_STATUS, 1);
        broadcastRead(reg::ESC_DL_STATUS, 1);
        broadcastRead(reg::AL_STATUS, 1);

        // PDIO watchdogs
        nanoseconds const precision = 100us;
        uint16_t const wdg_divider = computeWatchdogDivider(precision);
        uint16_t const wdg_time = computeWatchdogTime(watchdog, precision);

        broadcastWrite(reg::WDG_DIVIDER,  &wdg_divider, sizeof(wdg_divider));
        broadcastWrite(reg::WDG_TIME_PDI, &wdg_time,    sizeof(wdg_time));
        broadcastWrite(reg::WDG_TIME_PDO, &wdg_time,    sizeof(wdg_time));

        // eeprom to master
        broadcastWrite(reg::EEPROM_CONFIG, param, 2);
    }


    void Bus::setAddresses()
    {
        // Regular processDatagram can't be used here with redundancy to avoid messing the slave address attribution.

        Frame frame;
        auto process = [&]()
        {
            link_->writeThenRead(frame);
            while (frame.isDatagramAvailable())
            {
                auto [header, _, wkc] = frame.nextDatagram();
                if (wkc != 1)
                {
                    THROW_ERROR("Invalid working counter");
                }
            }
            frame.clear();
        };

        for (size_t i = 0; i < slaves_.size(); ++i)
        {
            slaves_[i].address = static_cast<uint16_t>(i + 1001);
            frame.addDatagram(0, Command::APWR, createAddress(0 - static_cast<uint16_t>(i), reg::STATION_ADDR), &slaves_[i].address, sizeof(slaves_[i].address));
            if (frame.isFull())
            {
                process();
            }
        }

        if (frame.datagramCounter() != 0)
        {
            process();
        }
    }


    void Bus::configureMailboxes()
    {
        auto process = [](DatagramHeader const*, uint8_t const*, uint16_t wkc)
        {
            if (wkc != 1)
            {
                return DatagramState::INVALID_WKC;
            }
            return DatagramState::OK;
        };

        auto error = [](DatagramState const&)
        {
            THROW_ERROR("Invalid working counter");
        };

        for (auto& slave : slaves_)
        {
            if (slave.supported_mailbox)
            {
                SyncManager SM[2];
                slave.mailbox.generateSMConfig(SM);
                link_->addDatagram(Command::FPWR, createAddress(slave.address, reg::SYNC_MANAGER), SM, process, error);
            }
        }

        link_->processDatagrams();
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

                for (uint32_t i = 0; i < sm_size; ++i)
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
                    readSDO(slave, CoE::SM_CHANNEL + static_cast<uint16_t>(i), 1, Access::EMULATE_COMPLETE, mapped_index, &map_size);

                    for (uint32_t j = 0; j < (map_size / 2); ++j)
                    {
                        uint8_t object[512];
                        uint32_t object_size = sizeof(object);
                        readSDO(slave, mapped_index[j], 1, Access::EMULATE_COMPLETE, object, &object_size);

                        for (uint32_t k = 0; k < object_size; k += 4)
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
                auto siiMapping = [&](Slave::PIMapping* mapping, std::vector<eeprom::PDOEntry const*>& PDOs, SyncManagerType type)
                {
                    mapping->sync_manager = -1;
                    mapping->size = 0;
                    for (auto const& pdo : PDOs)
                    {
                        mapping->size += pdo->bitlen;
                    }
                    mapping->bsize = bits_to_bytes(mapping->size);
                    for (uint32_t i = 0; i < slave.sii.syncManagers_.size(); ++i)
                    {
                        auto sm = slave.sii.syncManagers_[i];
                        if (sm->type == type)
                        {
                            mapping->sync_manager = i;
                        }
                    }
                    if (mapping->sync_manager == -1 and mapping->size != 0)
                    {
                        THROW_ERROR("Invalid SyncManager configuration");
                    }
                };

                siiMapping(&slave.output, slave.sii.RxPDO, SyncManagerType::Output);
                siiMapping(&slave.input,  slave.sii.TxPDO, SyncManagerType::Input);
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
                address = static_cast<uint32_t>(pi_frames_.size()) * MAX_ETHERCAT_PAYLOAD_SIZE;
                pi_frames_.push_back({address, 0, {}, {}});
            }

            // create block IO entries
            if (slave.input.bsize > 0)
            {
                pi_frames_.back().inputs.push_back ({nullptr, address - pi_frames_.back().address, slave.input.bsize,  &slave});
            }

            if (slave.output.bsize > 0)
            {
                pi_frames_.back().outputs.push_back({nullptr, address - pi_frames_.back().address, slave.output.bsize, &slave});
            }

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


    void Bus::sendLogicalRead(std::function<void(DatagramState const&)> const& error)
    {
        for (auto const& pi_frame : pi_frames_)
        {
            auto process = [pi_frame](DatagramHeader const*, uint8_t const* data, uint16_t wkc)
            {
                if (wkc != pi_frame.inputs.size())
                {
                    DEBUG_PRINT("Invalid working counter: expected %ld, got %d\n", pi_frame.inputs.size(), wkc);
                    return DatagramState::INVALID_WKC;
                }

                for (auto& input : pi_frame.inputs)
                {
                    std::memcpy(input.iomap, data + input.offset, input.size);
                }
                return DatagramState::OK;
            };

            link_->addDatagram(Command::LRD, pi_frame.address, nullptr, static_cast<uint16_t>(pi_frame.size), process, error);
        }
    }


    void Bus::processDataRead(std::function<void(DatagramState const&)> const& error)
    {
        sendLogicalRead(error);
        link_->processDatagrams();
    }


    void Bus::sendLogicalWrite(std::function<void(DatagramState const&)> const& error)
    {
        for (auto const& pi_frame : pi_frames_)
        {
            uint8_t buffer[MAX_ETHERCAT_PAYLOAD_SIZE];
            for (auto const& output : pi_frame.outputs)
            {
                std::memcpy(buffer + output.offset, output.iomap, output.size);
            }

            auto process = [pi_frame](DatagramHeader const*, uint8_t const*, uint16_t wkc)
            {
                if (wkc != pi_frame.outputs.size())
                {
                    DEBUG_PRINT("Invalid working counter: expected %ld, got %d\n", pi_frame.outputs.size(), wkc);
                    return DatagramState::INVALID_WKC;
                }
                return DatagramState::OK;
            };
            link_->addDatagram(Command::LWR, pi_frame.address, buffer, static_cast<uint16_t>(pi_frame.size), process, error);
        }
    }


    void Bus::processDataWrite(std::function<void(DatagramState const&)> const& error)
    {
        sendLogicalWrite(error);
        link_->processDatagrams();
    }


    void Bus::sendLogicalReadWrite(std::function<void(DatagramState const&)> const& error)
    {
        for (auto const& pi_frame : pi_frames_)
        {
            uint8_t buffer[MAX_ETHERCAT_PAYLOAD_SIZE];
            for (auto const& output : pi_frame.outputs)
            {
                std::memcpy(buffer + output.offset, output.iomap, output.size);
            }

            auto process = [pi_frame](DatagramHeader const*, uint8_t const* data, uint16_t wkc)
            {
                uint16_t expected_wkc = static_cast<uint16_t>(pi_frame.inputs.size() + pi_frame.outputs.size() * 2);
                if (wkc != expected_wkc) //TODO: buggy in master?
                {
                    DEBUG_PRINT("Invalid working counter: expected %d, got %d\n", expected_wkc, wkc);
                    return DatagramState::INVALID_WKC;
                }

                for (auto& input : pi_frame.inputs)
                {
                    std::memcpy(input.iomap, data + input.offset, input.size);
                }
                return DatagramState::OK;
            };

            link_->addDatagram(Command::LRW, pi_frame.address, buffer, static_cast<uint16_t>(pi_frame.size), process, error);
        }
    }

    void Bus::processDataReadWrite(std::function<void(DatagramState const&)> const& error)
    {
        sendLogicalReadWrite(error);
        link_->processDatagrams();
    }


    void Bus::configureFMMUs()
    {
        auto prepareDatagrams = [this](Slave& slave, Slave::PIMapping& mapping, SyncManagerType type)
        {

            if (mapping.bsize == 0)
            {
                // there is nothing to do for this mapping
                return;
            }

            auto error = [](DatagramState const&)
            {
                THROW_ERROR("Invalid working counter");
            };

            auto process = [](DatagramHeader const*, uint8_t const*, uint16_t wkc)
            {
                if (wkc != 1)
                {
                    return DatagramState::INVALID_WKC;
                }
                return DatagramState::OK;
            };

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
            sm.length        = static_cast<uint16_t>(mapping.bsize);
            sm.status        = 0x00; // RO register
            sm.activate      = 0x01; // Sync Manager enable
            sm.pdi_control   = 0x00; // RO register
            link_->addDatagram(Command::FPWR,
                              createAddress(slave.address, reg::SYNC_MANAGER + static_cast<uint16_t>(mapping.sync_manager * 8)),
                              sm, process, error);
            DEBUG_PRINT("SM[%d] type %d - start address 0x%04x - length %d - flags: 0x%02x\n", mapping.sync_manager, type, sm.start_address, sm.length, sm.control);

            fmmu.logical_address    = mapping.address;
            fmmu.length             = static_cast<uint16_t>(mapping.bsize);
            fmmu.logical_start_bit  = 0;   // we map every bits
            fmmu.logical_stop_bit   = 0x7; // we map every bits
            fmmu.physical_address   = sii_sm->start_adress;
            fmmu.physical_start_bit = 0;
            fmmu.activate           = 1;
            link_->addDatagram(Command::FPWR, createAddress(slave.address, targeted_fmmu), fmmu, process, error);
            DEBUG_PRINT("slave %04x - size %d - ladd 0x%04x - paddr 0x%04x\n", slave.address, mapping.bsize, mapping.address, fmmu.physical_address);
        };

        for (auto& slave : slaves_)
        {
            prepareDatagrams(slave, slave.input,  SyncManagerType::Input);
            prepareDatagrams(slave, slave.output, SyncManagerType::Output);
        }

        link_->processDatagrams();
    }


    bool Bus::areEepromReady()
    {
        bool ready = true;
        auto process = [&ready](DatagramHeader const*, uint8_t const* data, uint16_t wkc)
        {
            if (wkc != 1)
            {
                return DatagramState::INVALID_WKC;
            }
            uint16_t answer = *reinterpret_cast<uint16_t const*>(data);
            if (answer & 0x8000)
            {
                ready = false;
            }
            return DatagramState::OK;
        };

        auto error = [](DatagramState const&)
        {
            THROW_ERROR("Error while fetching eeprom state");
        };

        for (int i = 0; i < 10; ++i)
        {
            sleep(tiny_wait);
            for (auto& slave : slaves_)
            {
                link_->addDatagram(Command::FPRD, createAddress(slave.address, reg::EEPROM_CONTROL), nullptr, 2, process, error);
            }

            ready = true; // rearm check
            try
            {
                link_->processDatagrams();
            }
            catch (...)
            {
                return false;
            }

            if (ready)
            {
                return ready;
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
        {
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
        }

        // Read result
        auto error = [](DatagramState const& state)
        {
            THROW_ERROR_DATAGRAM("Error while fetching eeprom data", state);
        };

        for (auto& slave : slaves)
        {
            auto process = [this, &slave, &apply](DatagramHeader const*, uint8_t const* data, uint16_t wkc)
            {
                if (wkc != 1)
                {
                    return DatagramState::INVALID_WKC;
                }
                uint32_t answer;
                std::memcpy(&answer, data, sizeof(uint32_t));
                apply(*slave, answer);
                return DatagramState::OK;
            };

            link_->addDatagram(Command::FPRD, createAddress(slave->address, reg::EEPROM_DATA), nullptr, 4, process, error);
        }
        link_->processDatagrams();
    }


    void Bus::fetchEeprom()
    {
        std::vector<Slave*> slaves;
        for (auto& slave : slaves_)
        {
            slaves.push_back(&slave);
        }

        // Get SII
        int32_t pos = 0;
        while (not slaves.empty())
        {
            readEeprom(static_cast<uint16_t>(pos), slaves,
            [](Slave& s, uint32_t word)
            {
                s.sii.buffer.push_back(word);
            });

            pos += 2;

            slaves.erase(std::remove_if(slaves.begin(), slaves.end(),
            [](Slave* s)
            {
                // First section (64 words == 32 double words) may have bytes with the eeprom::Category::End value
                return (((s->sii.buffer.back() >> 16) == eeprom::Category::End) and (s->sii.buffer.size() > 32));
            }),
            slaves.end());
        }

        // Parse SII
        for (auto& slave : slaves_)
        {
            slave.parseSII();
        }
    }


    void Bus::sendMailboxesReadChecks(std::function<void(DatagramState const&)> const& error)
    {
        auto isFull = [](uint8_t state, uint16_t wkc, bool stable_value)
        {
            if (wkc != 1)
            {
                DEBUG_PRINT("Invalid working counter\n");
                return stable_value;
            }
            return ((state & MAILBOX_STATUS) == MAILBOX_STATUS);
        };

        for (auto& slave : slaves_)
        {
            auto process_read = [&slave, isFull](DatagramHeader const*, uint8_t const* state, uint16_t wkc)
            {
                slave.mailbox.can_read = isFull(*state, wkc, false);
                return DatagramState::OK;
            };

            if (slave.supported_mailbox == 0)
            {
                continue;
            }
            link_->addDatagram(Command::FPRD, createAddress(slave.address, reg::SYNC_MANAGER_1 + reg::SM_STATS), nullptr, 1, process_read,  error);
        }
    }

    void Bus::sendMailboxesWriteChecks(std::function<void(DatagramState const&)> const& error)
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

        for (auto& slave : slaves_)
        {
            auto process_write = [&slave, isFull](DatagramHeader const*, uint8_t const* state, uint16_t wkc)
            {
                slave.mailbox.can_write = not isFull(*state, wkc, true);
                return DatagramState::OK;
            };

            if (slave.supported_mailbox == 0)
            {
                continue;
            }
            link_->addDatagram(Command::FPRD, createAddress(slave.address, reg::SYNC_MANAGER_0 + reg::SM_STATS), nullptr, 1, process_write, error);
        }
    }

    void Bus::checkMailboxes(std::function<void(DatagramState const&)> const& error)
    {
        sendMailboxesWriteChecks(error);
        sendMailboxesReadChecks(error);
        link_->processDatagrams();
    }


    void Bus::sendWriteMessages(std::function<void(DatagramState const&)> const& error)
    {
        auto process = [](DatagramHeader const*, uint8_t const*, uint16_t wkc)
        {
            if (wkc != 1)
            {
                DEBUG_PRINT("Invalid working counter\n");
                return DatagramState::INVALID_WKC;
            }
            return DatagramState::OK;
        };

        for (auto& slave : slaves_)
        {
            if ((slave.mailbox.can_write) and (not slave.mailbox.to_send.empty()))
            {
                // send one waiting message
                auto message = slave.mailbox.send();
                link_->addDatagram(Command::FPWR, createAddress(slave.address, slave.mailbox.recv_offset), message->data(),
                                  static_cast<uint16_t>(message->size()), process, error);
            }
        }
    }

    void Bus::sendReadMessages(std::function<void(DatagramState const&)> const& error)
    {
        Frame frame;
        for (auto& slave : slaves_)
        {
            auto process = [&slave](DatagramHeader const*, uint8_t const* data, uint16_t wkc)
            {
                if (wkc != 1)
                {
                    DEBUG_PRINT("Invalid working counter for slave %d\n", slave.address);
                    return DatagramState::INVALID_WKC;
                }

                if (not slave.mailbox.receive(data))
                {
                    DEBUG_PRINT("Slave %d: receive a message but didn't process it\n", slave.address);
                    return DatagramState::NO_HANDLER;
                }

                return DatagramState::OK;
            };

            if (slave.mailbox.can_read)
            {
                // retrieve waiting message
                link_->addDatagram(Command::FPRD, createAddress(slave.address, slave.mailbox.send_offset), nullptr, slave.mailbox.send_size, process, error);
            }
        }
    }


    void Bus::processMessages(std::function<void(DatagramState const&)> const& error)
    {
        sendWriteMessages(error);
        sendReadMessages(error);
        link_->processDatagrams();
    }


    void Bus::sendNop(std::function<void(DatagramState const&)> const& error)
    {
        auto process = [](DatagramHeader const*, uint8_t const*, uint16_t) { return DatagramState::OK; };
        link_->addDatagram(Command::NOP, 0, nullptr, 1, process, error);
    }


    void Bus::processAwaitingFrames()
    {
        link_->processDatagrams();
    }


    void Bus::finalizeDatagrams()
    {
        link_->finalizeDatagrams();
    }


    std::shared_ptr<GatewayMessage> Bus::addGatewayMessage(uint8_t const* raw_message, int32_t raw_message_size, uint16_t gateway_index)
    {
        mailbox::Header const* const mbx_header = reinterpret_cast<mailbox::Header const*>(raw_message);

        // Try to associate the request with a destination
        if (mbx_header->address == 0)
        {
            // Master is the destination, unsupported for now (ETG 1510)
            DEBUG_PRINT("Master mailbox not implemented");
            return nullptr;
        }

        auto it = std::find_if(slaves_.begin(), slaves_.end(), [&](Slave const& slave) { return slave.address == mbx_header->address; });
        if (it == slaves_.end())
        {
            DEBUG_PRINT("No slave with address %d on the bus", mbx_header->address);
            return nullptr;
        }

        return it->mailbox.createGatewayMessage(raw_message, raw_message_size, gateway_index);
    }


    void Bus::clearErrorCounters()
    {
        uint16_t clear_param[20] = {0}; // Note: value is not taken into account by the slave and result will always be zero
        uint16_t wkc = broadcastWrite(reg::ERROR_COUNTERS, clear_param, 20);
        if (wkc != slaves_.size())
        {
            THROW_ERROR("Invalid working counter");
        }
    }

    void Bus::sendRefreshErrorCounters(std::function<void(DatagramState const&)> const& error)
    {
        for (auto& slave : slaves_)
        {
            auto process = [&slave](DatagramHeader const*, uint8_t const* data, uint16_t wkc)
            {
                if (wkc != 1)
                {
                    DEBUG_PRINT("Invalid working counter for slave %d\n", slave.address);
                    return DatagramState::INVALID_WKC;
                }

                std::memcpy(&slave.error_counters, data, sizeof(ErrorCounters));
                return DatagramState::OK;
            };

            link_->addDatagram(Command::FPRD, createAddress(slave.address, reg::ERROR_COUNTERS), nullptr, sizeof(ErrorCounters), process, error);
        }
    }

    void Bus::sendGetDLStatus(Slave& slave, std::function<void(DatagramState const&)> const& error)
    {
        auto process = [&slave](DatagramHeader const*, uint8_t const* data, uint16_t wkc)
        {
            if (wkc != 1)
            {
                return DatagramState::INVALID_WKC;
            }

            slave.dl_status= *reinterpret_cast<DLStatus const*>(data);
            return DatagramState::OK;
        };

        link_->addDatagram(Command::FPRD, createAddress(slave.address, reg::ESC_DL_STATUS), nullptr, 2, process, error);
    }

}
