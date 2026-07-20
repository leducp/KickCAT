#include <cstring>
#include <fstream>

#include "kickcat/ESC/EmulatedESC.h"
#include "kickcat/OS/Time.h"
#include "kickcat/debug.h"


namespace kickcat
{
    EmulatedESC::EmulatedESC()
        : memory_{}
    {
        // Configure ESC constants
        memory_.type            = 0x04;     // IP Core
        memory_.revision        = 3;        // IP Core major version
        memory_.build           = 0x0301;   // v3.0.1
        memory_.ffmus_supported = 8;
        memory_.sync_managers_supported = 8;
        memory_.ram_size        = 60;       // 60KB, IP Core max
        memory_.port_desc       = 0x0f;     // 2 ports (0, 1), MII
        memory_.esc_features    = 0x01cc;

        // Default value is 'Request INIT State'
        memory_.al_control = State::INIT;

        // Default value is 'INIT State'
        memory_.al_status = State::INIT;

        // eeprom never busy because the data are processed in sync with the request.
        memory_.eeprom_control &= ~0x8000;

        // DC time loop control reset values (datasheet sec2 2.15.2.5/2.15.2.7/2.15.2.8)
        uint16_t const speed_counter_start = 0x1000;
        std::memcpy(memory_.DC + (reg::DC_SPEED_CNT_START - reg::DC_RECEIVED_TIME), &speed_counter_start, sizeof(speed_counter_start));
        memory_.DC[reg::DC_TIME_FILTER            - reg::DC_RECEIVED_TIME] = 4;
        memory_.DC[reg::DC_SPEED_CNT_FILTER_DEPTH - reg::DC_RECEIVED_TIME] = 12;

        // Disabled watchdog = OK at power-on
        memory_.watchdog_status_process_data = 1;

        // Set default values in registers (TODO: a lot of them are left uninitialized)
        std::memset(memory_.sync_manager, 0, sizeof(memory_.sync_manager));
    }

    EmulatedESC::EmulatedESC(fs::path const& path)
        : EmulatedESC()
    {
        loadEeprom(path);
    }

    void EmulatedESC::loadEeprom(fs::path const& path)
    {
        std::vector<uint8_t> image = loadBinaryFile(path);
        loadEeprom(image);
    }

    std::vector<uint8_t> loadBinaryFile(fs::path const& path) {
        std::ifstream eeprom_file;
        eeprom_file.open(path, std::ios::binary | std::ios::ate);
        if (not eeprom_file.is_open())
        {
            THROW_ERROR("Cannot load EEPROM");
        }
        int size = eeprom_file.tellg();
        eeprom_file.seekg (0, std::ios::beg);
        std::vector<uint8_t> image(static_cast<std::size_t>(size));
        eeprom_file.read(reinterpret_cast<char*>(image.data()), size);
        eeprom_file.close();
        return image;
    }

    void EmulatedESC::loadEeprom(std::vector<uint16_t> const& eeprom_data)
    {
        eeprom_ = eeprom_data;
        loadEeprom();
    }

    void EmulatedESC::loadEeprom(std::vector<uint8_t> const& image)
    {
        eeprom_.assign((image.size() + 1) / 2, 0);  // word-addressed; odd trailing byte zero-filled
        std::memcpy(eeprom_.data(), image.data(), image.size());
        loadEeprom();
    }


    void EmulatedESC::loadEeprom()
    {
        // Reads words 0 and 4; a RELOAD command can reach this before any image is
        // loaded (empty eeprom_), so guard against an out-of-bounds access.
        if (eeprom_.size() <= 4)
        {
            return;
        }

        // Device emulation
        memory_.esc_configuration = eeprom_[0] >> 8;

        memory_.station_alias = eeprom_[4]; // fourth word of eeprom, at first load
    }


    void EmulatedESC::setClockDrift(double ppm)
    {
        nanoseconds now = since_ecat_epoch();
        drift_accumulated_ += nanoseconds(static_cast<int64_t>(
            static_cast<double>((now - drift_origin_).count()) * clock_drift_ppm_ * 1e-6));
        drift_origin_ = now;
        clock_drift_ppm_ = ppm;
    }


