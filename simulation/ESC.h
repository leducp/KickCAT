#ifndef KICKCAT_SIMULATION_ESC_H
#define KICKCAT_SIMULATION_ESC_H

#include "kickcat/protocol.h"
#include <vector>
#include <functional>

namespace kickcat
{
    class ESC
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
        ESC(std::string const& eeprom);

        void processDatagram(DatagramHeader* header, uint8_t* data, uint16_t* wkc);

        // Called when the ESM state change.
        /// \param before   ESM state before changement
        /// \param current  current ESM state
        void setChangeStateCallback(std::function<void(State before, State current)> callback);

        int32_t read (uint16_t address, void* data,       uint16_t size);
        int32_t write(uint16_t address, void const* data, uint16_t size);

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

            FMMU fmmu[16];
            uint8_t padding24[0x100];

            SyncManager sync_manager[16];
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
            SyncManager* registers;
        };
        std::vector<SM> syncs_;

        struct PDO
        {
            uint32_t logical_address;
            uint8_t* physical_address;
            uint16_t size;
        };
        std::vector<PDO> rx_pdos_;
        std::vector<PDO> tx_pdos_;

        std::function<void(State, State)> changeState_{[](State, State){}};

        void processEcatRequest(DatagramHeader* header, uint8_t* data, uint16_t* wkc);
        void processInternalLogic();

        void processReadCommand     (DatagramHeader* header, uint8_t* data, uint16_t* wkc, uint16_t offset);
        void processWriteCommand    (DatagramHeader* header, uint8_t* data, uint16_t* wkc, uint16_t offset);
        void processReadWriteCommand(DatagramHeader* header, uint8_t* data, uint16_t* wkc, uint16_t offset);

        void configureSMs();
        void configurePDOs();

        int32_t computeInternalMemoryAccess(uint16_t address, void* buffer, uint16_t size, Access access);
        std::tuple<uint8_t*, uint8_t*, uint16_t> computeLogicalIntersection(DatagramHeader const* header, uint8_t* data, PDO const& pdo);
    };
};

#endif
