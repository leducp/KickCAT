#ifndef KICKCAT_SLAVE_ESC_EMULATED_ESC_H
#define KICKCAT_SLAVE_ESC_EMULATED_ESC_H

#include <filesystem>

#include "kickcat/protocol.h"
#include "kickcat/AbstractESC.h"

namespace kickcat
{
    namespace fs = std::filesystem;

    class EmulatedESC final : public AbstractESC
    {
        // ESC access type
        enum Access
        {
            PDI_READ   = 0x01,
            PDI_WRITE  = 0x02,
            ECAT_READ  = 0x04,
            ECAT_WRITE = 0x08
        };

    public:
        EmulatedESC();
        EmulatedESC(fs::path const& eeprom_path);
        virtual ~EmulatedESC() = default;

        // Helpers to load the eeprom
        void loadEeprom(fs::path const& eeprom_path);
        void loadEeprom(std::vector<uint16_t> const& eeprom_data);
        void loadEeprom(std::vector<uint8_t> const& image);  // word-addressed; odd trailing byte zero-filled

        // Access from ECAT POV
        void processDatagram(DatagramHeader* header, void* data, uint16_t* wkc);

        // Access from the PDI POV
        int32_t read (uint16_t address, void* data,       uint16_t size) override;
        int32_t write(uint16_t address, void const* data, uint16_t size) override;

        // Per-ESC store-and-forward processing delay. The network routing engine
        // accumulates it along the physical path to produce DC port receive-times.
        nanoseconds forwardingDelay() const           { return forwarding_delay_; }
        void setForwardingDelay(nanoseconds delay)    { forwarding_delay_ = delay; }

        // Injectable local oscillator deviation in parts-per-million; the local clock
        // stays continuous across a change (accumulated drift is rebased, not dropped).
        void setClockDrift(double ppm);

        // Injectable zero-mean jitter on the reported 0x910 system time: each ECAT read
        // perturbs the value by a uniform pseudo-random offset in [-amplitude, +amplitude].
        // Models sampling/readout jitter distinct from the steady drift above; 0 disables.
        void setClockJitter(nanoseconds amplitude) { clock_jitter_ = amplitude; }

        // Local free-running clock for a given reference instant: reference time plus
        // the accumulated injected drift. No system time offset nor time-loop trim:
        // receive times latch local time, not system time (datasheet sec1 9.1.3).
        nanoseconds localClock(nanoseconds ref) const;

        // Local copy of the system time: local clock + system time offset (0x920)
        // + time-control-loop trim. This is the 0x910 view.
        nanoseconds localSystemTime() const;

    private:
        struct Memory
        {
            uint8_t type;
            uint8_t revision;
            uint16_t build;
            uint8_t ffmus_supported;
            uint8_t sync_managers_supported;
            uint8_t ram_size;
            uint8_t port_desc;
            uint16_t esc_features;
            uint8_t padding0[6];

            uint16_t station_address;
            uint16_t station_alias;
            uint8_t padding1[12];

            uint8_t write_enable;
            uint8_t write_protection;
            uint8_t padding2[14];

            uint8_t esc_write_enable;
            uint8_t esc_write_protection;
            uint8_t padding3[14];

            uint8_t reset_ecat;
            uint8_t reset_pdi;
            uint8_t padding4[190];

            uint32_t dl_control;
            uint8_t padding5[4];

            uint16_t physical_read_write_offset;
            uint8_t padding6[6];

            uint16_t dl_status;
            uint8_t padding7[14];

            uint16_t al_control;
            uint8_t padding8[14];
            uint16_t al_status;
            uint8_t padding9[2];
            uint16_t al_status_code;
            uint8_t padding10[2];

            uint8_t run_led_override;
            uint8_t err_led_override;
            uint8_t padding11[6];

            uint8_t pdi_control;
            uint8_t esc_configuration;
            uint8_t padding12[12];

            uint16_t pdi_information;
            uint32_t pdi_configuration;
            uint8_t padding13[172];

            uint16_t ecat_event_mask;
            uint8_t padding14[2];
            uint32_t pdi_al_event_mask;
            uint8_t padding15[8];
            uint16_t ecat_event_request;
            uint8_t padding16[14];
            uint32_t al_event_request;
            uint8_t padding17[220];

            uint64_t rx_error_counter;
            uint32_t forward_rx_error_counter;
            uint8_t ecat_pu_error_counter;
            uint8_t pdi_error_counter;
            uint16_t pdi_error_code;
            uint32_t lost_link_error_counter;
            uint8_t padding18[236];

            uint16_t watchdog_divider;
            uint8_t padding19[14];
            uint16_t watchdog_time_pdi;
            uint8_t padding20[14];
            uint16_t watchdog_time_process_data;
            uint8_t padding21[30];
            uint16_t watchdog_status_process_data;
            uint8_t watchdog_counter_process_data;
            uint8_t watchdog_counter_pdi;
            uint8_t padding22[188];

            uint8_t eeprom_configuration;
            uint8_t eeprom_pdi_access;
            uint16_t eeprom_control;
            uint32_t eeprom_address;
            uint64_t eeprom_data;