    nanoseconds EmulatedESC::localClock(nanoseconds ref) const
    {
        int64_t drift = static_cast<int64_t>(
            static_cast<double>((ref - drift_origin_).count()) * clock_drift_ppm_ * 1e-6);
        return ref + drift_accumulated_ + nanoseconds(drift);
    }


    nanoseconds EmulatedESC::localSystemTime() const
    {
        int64_t offset;
        std::memcpy(&offset, memory_.DC + (reg::DC_SYSTEM_TIME_OFFSET - reg::DC_RECEIVED_TIME), sizeof(offset));
        return localClock(since_ecat_epoch()) + nanoseconds(offset) + dc_correction_;
    }


    void EmulatedESC::dcSystemTimeRead(uint16_t offset, uint16_t size)
    {
        if ((offset >= reg::DC_ECAT_RECEIVED_TIME) or ((offset + size) <= reg::DC_SYSTEM_TIME))
        {
            return;
        }
        uint64_t t = static_cast<uint64_t>(localSystemTime().count());

        // Zero-mean read jitter: perturb the reported system time by a uniform offset in
        // [-amplitude, +amplitude]. Lets the host exercise the master's soft-PLL rejection.
        if (clock_jitter_ > 0ns)
        {
            jitter_rng_ ^= jitter_rng_ << 13;
            jitter_rng_ ^= jitter_rng_ >> 7;
            jitter_rng_ ^= jitter_rng_ << 17;
            int64_t const span  = 2 * clock_jitter_.count() + 1;
            int64_t const noise = static_cast<int64_t>(jitter_rng_ % static_cast<uint64_t>(span)) - clock_jitter_.count();
            t = static_cast<uint64_t>(static_cast<int64_t>(t) + noise);
        }

        std::memcpy(memory_.DC + (reg::DC_SYSTEM_TIME - reg::DC_RECEIVED_TIME), &t, sizeof(t));
    }


    void EmulatedESC::dcTimeLoopWrite(uint16_t offset, uint16_t size)
    {
        if ((offset > reg::DC_SPEED_CNT_FILTER_DEPTH) or ((offset + size) <= reg::DC_SYSTEM_TIME))
        {
            return;
        }

        if ((offset < reg::DC_ECAT_RECEIVED_TIME) and ((offset + size) > reg::DC_SYSTEM_TIME))
        {
            uint64_t received;
            std::memcpy(&received, memory_.DC + (reg::DC_SYSTEM_TIME - reg::DC_RECEIVED_TIME), sizeof(received));
            uint32_t delay;
            std::memcpy(&delay, memory_.DC + (reg::DC_SYSTEM_TIME_DELAY - reg::DC_RECEIVED_TIME), sizeof(delay));

            // Datasheet sec1 9.1.3.3: dt = (local time + offset - propagation delay) - received.
            // Simplifications vs a real ESC time control loop:
            //  - local copy sampled at datagram processing time, not latched at frame SOF
            //  - trim is a bounded slew of the local copy (at most one speed-counter-start
            //    unit, taken as ns, per update) instead of an oscillator speed adjustment
            //    that also acts between updates
            //  - speed counter diff (0x932) and its filter (0x935) are not modeled
            int64_t dt = (localSystemTime().count() - delay) - static_cast<int64_t>(received);

            uint16_t speed_counter_start;
            std::memcpy(&speed_counter_start, memory_.DC + (reg::DC_SPEED_CNT_START - reg::DC_RECEIVED_TIME), sizeof(speed_counter_start));
            int64_t bound = speed_counter_start & 0x7FFF;
            int64_t step = dt;
            if (step > bound)
            {
                step = bound;
            }
            if (step < -bound)
            {
                step = -bound;
            }
            dc_correction_ -= nanoseconds(step);

            uint8_t depth = memory_.DC[reg::DC_TIME_FILTER - reg::DC_RECEIVED_TIME] & 0x0F;
            if (depth == 0)
            {
                dc_diff_filtered_ = dt;
            }
            else
            {
                dc_diff_filtered_ += (dt - dc_diff_filtered_) / (int64_t(1) << depth);
            }

            // 0x92C sign-magnitude: bit 31 set when the local copy is >= the received
            // system time (datasheet sec2 2.15.2.4), bits 30:0 magnitude, saturated.
            uint64_t magnitude;
            if (dc_diff_filtered_ >= 0)
            {
                magnitude = static_cast<uint64_t>(dc_diff_filtered_);
            }
            else
            {
                magnitude = static_cast<uint64_t>(-dc_diff_filtered_);
            }
            if (magnitude > 0x7FFFFFFF)
            {
                magnitude = 0x7FFFFFFF;
            }
            uint32_t raw = static_cast<uint32_t>(magnitude);
            if (dc_diff_filtered_ >= 0)
            {
                raw |= 0x80000000u;
            }
            std::memcpy(memory_.DC + (reg::DC_SYSTEM_TIME_DIFF - reg::DC_RECEIVED_TIME), &raw, sizeof(raw));
        }

        // Writing speed counter start or a filter depth resets the loop filters and
        // the system time difference (datasheet sec2 2.15.2.5/2.15.2.7).
        if ((offset + size) > reg::DC_SPEED_CNT_START)
        {
            dc_diff_filtered_ = 0;
            uint32_t const zero = 0;
            std::memcpy(memory_.DC + (reg::DC_SYSTEM_TIME_DIFF - reg::DC_RECEIVED_TIME), &zero, sizeof(zero));
        }
    }


