#include "kickcat/ESC/EmulatedESC.h"
#include "kickcat/OS/Time.h"
#include "kickcat/debug.h"

#include <cstring>
#include <fstream>

namespace kickcat
{
    EmulatedESC::EmulatedESC(std::string const& eeprom)
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

        // Device emulation is ON
        memory_.esc_configuration = 0x01;

        // Default value is 'Request INIT State'
        memory_.al_control = State::INIT;

        // Default value is 'INIT State'
        memory_.al_status = State::INIT;

        // eeprom never busy because the data are processed in sync with the request.
        memory_.eeprom_control &= ~0x8000;

        // Load eeprom data
        std::ifstream eeprom_file;
        eeprom_file.open(eeprom, std::ios::binary | std::ios::ate);
        if (not eeprom_file.is_open())
        {
            THROW_ERROR("Cannot load EEPROM");
        }
        int size = eeprom_file.tellg();
        eeprom_file.seekg (0, std::ios::beg);
        eeprom_.resize(size / 2); // vector of uint16_t so / 2 since the size is in byte
        eeprom_file.read((char*)eeprom_.data(), size);
        eeprom_file.close();

        memory_.station_alias = eeprom_[4]; // fourth word of eeprom, at first load
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
                        if (not (sync.registers->status & MAILBOX_STATUS) and (sync.registers->control & 0x3))
                        {
                            return -1; // Cannot read mailbox: it is empty
                        }
                        std::memcpy(buffer, pos, to_copy);