            uint16_t mii_control;
            uint8_t phy_address;
            uint8_t phy_register_address;
            uint16_t phy_data;
            uint8_t mii_ecat_access_state;
            uint8_t mii_pdi_access_state;
            uint32_t phy_port_status;
            uint8_t padding23[228];

            fmmu::Register fmmu[16];
            uint8_t padding24[0x100];

            SyncManager::Register sync_manager[16];
            uint8_t padding25[128];

            // DC
            uint8_t DC[0x100];
            uint8_t padding26[0x400];

            // ESC specific registers
            uint8_t esc_specific[0x100];

            uint32_t digital_io_output_data;
            uint8_t padding27[12];
            uint64_t gpo;
            uint64_t gpi;
            uint8_t padding28[96];

            uint8_t user_ram[128];

            uint8_t process_data_ram[0xf000];   // 60KB max following documentation
        }__attribute__((__packed__));

        Memory memory_;
        std::vector<uint16_t> eeprom_;      // EEPPROM addressing is word/16 bits

        struct SM
        {
            uint8_t access;
            uint16_t address;
            uint16_t size;
            SyncManager::Register* registers;
        };
        std::vector<SM> syncs_;

        // physical is a flat offset into the ESC memory map, so one FMMU list covers
        // both process-data RAM and register space (e.g. an SM mailbox-status bit).
        struct Fmmu
        {
            uint32_t logical_address;
            uint8_t* physical;
            uint32_t bit_length;
            uint8_t  logical_start_bit;
            uint8_t  physical_start_bit;
            bool     is_input;          // FMMU type 1: slave -> master
        };
        std::vector<Fmmu> fmmus_;
        bool has_output_fmmu_{false};   // process-data watchdog only applies when outputs exist

        void loadEeprom();

        void processEcatRequest(DatagramHeader* header, void* data, uint16_t* wkc);
        void processInternalLogic();

        void processReadCommand     (DatagramHeader* header, void* data, uint16_t* wkc, uint16_t offset);
        void processWriteCommand    (DatagramHeader* header, void* data, uint16_t* wkc, uint16_t offset);
        void processReadWriteCommand(DatagramHeader* header, void* data, uint16_t* wkc, uint16_t offset);

        // Broadcast reads OR the memory into the frame instead of overwriting it (ETG.1000.4).
        void processBroadcastReadCommand     (DatagramHeader* header, void* data, uint16_t* wkc, uint16_t offset);
        void processBroadcastReadWriteCommand(DatagramHeader* header, void* data, uint16_t* wkc, uint16_t offset);
        int32_t readOrIntoFrame(uint16_t offset, void* data, uint16_t size);

        // FPxx target: configured station address, or the station alias when set.
        bool matchesConfiguredAddress(uint16_t position) const;

        void processLRD(DatagramHeader* header, void* data, uint16_t* wkc);
        void processLWR(DatagramHeader* header, void* data, uint16_t* wkc);
        void processLRW(DatagramHeader* header, void* data, uint16_t* wkc);
        // Return true if any FMMU's logical range fell inside this datagram.
        bool processFmmus(bool read, DatagramHeader const* header, void* frame);
        bool copyFmmu(Fmmu const& fmmu, bool read, DatagramHeader const* header, void* frame);

        void configureSMs();
        void configureFmmus();

        // DC time machinery: both helpers self-filter on the accessed range, and are
        // only called from physically-addressed command paths, so the logical
        // (cyclic process data) path pays nothing.
        void dcSystemTimeRead(uint16_t offset, uint16_t size);  // refresh 0x910 before an ECAT read
        void dcTimeLoopWrite(uint16_t offset, uint16_t size);   // time control loop on ECAT writes

        int32_t computeInternalMemoryAccess(uint16_t address, void* buffer, uint16_t size, Access access);

        nanoseconds pdiWatchdog();  // Get configured PDI watchdog
        nanoseconds pdoWatchdog();  // Get configured PDO watchdog
        void checkWatchdog();
        nanoseconds lastLogicalWrite_{now()};

        nanoseconds last_write_eeprom_{now()};

        // Store-and-forward propagation delay: the time for a frame to pass from this
        // ESC to the next. Real (and, being a buffer model rather than cut-through,
        // potentially larger than real hardware) - independent of the host clock the
        // registers read from. The DC phase measures and compensates it; it must stay
        // the single source of truth shared with SYNC0/SYNC1 timing, else sync drifts.
        nanoseconds forwarding_delay_{300ns};

        // Local clock model: drift accumulates from drift_origin_ at clock_drift_ppm_;
        // dc_correction_ is the time-control-loop trim of the local copy of system time.
        double      clock_drift_ppm_{0.0};
        nanoseconds drift_origin_{since_ecat_epoch()};
        nanoseconds drift_accumulated_{0ns};
        nanoseconds dc_correction_{0ns};
        int64_t     dc_diff_filtered_{0};   // 0x92C mean-value filter state

        // Injected read-time jitter (setClockJitter) and its xorshift64 state. Mutable:
        // the RNG advances on every 0x910 read, which happens from a const-ish read path.
        nanoseconds      clock_jitter_{0ns};
        mutable uint64_t jitter_rng_{0x2545f4914f6cdd1dull};
    };

    std::vector<uint8_t> loadBinaryFile(fs::path const& path);
}

#endif