    int32_t EmulatedESC::computeInternalMemoryAccess(uint16_t address, void* buffer, uint16_t size, Access access)
    {
        uint8_t* pos = reinterpret_cast<uint8_t*>(&memory_) + address;

        if (address >= 0x1000)
        {
            uint16_t to_copy = std::min(size, uint16_t(UINT16_MAX - address));

            // ESC RAM access -> check if a syncmanager allow the access
            for (auto& sync : syncs_)
            {
                // Check access rights
                if (not (access & sync.access))
                {
                    continue;
                }

                // Check that the access is contains in the SM space
                if ((address < sync.address) or ((address + to_copy) > (sync.address + sync.size)))
                {
                    continue;
                }

                // Everything is fine: do the copy
                switch (access)
                {
                    case PDI_READ:
                    case ECAT_READ:
                    {
                        if (not (sync.registers->status & SM_STATUS_MAILBOX) and (sync.registers->control & 0x3))
                        {
                            return -EAGAIN; // Cannot read mailbox: it is empty
                        }
                        std::memcpy(buffer, pos, to_copy);

                        if ((address + size - 1) == (sync.address + sync.size - 1))
                        {
                            // Last byte read -> access is done and mailbox is now empty
                            sync.registers->status &= ~SM_STATUS_MAILBOX;
                        }
                        return to_copy;
                    }
                    case PDI_WRITE:
                    case ECAT_WRITE:
                    {
                        if ((sync.registers->status & SM_STATUS_MAILBOX) and (sync.registers->control & 0x3))
                        {
                            return -EAGAIN; // Cannot write mailbox: it is full
                        }
                        std::memcpy(pos, buffer, to_copy);

                        if ((address + size - 1) == (sync.address + sync.size - 1))
                        {
                            // Last byte written -> access is done and mailbox is now full
                            sync.registers->status |= SM_STATUS_MAILBOX;
                        }
                        return to_copy;
                    }
                }
            }

            // PDI reads the output image and writes the input image; a sub-byte/register
            // FMMU has a zero-byte span, so it never matches a process-data access here.
            if (access & Access::PDI_READ)
            {
                for (auto& fmmu : fmmus_)
                {
                    if (fmmu.is_input or (pos < fmmu.physical) or ((pos + to_copy) > (fmmu.physical + fmmu.bit_length / 8)))
                    {
                        continue;
                    }
                    std::memcpy(buffer, pos, to_copy);
                    return to_copy;
                }
            }

            if (access & Access::PDI_WRITE)
            {
                for (auto& fmmu : fmmus_)
                {
                    if ((not fmmu.is_input) or (pos < fmmu.physical) or ((pos + to_copy) > (fmmu.physical + fmmu.bit_length / 8)))
                    {
                        continue;
                    }
                    std::memcpy(pos, buffer, to_copy);
                    return to_copy;
                }
            }

            // No SM nor FFMU enable this access
            return -EACCES;
        }

        // register access: cannot overlap memory after register space in one access
        uint16_t to_copy = std::min(size, uint16_t(0x1000 - address));
        switch (access)
        {
            case PDI_READ:
            case ECAT_READ:
            {
                std::memcpy(buffer, pos, to_copy);
                return to_copy;
            }
            case PDI_WRITE:
            case ECAT_WRITE:
            {
                std::memcpy(pos, buffer, to_copy);
                return to_copy;
            }
        }

        return -ENOTSUP; // shall not be possible to be reached
    }