                        if ((address + size - 1) == (sync.address + sync.size - 1))
                        {
                            // Last byte read -> access is done and mailbox is now empty
                            sync.registers->status &= ~MAILBOX_STATUS;
                        }
                        return to_copy;
                    }
                    case PDI_WRITE:
                    case ECAT_WRITE:
                    {
                        if ((sync.registers->status & MAILBOX_STATUS) and (sync.registers->control & 0x3))
                        {
                            return -1; // Cannot write mailbox: it is full
                        }
                        std::memcpy(pos, buffer, to_copy);

                        if ((address + size - 1) == (sync.address + sync.size - 1))
                        {
                            // Last byte written -> access is done and mailbox is now full
                            sync.registers->status |= MAILBOX_STATUS;
                        }
                        return to_copy;
                    }
                }
            }

            if (access & Access::PDI_READ)
            {
                for (auto& pdo : rx_pdos_)
                {
                    if ((pos < pdo.physical_address) or ((pos + to_copy) > (pdo.physical_address + pdo.size)))
                    {
                        continue;
                    }
                    std::memcpy(buffer, pos, to_copy);
                    return to_copy;
                }
            }

            if (access & Access::PDI_WRITE)
            {
                for (auto& pdo : tx_pdos_)
                {
                    if ((pos < pdo.physical_address) or ((pos + to_copy) > (pdo.physical_address + pdo.size)))
                    {
                        continue;
                    }
                    std::memcpy(pos, buffer, to_copy);
                    return to_copy;
                }
            }

            // No SM nor FFMU enable this access
            return -1;
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

        return -1; // shall not be possible to be reached
    }


    int32_t EmulatedESC::write(uint16_t address, void const* data, uint16_t size)
    {
        return computeInternalMemoryAccess(address, const_cast<void*>(data), size, Access::PDI_WRITE);
    }

    int32_t EmulatedESC::read(uint16_t address, void* data, uint16_t size)
    {
        return computeInternalMemoryAccess(address, data, size, Access::PDI_READ);
    }


    void EmulatedESC::processDatagram(DatagramHeader* header, uint8_t* data, uint16_t* wkc)
    {
        processEcatRequest(header, data, wkc);
        processInternalLogic();
    }


    uint16_t EmulatedESC::processPDO(std::vector<PDO> const& pdos, bool read, DatagramHeader* header, uint8_t* data)
    {
        int wkc = 0;
        for (auto const& pdo : pdos)
        {
            auto[frame, internal, to_copy] = computeLogicalIntersection(header, data, pdo);
            if (to_copy == 0)
            {
                continue;
            }

            if (read)
            {
                std::memcpy(frame, internal, to_copy);
            }
            else
            {
                std::memcpy(internal, frame, to_copy);
                lastLogicalWrite_ = since_epoch();   // update watchdog
            }
            ++wkc;
        }
        return wkc;
    }


    void EmulatedESC::processLRD(DatagramHeader* header, uint8_t* data, uint16_t* wkc)
    {
        *wkc += processPDO(tx_pdos_, true, header, data);
    }

    void EmulatedESC::processLWR(DatagramHeader* header, uint8_t* data, uint16_t* wkc)
    {
        *wkc += processPDO(rx_pdos_, false, header, data);
    }

    void EmulatedESC::processLRW(DatagramHeader* header, uint8_t* data, uint16_t* wkc)
    {
        uint8_t swap[MAX_ETHERCAT_PAYLOAD_SIZE];
        std::memcpy(swap, data, header->len);

        *wkc += processPDO(tx_pdos_, true,  header, data);      // read directly in the frame
        *wkc += processPDO(rx_pdos_, false, header, swap) * 2;  // write from the swap
    }


    void EmulatedESC::processEcatRequest(DatagramHeader* header, uint8_t* data, uint16_t* wkc)
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
            case Command::FPRD:
            {
                if (position == memory_.station_address)
                {
                    processReadCommand(header, data, wkc, offset);
                }
                break;
            }
            case Command::FPWR:
            {
                if (position == memory_.station_address)
                {
                    processWriteCommand(header, data, wkc, offset);
                }
                break;
            }
            case Command::FPRW:
            {
                if (position == memory_.station_address)
                {
                    processReadWriteCommand(header, data, wkc, offset);
                }
                break;
            }

            //************* Broadcast *************//
            case Command::BRD:  { processReadCommand     (header, data, wkc, offset); break; }
            case Command::BWR:  { processWriteCommand    (header, data, wkc, offset); break; }
            case Command::BRW:  { processReadWriteCommand(header, data, wkc, offset); break; }

            //************** Logical **************//
            case Command::LRD:  { processLRD(header, data, wkc); break; }
            case Command::LWR:  { processLWR(header, data, wkc); break; }
            case Command::LRW:  { processLRW(header, data, wkc); break; }

            //******** Auto Inc. Multiples ********//
            case Command::ARMW: { break; }
            case Command::FRMW: { break; }

            //*************** Misc. ***************//
            case Command::NOP:  { break; }
        }
    }


    void EmulatedESC::processInternalLogic()
    {
        static nanoseconds const start = since_epoch();

        // ESM state change
        if (memory_.al_control != memory_.al_status)
        {
            State before  = static_cast<State>(memory_.al_status  & 0xf);
            State current = static_cast<State>(memory_.al_control & 0xf);
            simu_info("%f  %s -> %s  \n", seconds_f(since_epoch() - start).count(), toString(before), toString(current));

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
                        configurePDOs();
                    }
                    break;
                }
                case State::OPERATIONAL: { break; }
                default: {}
            }
            changeState_(before, current);
        }

        // Mirror AL_STATUS - Device Emulation
        memory_.al_status = memory_.al_control;

        // Handle eeprom access
        uint16_t order = memory_.eeprom_control & 0x0701;
        switch (order)
        {
            case eeprom::Control::READ:
            {
                std::memcpy((void*)&memory_.eeprom_data, eeprom_.data() + memory_.eeprom_address, 4);
                memory_.eeprom_control &= ~0x0700; // clear order
                break;
            }
            case eeprom::Control::WRITE:
            {
                memory_.eeprom_control &= ~0x0700; // clear order
                if (elapsed_time(last_write_eeprom_) < 2ms)
                {
                    memory_.eeprom_control |= 0x8000; // esc EEPROM interface busy
                }

                if (elapsed_time(last_write_eeprom_) < 4ms)
                {
                    memory_.eeprom_control |= 0x2000;// wait EEPROM acknowledge
                }
                else
                {
                    last_write_eeprom_ = since_epoch();
                    std::memcpy(eeprom_.data() + memory_.eeprom_address, &memory_.eeprom_data, 2);
                    memory_.eeprom_control &= ~0x0700; // clear order
                }

                break;
            }
            case eeprom::Control::NOP:
            case eeprom::Control::RELOAD:
            {
                break;
            }
        }

        checkWatchdog();
    }


    void EmulatedESC::processReadCommand(DatagramHeader* header, uint8_t* data, uint16_t* wkc, uint16_t offset)
    {
        int32_t read = computeInternalMemoryAccess(offset, data, header->len, Access::ECAT_READ);
        if (read > 0)
        {
            *wkc += 1;
        }
    }


    void EmulatedESC::processWriteCommand(DatagramHeader* header, uint8_t* data, uint16_t* wkc, uint16_t offset)
    {
        int32_t written = computeInternalMemoryAccess(offset, data, header->len, Access::ECAT_WRITE);
        if (written > 0)
        {
            *wkc += 1;
        }
    }


    void EmulatedESC::processReadWriteCommand(DatagramHeader* header, uint8_t* data, uint16_t* wkc, uint16_t offset)
    {
        uint8_t swap[MAX_ETHERCAT_PAYLOAD_SIZE];
        std::memcpy(swap, data, header->len);

        int32_t read    = computeInternalMemoryAccess(offset, data, header->len, Access::ECAT_READ);    // read directly to the frame
        int32_t written = computeInternalMemoryAccess(offset, swap, header->len, Access::ECAT_WRITE);   // write from the swap

        if (read > 0)
        {
            *wkc += 1;
        }
        if (written > 0)
        {
            *wkc += 2;
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


    void EmulatedESC::configurePDOs()
    {
        tx_pdos_.clear();
        rx_pdos_.clear();
        for (int i = 0; i < 16; ++i)
        {
            auto const& fmmu = memory_.fmmu[i];
            if (fmmu.activate == 0)
            {
                continue;
            }

            PDO pdo;
            pdo.size = fmmu.length;
            pdo.logical_address = fmmu.logical_address;
            pdo.physical_address = memory_.process_data_ram + (fmmu.physical_address - 0x1000);

            if (fmmu.type == 1)
            {
                tx_pdos_.push_back(pdo);
            }
            else if (fmmu.type == 2)
            {
                rx_pdos_.push_back(pdo);
            }
            else
            {
                continue;
            }
        }
    }


    std::tuple<uint8_t*, uint8_t*, uint16_t> EmulatedESC::computeLogicalIntersection(DatagramHeader const* header, uint8_t* data, PDO const& pdo)
    {
        uint32_t start_logical_address = header->address;
        uint32_t end_logical_address = header->address + header->len;

        uint32_t address_min = std::max(start_logical_address, pdo.logical_address);
        uint32_t address_max = std::min(end_logical_address, pdo.logical_address + pdo.size);
        if (address_max < address_min)
        {
            return {nullptr, nullptr, 0};
        }
        uint32_t to_copy = address_max - address_min;
        uint32_t phys_offset = address_min - pdo.logical_address;
        uint32_t frame_offset = address_min - start_logical_address;

        return {data + frame_offset, pdo.physical_address + phys_offset, to_copy};
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
        // Warning: PDI watchdog not handled yet!

        nanoseconds delay = pdoWatchdog();
        if (delay == 0ns)
        {
            return; // watchdog deactivated
        }

        if ((lastLogicalWrite_ + delay) < since_epoch())
        {
            // Watchog OK
            memory_.watchdog_status_process_data = 0;
            return;
        }

        // Watchog Triggered - detect front
        if (memory_.watchdog_status_process_data == 0)
        {
            // Increase PDO wdg counter and reflect the current state
            memory_.watchdog_status_process_data = 1;

            if (memory_.watchdog_counter_process_data < 0xFF)
            {
                memory_.watchdog_counter_process_data++;
            }
        }
    }
}
