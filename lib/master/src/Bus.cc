#include <cstring>
#include <inttypes.h>
#include <algorithm>

#include "debug.h"
#include "Bus.h"
#include "Prints.h"

#include "CoE/mailbox/request.h"

namespace kickcat
{
    Bus::Bus(std::shared_ptr<AbstractLink> link)
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
        try
        {
            wkc = broadcastRead(reg::TYPE, 1);
        }
        catch (std::exception const& e)
        {
            bus_error("detectSlaves failed: %s\n", e.what());
            return 0;
        }
        slaves_.resize(wkc);
        bus_info("%zu slave detected on the network\n", slaves_.size());
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
        fetchESC();
        fetchDL();

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

        // create callbacks reception for mailbox (that do not depends on a request initiated by the master)
        for (auto& slave : slaves_)
        {
            if (slave.sii.info.mailbox_protocol & eeprom::MailboxProtocol::CoE)
            {
                // CoE emergency callback
                auto emg = std::make_shared<mailbox::request::EmergencyMessage>(slave.mailbox);
                slave.mailbox.to_process.push_back(emg);
            }

            if ((slave.sii.info.mailbox_protocol & eeprom::MailboxProtocol::EoE) or
                (slave.sii.info.mailbox_protocol & eeprom::MailboxProtocol::FoE) or
                (slave.sii.info.mailbox_protocol & eeprom::MailboxProtocol::SoE) or
                (slave.sii.info.mailbox_protocol & eeprom::MailboxProtocol::CoE) or
                (slave.sii.info.mailbox_protocol & eeprom::MailboxProtocol::AoE))
            {
                // check callback to display what's missing
                auto chk = std::make_shared<mailbox::request::CheckMessage>(slave.mailbox);
                slave.mailbox.to_process.push_back(chk);
            }
        }
    }


    void Bus::requestState(State request)
    {
        uint16_t param = request | State::ERROR_ACK;
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
            // slave.al_status_code =   *reinterpret_cast<uint16_t const*>(data + 4);
            std::memcpy(&slave.al_status_code, data + 4, sizeof(uint16_t));
            return DatagramState::OK;
        };

        link_->addDatagram(Command::FPRD, createAddress(slave.address, reg::AL_STATUS), nullptr, 6, process, error);
    }


    State Bus::getCurrentState(Slave& slave)
    {
        auto error = [](DatagramState const& state)
        {
            bus_error("Error while trying to get slave state (%s).\n", toString(state));
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
            THROW_ERROR_CODE("State transition error", error::category::AL, slave.al_status_code);
        }
        return State(slave.al_status);
    }


    void Bus::waitForState(State request, nanoseconds timeout, std::function<void()> background_task)
    {
        nanoseconds now = since_epoch();

        while (true)
        {
            background_task();

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
        broadcastWrite(reg::DC_CYCLIC_CONTROL,  param, 2);
        broadcastWrite(reg::ECAT_EVENT_MASK,    param, 2);
        broadcastWrite(reg::WDOG_COUNTER_PDO,   param, 2);

        uint16_t dc_param = 0x1000; // reset value
        broadcastWrite(reg::DC_SPEED_CNT_START, &dc_param, sizeof(dc_param));

        dc_param = 0x0c00;          // reset value
        broadcastWrite(reg::DC_TIME_FILTER, &dc_param, sizeof(dc_param));

        // reset ECAT Event registers
        broadcastRead(reg::DC_LATCH0_STATUS, 1);
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


    void Bus::fetchESC()
    {
        auto error = [](DatagramState const& state)
        {
            THROW_ERROR_DATAGRAM("Error while fetching Slave ESC description", state);
        };

        for (auto& slave : slaves_)
        {
            auto process = [this, &slave](DatagramHeader const*, uint8_t const* data, uint16_t wkc)
            {
                if (wkc != 1)
                {
                    return DatagramState::INVALID_WKC;
                }
                std::memcpy(&slave.esc, data, sizeof(ESC::Description));
                return DatagramState::OK;
            };

            link_->addDatagram(Command::FPRD, createAddress(slave.address, reg::TYPE), nullptr, sizeof(ESC::Description), process, error);
        }
        link_->processDatagrams();
    }


    void Bus::fetchDL()
    {
        auto error = [](DatagramState const& state)
        {
            THROW_ERROR_DATAGRAM("Error fetching DL status", state);
        };

        for (auto& slave : slaves_)
        {
            sendGetDLStatus(slave, error);
        }
        processAwaitingFrames();
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
            if (slave.sii.info.mailbox_protocol)
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

            if (slave.sii.info.mailbox_protocol & eeprom::MailboxProtocol::CoE)
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
                auto siiMapping = [&](Slave::PIMapping* mapping, std::vector<eeprom::PDOMapping> const& PDOs, SyncManagerType type)
                {
                    mapping->sync_manager = -1;
                    mapping->size = 0;
                    for (auto const& pdo : PDOs)
                    {
                        for (auto const& entry : pdo.entries)
                        {
                            mapping->size += entry.bitlen;
                        }
                    }
                    mapping->bsize = bits_to_bytes(mapping->size);
                    for (uint32_t i = 0; i < slave.sii.syncManagers.size(); ++i)
                    {
                        auto const& sm = slave.sii.syncManagers[i];
                        if (sm.type == type)
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


    void Bus::configureMailboxStatusCheck(MailboxStatusFMMU mode)
    {
        if (mode == MailboxStatusFMMU::NONE)
        {
            mailbox_status_fmmu_ = mode;
            return;
        }

        uint8_t required_fmmus = 3;
        if ((mode & MailboxStatusFMMU::READ_CHECK) and (mode & MailboxStatusFMMU::WRITE_CHECK))
        {
            required_fmmus = 4;
        }

        for (auto const& slave : slaves_)
        {
            if (slave.sii.info.mailbox_protocol == 0)
            {
                continue;
            }

            if (slave.esc.fmmus < required_fmmus)
            {
                bus_error("Slave %d has %d FMMUs, need %d for requested mailbox status FMMU mode\n",
                    slave.address, slave.esc.fmmus, required_fmmus);
                THROW_ERROR("Insufficient FMMUs for mailbox status mapping");
            }
        }

        mailbox_status_fmmu_ = mode;
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
        std::vector<std::vector<Slave*>> frame_mbx_slaves(1);
        uint32_t address = 0;
        for (auto& slave : slaves_)
        {
            // get the biggest one.
            int32_t size = std::max(slave.input.bsize, slave.output.bsize);
            if ((address + size) > (pi_frames_.size() * MAX_ETHERCAT_PAYLOAD_SIZE)) // do we overflow current frame ?
            {
                pi_frames_.back().logical_size = address - pi_frames_.back().address; // frame size = current address - frame address

                // current size will overflow the frame at the current offset: set in on the next frame
                address = static_cast<uint32_t>(pi_frames_.size()) * MAX_ETHERCAT_PAYLOAD_SIZE;
                pi_frames_.push_back({address, 0, 0, {}, {}, {}, {}, 0, {}});
                frame_mbx_slaves.push_back({});
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

            if (slave.sii.info.mailbox_protocol != 0)
            {
                frame_mbx_slaves.back().push_back(&slave);
            }
        }

        // update last frame size
        pi_frames_.back().logical_size = address - pi_frames_.back().address;

        // Set pdo_size for all frames (equals logical_size before mailbox status extension)
        for (auto& frame : pi_frames_)
        {
            frame.pdo_size = frame.logical_size;
        }

        // Extend frames with bit-wise mailbox status mappings
        if (mailbox_status_fmmu_ != MailboxStatusFMMU::NONE)
        {
            for (size_t fi = 0; fi < pi_frames_.size(); ++fi)
            {
                auto& frame = pi_frames_[fi];
                auto const& mbx_slaves = frame_mbx_slaves[fi];
                if (mbx_slaves.empty())
                {
                    continue;
                }

                // ESC constraint: two read-direction FMMUs using bit-wise mapping on the
                // same ESC must be separated by at least 3 logical bytes not configured by
                // any FMMU of the same type (see ESC datasheet, "Restrictions on FMMU settings").
                // PDO input FMMU (FMMU1) is read-direction, and so are mailbox status FMMUs,
                // so we insert a 3-byte gap between the PDO region and the status region,
                // and between the read-check and write-check regions when both are active.
                uint32_t status_offset = static_cast<uint32_t>(frame.pdo_size);
                constexpr uint32_t FMMU_BYTE_SEPARATION = 3;

                if (mailbox_status_fmmu_ & MailboxStatusFMMU::READ_CHECK)
                {
                    status_offset += FMMU_BYTE_SEPARATION;
                    for (size_t i = 0; i < mbx_slaves.size(); ++i)
                    {
                        frame.mailbox_read_status.push_back({
                            status_offset + static_cast<uint32_t>(i / 8),
                            static_cast<uint8_t>(i % 8),
                            mbx_slaves[i]
                        });
                    }
                    uint32_t read_bytes = (static_cast<uint32_t>(mbx_slaves.size()) + 7) / 8;
                    status_offset += read_bytes;
                }

                if (mailbox_status_fmmu_ & MailboxStatusFMMU::WRITE_CHECK)
                {
                    status_offset += FMMU_BYTE_SEPARATION;
                    for (size_t i = 0; i < mbx_slaves.size(); ++i)
                    {
                        frame.mailbox_write_status.push_back({
                            status_offset + static_cast<uint32_t>(i / 8),
                            static_cast<uint8_t>(i % 8),
                            mbx_slaves[i]
                        });
                    }
                    uint32_t write_bytes = (static_cast<uint32_t>(mbx_slaves.size()) + 7) / 8;
                    status_offset += write_bytes;
                }

                frame.logical_size = static_cast<int32_t>(status_offset);

                // WKC adjustment: only count slaves that gain a read-direction FMMU
                // but have no TxPDO (input). Slaves with TxPDO already increment WKC
                // on LRD/LRW, so their mailbox status FMMU adds no extra increment.
                uint16_t adjust = 0;
                for (auto const* slave : mbx_slaves)
                {
                    if (slave->input.bsize == 0)
                    {
                        ++adjust;
                    }
                }
                frame.mailbox_status_wkc_read_adjust = adjust;
            }
        }

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

        // Fourth step: pre-allocate write buffers for cyclic path
        for (auto& frame : pi_frames_)
        {
            frame.write_buffer.resize(frame.logical_size);
        }

        // Fifth step: program FMMUs and SyncManagers
        configureFMMUs();

        if (mailbox_status_fmmu_ != MailboxStatusFMMU::NONE)
        {
            configureMailboxFMMUs();
        }
    }


    void Bus::sendLogicalRead(std::function<void(DatagramState const&)> const& error)
    {
        for (auto const& pi_frame : pi_frames_)
        {
            auto process = [&pi_frame](DatagramHeader const*, uint8_t const* data, uint16_t wkc)
            {
				// copy before: a wrong wkc doesn't mean that all data shall be discarded
                for (auto& input : pi_frame.inputs)
                {
                    std::memcpy(input.iomap, data + input.offset, input.size);
                }

                for (auto const& ms : pi_frame.mailbox_read_status)
                {
                    ms.slave->mailbox.can_read = (data[ms.byte_offset] >> ms.bit_position) & 1;
                }
                for (auto const& ms : pi_frame.mailbox_write_status)
                {
                    ms.slave->mailbox.can_write = not ((data[ms.byte_offset] >> ms.bit_position) & 1);
                }

                uint16_t expected_wkc = static_cast<uint16_t>(pi_frame.inputs.size() + pi_frame.mailbox_status_wkc_read_adjust);
                if (wkc != expected_wkc)
                {
                    bus_error("Invalid working counter: expected %d, got %d\n", expected_wkc, wkc);
                    return DatagramState::INVALID_WKC;
                }

                return DatagramState::OK;
            };

            link_->addDatagram(Command::LRD, pi_frame.address, nullptr, static_cast<uint16_t>(pi_frame.logical_size), process, error);
        }
    }


    void Bus::processDataRead(std::function<void(DatagramState const&)> const& error)
    {
        sendLogicalRead(error);
        link_->processDatagrams();
    }


    void Bus::sendLogicalWrite(std::function<void(DatagramState const&)> const& error)
    {
        for (auto& pi_frame : pi_frames_)
        {
            for (auto const& output : pi_frame.outputs)
            {
                std::memcpy(pi_frame.write_buffer.data() + output.offset, output.iomap, output.size);
            }

            auto process = [&pi_frame](DatagramHeader const*, uint8_t const*, uint16_t wkc)
            {
                if (wkc != pi_frame.outputs.size())
                {
                    bus_error("Invalid working counter: expected %zu, got %" PRIu16 "\n", pi_frame.outputs.size(), wkc);
                    return DatagramState::INVALID_WKC;
                }
                return DatagramState::OK;
            };
            link_->addDatagram(Command::LWR, pi_frame.address, pi_frame.write_buffer.data(), static_cast<uint16_t>(pi_frame.pdo_size), process, error);
        }

        if (dc_slave_ != nullptr)
        {
            sendDriftCompensation(error);
        }
    }


    void Bus::processDataWrite(std::function<void(DatagramState const&)> const& error)
    {
        sendLogicalWrite(error);
        link_->processDatagrams();
    }


    void Bus::sendLogicalReadWrite(std::function<void(DatagramState const&)> const& error)
    {
        for (auto& pi_frame : pi_frames_)
        {
            for (auto const& output : pi_frame.outputs)
            {
                std::memcpy(pi_frame.write_buffer.data() + output.offset, output.iomap, output.size);
            }

            auto process = [&pi_frame](DatagramHeader const*, uint8_t const* data, uint16_t wkc)
            {
                uint16_t expected_wkc = static_cast<uint16_t>(pi_frame.inputs.size() + pi_frame.outputs.size() * 2
                    + pi_frame.mailbox_status_wkc_read_adjust);
                if (wkc != expected_wkc)
                {
                    bus_error("Invalid working counter: expected %d, got %d\n", expected_wkc, wkc);
                    return DatagramState::INVALID_WKC;
                }

                for (auto& input : pi_frame.inputs)
                {
                    std::memcpy(input.iomap, data + input.offset, input.size);
                }

                for (auto const& ms : pi_frame.mailbox_read_status)
                {
                    ms.slave->mailbox.can_read = (data[ms.byte_offset] >> ms.bit_position) & 1;
                }
                for (auto const& ms : pi_frame.mailbox_write_status)
                {
                    ms.slave->mailbox.can_write = not ((data[ms.byte_offset] >> ms.bit_position) & 1);
                }

                return DatagramState::OK;
            };

            link_->addDatagram(Command::LRW, pi_frame.address, pi_frame.write_buffer.data(), static_cast<uint16_t>(pi_frame.logical_size), process, error);
        }

        if (dc_slave_ != nullptr)
        {
            sendDriftCompensation(error);
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
            auto& sii_sm = slave.sii.syncManagers[mapping.sync_manager];

            SyncManager sm;
            FMMU fmmu;
            std::memset(&sm,   0, sizeof(SyncManager));
            std::memset(&fmmu, 0, sizeof(FMMU));

            uint16_t targeted_fmmu = reg::FMMU; // FMMU0 - outputs
            sm.control = 0x64;                  // 3 buffers - write acces - PDI IRQ ON - Watchdog trigger
            fmmu.type  = 2;                     // write access
            if (type == SyncManagerType::Input)
            {
                sm.control = 0x20;              // 3 buffers - read acces - PDI IRQ ON
                fmmu.type  = 1;                 // read access
                targeted_fmmu += 0x10;          // FMMU1 - inputs (slave to master)
            }

            sm.start_address = sii_sm.start_address;
            sm.length        = static_cast<uint16_t>(mapping.bsize);
            sm.status        = 0x00; // RO register
            sm.activate      = 0x01; // Sync Manager enable
            sm.pdi_control   = 0x00; // RO register
            link_->addDatagram(Command::FPWR,
                              createAddress(slave.address, reg::SYNC_MANAGER + static_cast<uint16_t>(mapping.sync_manager * 8)),
                              sm, process, error);
            bus_info("SM[%" PRIi32 "] type %d - start address 0x%04x - length %d - flags: 0x%02x\n", mapping.sync_manager, type, sm.start_address, sm.length, sm.control);

            fmmu.logical_address    = mapping.address;
            fmmu.length             = static_cast<uint16_t>(mapping.bsize);
            fmmu.logical_start_bit  = 0;   // we map every bits
            fmmu.logical_stop_bit   = 0x7; // we map every bits
            fmmu.physical_address   = sii_sm.start_address;
            fmmu.physical_start_bit = 0;
            fmmu.activate           = 1;
            link_->addDatagram(Command::FPWR, createAddress(slave.address, targeted_fmmu), fmmu, process, error);
            bus_info("slave %04x - size %" PRIu32 " - ladd 0x%04" PRIu32 " - paddr 0x%04x\n", slave.address, mapping.bsize, mapping.address, fmmu.physical_address);
        };

        for (auto& slave : slaves_)
        {
            prepareDatagrams(slave, slave.input,  SyncManagerType::Input);
            prepareDatagrams(slave, slave.output, SyncManagerType::Output);
        }

        link_->processDatagrams();
    }


    void Bus::configureMailboxFMMUs()
    {
        auto process = [](DatagramHeader const*, uint8_t const*, uint16_t wkc)
        {
            if (wkc != 1)
            {
                return DatagramState::INVALID_WKC;
            }
            return DatagramState::OK;
        };

        auto error = [](DatagramState const& state)
        {
            THROW_ERROR_DATAGRAM("Invalid working counter while programming mailbox status FMMU", state);
        };

        for (auto const& frame : pi_frames_)
        {
            auto configureBitFMMU = [&](auto const& entries, uint16_t physical_address, uint16_t fmmu_reg_offset)
            {
                for (auto const& entry : entries)
                {
                    FMMU fmmu;
                    std::memset(&fmmu, 0, sizeof(FMMU));
                    fmmu.logical_address    = frame.address + entry.byte_offset;
                    fmmu.length             = 1;
                    fmmu.logical_start_bit  = entry.bit_position;
                    fmmu.logical_stop_bit   = entry.bit_position;
                    fmmu.physical_address   = physical_address;
                    fmmu.physical_start_bit = 3; // SM_STATUS_MAILBOX bit
                    fmmu.type               = 1; // read access (slave to master)
                    fmmu.activate           = 1;

                    link_->addDatagram(Command::FPWR,
                        createAddress(entry.slave->address, static_cast<uint16_t>(reg::FMMU + fmmu_reg_offset)),
                        fmmu, process, error);

                    bus_info("Mailbox FMMU slave %04x - FMMU%d - logical 0x%08x bit %d - physical 0x%04x bit 3\n",
                        entry.slave->address, fmmu_reg_offset / 0x10,
                        fmmu.logical_address, entry.bit_position, physical_address);
                }
            };

            // FMMU0 and FMMU1 are reserved for PDO (output and input).
            // FMMU2 is used by the first enabled mailbox check mode.
            // FMMU3 is used by the second one, only when both modes are active.
            constexpr uint16_t FMMU2_OFFSET  = 0x20;
            constexpr uint16_t FMMU_REG_SIZE = 0x10;

            uint16_t fmmu_offset = FMMU2_OFFSET;

            if (mailbox_status_fmmu_ & MailboxStatusFMMU::READ_CHECK)
            {
                configureBitFMMU(frame.mailbox_read_status,
                    static_cast<uint16_t>(reg::SYNC_MANAGER_1 + reg::SM_STATS), fmmu_offset);
                fmmu_offset += FMMU_REG_SIZE;
            }

            if (mailbox_status_fmmu_ & MailboxStatusFMMU::WRITE_CHECK)
            {
                configureBitFMMU(frame.mailbox_write_status,
                    static_cast<uint16_t>(reg::SYNC_MANAGER_0 + reg::SM_STATS), fmmu_offset);
            }
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
            uint16_t answer;
            std::memcpy(&answer, data, sizeof(uint16_t));
            if (answer & eeprom::Control::BUSY)
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
        eeprom::Request req;

        // Request specific address
        {
            req = { eeprom::Control::READ, address, 0 };
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
            auto process = [&slave, &apply](DatagramHeader const*, uint8_t const* data, uint16_t wkc)
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
        // Per-slave accumulation buffers (indexed same as slaves_)
        std::vector<std::vector<uint32_t>> buffers(slaves_.size());

        std::vector<Slave*> pending;
        for (auto& slave : slaves_)
        {
            pending.push_back(&slave);
        }

        auto getBuffer = [&](Slave& s) -> std::vector<uint32_t>&
        {
            auto idx = static_cast<std::size_t>(&s - slaves_.data());
            return buffers[idx];
        };

        // Get SII
        int32_t pos = 0;
        while (not pending.empty())
        {
            readEeprom(static_cast<uint16_t>(pos), pending,
            [&](Slave& s, uint32_t word)
            {
                getBuffer(s).push_back(word);
            });

            pos += 2;

            pending.erase(std::remove_if(pending.begin(), pending.end(),
            [&](Slave* s)
            {
                auto& buf = getBuffer(*s);
                return (((buf.back() >> 16) == eeprom::Category::End) or
                        ((buf.back() & eeprom::Category::End) == eeprom::Category::End)) and
                         (buf.size() > 32);
            }),
            pending.end());
        }

        // Parse SII
        for (std::size_t i = 0; i < slaves_.size(); ++i)
        {
            auto& buf = buffers[i];
            slaves_[i].parseSII(reinterpret_cast<uint8_t const*>(buf.data()),
                                buf.size() * sizeof(uint32_t));
        }
    }


    bool Bus::isEepromAcknowledged(Slave& slave)
    {
        bool acknowleded = true;
        auto process = [&acknowleded](DatagramHeader const*, uint8_t const* data, uint16_t wkc)
        {
            if (wkc != 1)
            {
                return DatagramState::INVALID_WKC;
            }
            uint16_t answer;
            std::memcpy(&answer, data, sizeof(uint16_t));
            if (answer & eeprom::Control::ERROR_CMD) // Missing EEPROM acknowledge or invalid command
            {
                acknowleded = false;
            }
            return DatagramState::OK;
        };

        auto error = [](DatagramState const&)
        {
            THROW_ERROR("Error while fetching eeprom state");
        };

        link_->addDatagram(Command::FPRD, createAddress(slave.address, reg::EEPROM_CONTROL), nullptr, 2, process, error);
        link_->processDatagrams();

        return acknowleded;
    }


    void Bus::writeEeprom(Slave& slave, uint16_t address, void* data, uint16_t size)
    {
        if (size > 2)
        {
            THROW_ERROR("Can't write more than 2 bytes to eeprom data");
        }

        // Read result
        auto error = [](DatagramState const& state)
        {
            THROW_ERROR_DATAGRAM("Error while writing eeprom data", state);
        };

        auto process = [](DatagramHeader const*, void const*, uint16_t wkc)
        {
            if (wkc != 1)
            {
                bus_error("Process INVALID WKC \n");
                return DatagramState::INVALID_WKC;
            }
            return DatagramState::OK;
        };

        if (not areEepromReady())
        {
            THROW_ERROR("Timeout eeprom busy");
        }

        link_->addDatagram(Command::FPWR, createAddress(slave.address, reg::EEPROM_DATA), data, size, process, error);
        link_->processDatagrams();

        // Request specific address
        eeprom::Request req;
        req = {eeprom::Control::WRITE | eeprom::Control::WR_EN, address, 0};

        bool acknowledged = false;

        nanoseconds start_time = since_epoch();
        while (not acknowledged)
        {
            link_->addDatagram(Command::FPWR, createAddress(slave.address, reg::EEPROM_CONTROL), &req, sizeof(req), process, error);
            link_->processDatagrams();
            acknowledged = isEepromAcknowledged(slave);
            sleep(big_wait);

            if (elapsed_time(start_time) > (10 * big_wait))
            {
                THROW_ERROR("Timeout acknowledge write eeprom");
            }
        }

        if (not areEepromReady())
        {
            THROW_ERROR("Timeout eeprom busy");
        }
    }


    void Bus::sendMailboxesReadChecks(std::function<void(DatagramState const&)> const& error)
    {
        auto isFull = [](uint8_t state, uint16_t wkc, bool stable_value)
        {
            if (wkc != 1)
            {
                bus_error("Invalid working counter\n");
                return stable_value;
            }
            return ((state & SM_STATUS_MAILBOX) == SM_STATUS_MAILBOX);
        };

        for (auto& slave : slaves_)
        {
            auto process_read = [&slave, isFull](DatagramHeader const*, uint8_t const* state, uint16_t wkc)
            {
                slave.mailbox.can_read = isFull(*state, wkc, false);
                return DatagramState::OK;
            };

            if (slave.sii.info.mailbox_protocol == 0)
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
                bus_error("Invalid working counter\n");
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

            if (slave.sii.info.mailbox_protocol == 0)
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
                bus_error("Invalid working counter\n");
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
                    bus_error("Invalid working counter for slave %d\n", slave.address);
                    return DatagramState::INVALID_WKC;
                }

                if (not slave.mailbox.receive(data))
                {
                    bus_warning("Slave %d: receive a message but didn't process it\n", slave.address);
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


    std::shared_ptr<mailbox::request::GatewayMessage> Bus::addGatewayMessage(uint8_t const* raw_message, int32_t raw_message_size, uint16_t gateway_index)
    {
        mailbox::Header mbx_header;
        std::memcpy(&mbx_header, raw_message, sizeof(mailbox::Header));

        // Try to associate the request with a destination
        if (mbx_header.address == 0)
        {
            // Master is the destination, unsupported for now (ETG 1510)
            bus_error("Master mailbox not implemented");
            return nullptr;
        }

        auto it = std::find_if(slaves_.begin(), slaves_.end(), [&](Slave const& slave) { return slave.address == mbx_header.address; });
        if (it == slaves_.end())
        {
            bus_error("No slave with address %d on the bus", mbx_header.address);
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
                    bus_error("Invalid working counter for slave %d\n", slave.address);
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

            std::memcpy(&slave.dl_status, data, sizeof(DLStatus));
            return DatagramState::OK;
        };

        link_->addDatagram(Command::FPRD, createAddress(slave.address, reg::ESC_DL_STATUS), nullptr, 2, process, error);
    }

}