    int32_t EmulatedESC::write(uint16_t address, void const* data, uint16_t size)
    {
        return computeInternalMemoryAccess(address, const_cast<void*>(data), size, Access::PDI_WRITE);
    }

    int32_t EmulatedESC::read(uint16_t address, void* data, uint16_t size)
    {
        return computeInternalMemoryAccess(address, data, size, Access::PDI_READ);
    }


    void EmulatedESC::processDatagram(DatagramHeader* header, void* data, uint16_t* wkc)
    {
        processEcatRequest(header, data, wkc);
        processInternalLogic();
    }


    bool EmulatedESC::copyFmmu(Fmmu const& fmmu, bool read, DatagramHeader const* header, void* frame)
    {
        uint32_t const start = header->address;
        uint32_t const end   = start + header->len;

        // Byte-aligned fast path; sub-byte mappings fall through to the bit loop below.
        if ((fmmu.logical_start_bit == 0) and (fmmu.physical_start_bit == 0) and ((fmmu.bit_length % 8) == 0))
        {
            uint32_t const size = fmmu.bit_length / 8;
            uint32_t const min  = std::max(start, fmmu.logical_address);
            uint32_t const max  = std::min(end,   fmmu.logical_address + size);
            if (max <= min)
            {
                return false;
            }
            uint32_t const to_copy = max - min;
            uint8_t* frame_p = static_cast<uint8_t*>(frame) + (min - start);
            uint8_t* phys_p  = fmmu.physical + (min - fmmu.logical_address);
            if (read)
            {
                std::memcpy(frame_p, phys_p, to_copy);
            }
            else
            {
                std::memcpy(phys_p, frame_p, to_copy);
                lastLogicalWrite_ = now();   // update watchdog
            }
            return true;
        }

        bool hit   = false;
        bool wrote = false;
        for (uint32_t b = 0; b < fmmu.bit_length; ++b)
        {
            uint32_t const logical_bit  = fmmu.logical_start_bit + b;
            uint32_t const logical_byte = fmmu.logical_address + (logical_bit / 8);
            if ((logical_byte < start) or (logical_byte >= end))
            {
                continue;
            }
            uint8_t* frame_byte = static_cast<uint8_t*>(frame) + (logical_byte - start);
            uint8_t const  loff = logical_bit % 8;
            uint32_t const physical_bit = fmmu.physical_start_bit + b;
            uint8_t* phys_byte = fmmu.physical + (physical_bit / 8);
            uint8_t const  poff = physical_bit % 8;
            if (read)
            {
                uint8_t const bit = (*phys_byte >> poff) & 1u;
                *frame_byte = static_cast<uint8_t>((*frame_byte & ~(1u << loff)) | (bit << loff));
            }
            else
            {
                uint8_t const bit = (*frame_byte >> loff) & 1u;
                *phys_byte = static_cast<uint8_t>((*phys_byte & ~(1u << poff)) | (bit << poff));
                wrote = true;
            }
            hit = true;
        }
        if (wrote)
        {
            lastLogicalWrite_ = now();   // update watchdog
        }
        return hit;
    }

    bool EmulatedESC::processFmmus(bool read, DatagramHeader const* header, void* frame)
    {
        bool hit = false;
        for (auto const& fmmu : fmmus_)
        {
            if (fmmu.is_input != read)   // read direction serves input FMMUs, write serves outputs
            {
                continue;
            }
            hit |= copyFmmu(fmmu, read, header, frame);
        }
        return hit;
    }


    void EmulatedESC::processLRD(DatagramHeader* header, void* data, uint16_t* wkc)
    {
        if (processFmmus(true, header, data))   // ETG.1000.4: one increment per direction, not per FMMU
        {
            *wkc += 1;
        }
    }

    void EmulatedESC::processLWR(DatagramHeader* header, void* data, uint16_t* wkc)
    {
        if (processFmmus(false, header, data))
        {
            *wkc += 1;
        }
    }

    void EmulatedESC::processLRW(DatagramHeader* header, void* data, uint16_t* wkc)
    {
        uint8_t swap[MAX_ETHERCAT_PAYLOAD_SIZE];
        std::memcpy(swap, data, header->len);

        bool const read  = processFmmus(true,  header, data);   // read directly into the frame
        bool const write = processFmmus(false, header, swap);   // write from the pre-read copy
        if (read)
        {
            *wkc += 1;
        }
        if (write)
        {
            *wkc += 2;
        }
    }


    bool EmulatedESC::matchesConfiguredAddress(uint16_t position) const
    {
        if (position == memory_.station_address)
        {
            return true;
        }
        if ((memory_.dl_control & 0x01000000) == 0)
        {
            return false; // alias addressing only when DL control 0x100[24] is set (master-written)
        }
        return (memory_.station_alias != 0) and (position == memory_.station_alias);
    }


    void EmulatedESC::processEcatRequest(DatagramHeader* header, void* data, uint16_t* wkc)
    {
        auto [position, offset] = extractAddress(header->address);
        switch (header->command)
        {
            //************* Auto Inc. *************//
            case Command::APRD:
            {
                if (position == 0)
                {
                    processReadCommand(header, data, wkc, offset);
                }
                ++position;
                header->address = createAddress(position, offset);
                break;
            }
            case Command::APWR:
            {
                if (position == 0)
                {
                    processWriteCommand(header, data, wkc, offset);
                }
                ++position;
                header->address = createAddress(position, offset);
                break;
            }
            case Command::APRW:
            {
                if (position == 0)
                {
                    processReadWriteCommand(header, data, wkc, offset);
                }
                ++position;
                header->address = createAddress(position, offset);
                break;
            }

            //************* Fixed Pos. ************//
            // A configured slave answers FPxx on its station address or, when set, its
            // station alias (ETG.1000.4 configured-address addressing).
            case Command::FPRD:
            {
                if (matchesConfiguredAddress(position))
                {
                    processReadCommand(header, data, wkc, offset);
                }
                break;
            }
            case Command::FPWR:
            {
                if (matchesConfiguredAddress(position))
                {
                    processWriteCommand(header, data, wkc, offset);
                }
                break;
            }
            case Command::FPRW:
            {
                if (matchesConfiguredAddress(position))
                {
                    processReadWriteCommand(header, data, wkc, offset);
                }
                break;
            }

            //************* Broadcast *************//
            // ETG.1000.4 5.4.x.4: every slave processes the datagram and increments ADP;
            // read data is bitwise-ORed into the frame, not copied over it.
            case Command::BRD:
            {
                processBroadcastReadCommand(header, data, wkc, offset);
                ++position;
                header->address = createAddress(position, offset);
                break;
            }
            case Command::BWR:
            {
                processWriteCommand(header, data, wkc, offset);
                ++position;
                header->address = createAddress(position, offset);
                break;
            }
            case Command::BRW:
            {
                processBroadcastReadWriteCommand(header, data, wkc, offset);
                ++position;
                header->address = createAddress(position, offset);
                break;
            }

            //************** Logical **************//
            case Command::LRD:  { processLRD(header, data, wkc); break; }
            case Command::LWR:  { processLWR(header, data, wkc); break; }
            case Command::LRW:  { processLRW(header, data, wkc); break; }

            //******** Auto Inc. Multiples ********//
            // Read-multiple-write (DC time distribution): the reference slave reads
            // its register into the frame, every other slave writes the frame value.
            case Command::ARMW:
            {
                if (position == 0)
                {
                    processReadCommand(header, data, wkc, offset);
                }
                else
                {
                    processWriteCommand(header, data, wkc, offset);
                }
                ++position;
                header->address = createAddress(position, offset);
                break;
            }
            case Command::FRMW:
            {
                if (matchesConfiguredAddress(position))
                {
                    processReadCommand(header, data, wkc, offset);
                }
                else
                {
                    processWriteCommand(header, data, wkc, offset);
                }
                break;
            }

            //*************** Misc. ***************//
            case Command::NOP:  { break; }
        }
    }


    void EmulatedESC::processInternalLogic()
    {
        static nanoseconds const start = now();

        // ESM state change
        if (memory_.al_control != memory_.al_status)
        {
            State before  = static_cast<State>(memory_.al_status  & 0xf);
            State current = static_cast<State>(memory_.al_control & 0xf);
            simu_info("%f  %s -> %s  \n", seconds_f(now() - start).count(), toString(before), toString(current));

            switch (current)
            {
                case State::BOOT: { break; }
                case State::INIT: { break; }
                case State::PRE_OP:
                {
                    if (before == State::INIT)
                    {
                        configureSMs();

                        seconds_f wdgPDI = pdiWatchdog();
                        seconds_f wdgPDO = pdoWatchdog();
                        simu_info("PDI watchdog: %04fs\n", wdgPDI.count());
                        simu_info("PDO watchdog: %04fs\n", wdgPDO.count());
                    }
                    break;
                }
                case State::SAFE_OP:
                {
                    if (before == State::PRE_OP)
                    {
                        configureFmmus();
                    }
                    break;
                }
                case State::OPERATIONAL: { break; }
                default: {}
            }
        }

        // Mirror AL_STATUS - Device Emulation
        if(memory_.esc_configuration & 0x01)
        {
            if (memory_.al_status & AL_STATUS_ERR_IND)
            {
                // Self-set error (watchdog drop): hold AL_STATUS until the master acks it,
                // otherwise the mirror would clear the fallback on the next tick.
                if (memory_.al_control & AL_CONTROL_ERR_ACK)
                {
                    memory_.al_status = memory_.al_control & static_cast<uint16_t>(~AL_CONTROL_ERR_ACK);
                }
            }
            else
            {
                memory_.al_status = memory_.al_control;
            }
        }

        // Handle eeprom access. Command is in bits [10:8] only: a conformant master
        // sets WR_EN (bit 0) alongside WRITE, so matching on bit 0 too drops writes.
        uint16_t order = memory_.eeprom_control & eeprom::Control::COMMAND;
        switch (order)
        {
            case eeprom::Control::READ:
            {
                if (memory_.eeprom_address + 2 <= eeprom_.size())
                {
                    std::memcpy((void*)&memory_.eeprom_data, eeprom_.data() + memory_.eeprom_address, 4);
                }
                else
                {
                    // Eeprom address is out of bound: return the default value of an unwritten eeprom.
                    // This is a shortcut for emulation, real device should handle the eeprom size.
                    memory_.eeprom_data = UINT64_MAX;
                }
                memory_.eeprom_control &= ~0x0700; // clear order
                break;
            }
            case eeprom::Control::WRITE:
            {
                if (not (memory_.eeprom_control & eeprom::Control::WR_EN))
                {
                    // Datasheet 0x0502[14]: write command without write enable
                    memory_.eeprom_control &= ~eeprom::Control::COMMAND;
                    memory_.eeprom_control |= eeprom::Control::ERROR_WR_EN;
                    break;
                }

                if (memory_.eeprom_address >= eeprom_.size())
                {
                    // Increase eeprom size to let the write be done properly
                    // This is a shortcut for emulation, real device should handle the eeprom size.
                    // One word past the address (word-addressed); fill mimics an unwritten eeprom.
                    eeprom_.resize(std::size_t(memory_.eeprom_address) + 1, 0xFFFF);
                }

                memory_.eeprom_control &= ~0x0700; // clear order
                if (elapsed_time(last_write_eeprom_) < 2ms)
                {
                    memory_.eeprom_control |= eeprom::Control::BUSY; // esc EEPROM interface busy
                }

                if (elapsed_time(last_write_eeprom_) < 4ms)
                {
                    memory_.eeprom_control |= eeprom::Control::ERROR_CMD;// wait EEPROM acknowledge
                }
                else
                {
                    last_write_eeprom_ = now();
                    std::memcpy(eeprom_.data() + memory_.eeprom_address, &memory_.eeprom_data, 2);
                    memory_.eeprom_control &= ~0x0700; // clear order
                    memory_.eeprom_control &= ~eeprom::Control::WR_EN; // self-clearing once the write is done
                }

                break;
            }
            case eeprom::Control::NOP:
            {
                memory_.eeprom_control &= ~(eeprom::Control::ERROR_CMD | eeprom::Control::ERROR_WR_EN);
                break;
            }
            case eeprom::Control::RELOAD:
            {
                // Re-apply the EEPROM-derived configuration to the registers (station
                // alias, ESC configuration), as a real ESC does on a Reload command.
                loadEeprom();
                memory_.eeprom_control &= ~0x0700; // clear order
                break;
            }
        }

        checkWatchdog();
    }


    void EmulatedESC::processReadCommand(DatagramHeader* header, void* data, uint16_t* wkc, uint16_t offset)
    {
        dcSystemTimeRead(offset, header->len);
        int32_t read = computeInternalMemoryAccess(offset, data, header->len, Access::ECAT_READ);
        if (read > 0)
        {
            *wkc += 1;
        }
    }


    void EmulatedESC::processWriteCommand(DatagramHeader* header, void* data, uint16_t* wkc, uint16_t offset)
    {
        int32_t written = computeInternalMemoryAccess(offset, data, header->len, Access::ECAT_WRITE);
        if (written > 0)
        {
            *wkc += 1;
            dcTimeLoopWrite(offset, header->len);
        }
    }


    int32_t EmulatedESC::readOrIntoFrame(uint16_t offset, void* data, uint16_t size)
    {
        uint8_t buffer[MAX_ETHERCAT_PAYLOAD_SIZE];
        dcSystemTimeRead(offset, size);
        int32_t read = computeInternalMemoryAccess(offset, buffer, size, Access::ECAT_READ);
        uint8_t* frame = static_cast<uint8_t*>(data);
        for (int32_t i = 0; i < read; ++i)
        {
            frame[i] |= buffer[i];
        }
        return read;
    }


    void EmulatedESC::processBroadcastReadCommand(DatagramHeader* header, void* data, uint16_t* wkc, uint16_t offset)
    {
        if (readOrIntoFrame(offset, data, header->len) > 0)
        {
            *wkc += 1;
        }
    }


    void EmulatedESC::processBroadcastReadWriteCommand(DatagramHeader* header, void* data, uint16_t* wkc, uint16_t offset)
    {
        // ETG.1000.4 5.4.3.4: the data as received is written, the memory content
        // before the write is ORed into the frame.
        uint8_t swap[MAX_ETHERCAT_PAYLOAD_SIZE];
        std::memcpy(swap, data, header->len);

        int32_t read    = readOrIntoFrame(offset, data, header->len);
        int32_t written = computeInternalMemoryAccess(offset, swap, header->len, Access::ECAT_WRITE);

        if (read > 0)
        {
            *wkc += 1;
        }
        if (written > 0)
        {
            *wkc += 2;
            dcTimeLoopWrite(offset, header->len);
        }
    }


    void EmulatedESC::processReadWriteCommand(DatagramHeader* header, void* data, uint16_t* wkc, uint16_t offset)
    {
        uint8_t swap[MAX_ETHERCAT_PAYLOAD_SIZE];
        std::memcpy(swap, data, header->len);

        dcSystemTimeRead(offset, header->len);
        int32_t read    = computeInternalMemoryAccess(offset, data, header->len, Access::ECAT_READ);    // read directly to the frame
        int32_t written = computeInternalMemoryAccess(offset, swap, header->len, Access::ECAT_WRITE);   // write from the swap

        if (read > 0)
        {
            *wkc += 1;
        }
        if (written > 0)
        {
            *wkc += 2;
            dcTimeLoopWrite(offset, header->len);
        }
    }


    void EmulatedESC::configureSMs()
    {
        syncs_.clear();
        for (auto& sm : memory_.sync_manager)
        {
            // Check activated /
            if (not sm.activate)
            {
                continue;
            }

            // Check mode: keep only one buffered mode  (triple buffered are handled through FMMU)
            if (not (sm.control & 0x3))
            {
                continue;
            }

            SM sync;
            sync.registers = &sm;
            sync.address = sm.start_address;
            sync.size = sm.length;

            // Save access rights
            if (sm.control & 0x4)
            {
                sync.access = (ECAT_WRITE | PDI_READ);
            }
            else
            {
                sync.access = (ECAT_READ | PDI_WRITE);
            }

            syncs_.push_back(sync);
        }
    }


    void EmulatedESC::configureFmmus()
    {
        fmmus_.clear();
        has_output_fmmu_ = false;
        for (int i = 0; i < 16; ++i)
        {
            auto const& fmmu = memory_.fmmu[i];
            if (fmmu.activate == 0)
            {
                continue;
            }
            if ((fmmu.type != 1) and (fmmu.type != 2))
            {
                continue;
            }
            if (fmmu.logical_stop_bit < fmmu.logical_start_bit)
            {
                continue;  // malformed: the unsigned span below would wrap to a huge bit_length
            }

            uint32_t const bit_length = (fmmu.length - 1u) * 8u + (fmmu.logical_stop_bit - fmmu.logical_start_bit + 1u);

            // Reject a span that runs past the flat memory map (a non-zero physical start bit
            // can push the last byte one past `length`), else logical access dereferences OOB.
            uint32_t const physical_bytes = (fmmu.physical_start_bit + bit_length + 7u) / 8u;
            if (static_cast<std::size_t>(fmmu.physical_address) + physical_bytes > sizeof(memory_))
            {
                continue;
            }

            Fmmu f;
            f.logical_address    = fmmu.logical_address;
            f.physical           = reinterpret_cast<uint8_t*>(&memory_) + fmmu.physical_address;
            f.bit_length         = bit_length;
            f.logical_start_bit  = fmmu.logical_start_bit;
            f.physical_start_bit = fmmu.physical_start_bit;
            f.is_input           = (fmmu.type == 1);
            if (not f.is_input)
            {
                has_output_fmmu_ = true;
            }
            fmmus_.push_back(f);
        }
        lastLogicalWrite_ = now();  // restart the output watchdog window at PDO (re)config
    }


    nanoseconds EmulatedESC::pdiWatchdog()
    {
        nanoseconds divider = (memory_.watchdog_divider + 2) * 40ns;
        return memory_.watchdog_time_pdi * divider;
    }


    nanoseconds EmulatedESC::pdoWatchdog()
    {
        nanoseconds divider = (memory_.watchdog_divider + 2) * 40ns;
        return memory_.watchdog_time_process_data * divider;
    }


    void EmulatedESC::checkWatchdog()
    {
        if (not has_output_fmmu_)
        {
            return; // no outputs: the process-data watchdog has nothing to monitor
        }
        nanoseconds delay = pdoWatchdog();
        if (delay == 0ns)
        {
            return; // watchdog deactivated
        }
        
        auto current = now(); // Create the current time
        if (current < (lastLogicalWrite_ + delay)) // If the current time is before the last valid PDO write plus the delay
        {
            memory_.watchdog_status_process_data = 1; // Watchdog is healthy
        } 
        else
        {  // Watchdog EXPIRED
            if (memory_.watchdog_status_process_data == 1) // Checks if the watchdog was previously OK
            {
                memory_.watchdog_status_process_data = 0; // Watchdog expired
                if (memory_.watchdog_counter_process_data < 0xFF)
                {
                    memory_.watchdog_counter_process_data++; // Counter of how many times the watchdog has expired
                }
                // Real ESCs never write AL_CONTROL (0x120, master-owned). Without a PDI
                // application (device emulation) the ESC drops AL_STATUS itself; otherwise
                // the application observes WDOG_STATUS and performs the fallback.
                if (memory_.esc_configuration & 0x01)
                {
                    memory_.al_status = (memory_.al_status & 0xFFF0) | State::SAFE_OP | AL_STATUS_ERR_IND;
                    memory_.al_status_code = SYNC_MANAGER_WATCHDOG; // SM Watchdog code
                }
            }
        }
    }
}
