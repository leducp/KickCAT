#include "Prints.h"

namespace kickcat
{
    void printInfo(Slave const& slave)
    {
        printf("%s", slave.getInfo().c_str());
    }

    void printPDOs(Slave const& slave)
    {
        printf("%s", slave.getPDOs().c_str());
    }

    void printErrorCounters(Slave const& slave)
    {
        printf("\n -*-*-*-*- slave %u -*-*-*-*-\n %s", slave.address, toString(slave.error_counters).c_str());
    }

    void printDLStatus(Slave const& slave)
    {
        printf("\n -*-*-*-*- slave %u -*-*-*-*-\n", slave.address);
        printf("Port 0: \n");
        printf("  Physical Link :  %d \n", slave.dl_status.PL_port0);
        printf("  Communications : %d \n", slave.dl_status.COM_port0);
        printf("  Loop Function :  %d \n", slave.dl_status.LOOP_port0);

        printf("Port 1: \n");
        printf("  Physical Link :  %d \n", slave.dl_status.PL_port1);
        printf("  Communications : %d \n", slave.dl_status.COM_port1);
        printf("  Loop Function :  %d \n", slave.dl_status.LOOP_port1);

        printf("Port 2: \n");
        printf("  Physical Link :  %d \n", slave.dl_status.PL_port2);
        printf("  Communications : %d \n", slave.dl_status.COM_port2);
        printf("  Loop Function :  %d \n", slave.dl_status.LOOP_port2);

        printf("Port 3: \n");
        printf("  Physical Link :  %d \n", slave.dl_status.PL_port3);
        printf("  Communications : %d \n", slave.dl_status.COM_port3);
        printf("  Loop Function :  %d \n", slave.dl_status.LOOP_port3);
    }

    void printGeneralEntry(Slave const& slave) 
    {
        eeprom::GeneralEntry const* general_entry = slave.sii.general;
        
        if (general_entry == nullptr)
        {
            printf("Uninitialized SII - Nothing to print");
        }
        else
        {
            printf( "group_info_id: %i \n",             general_entry->group_info_id);
            printf( "image_name_id: %i \n",             general_entry->image_name_id);
            printf( "device_order_id: %i \n",           general_entry->device_order_id);
            printf( "device_name_id: %i \n",            general_entry->device_name_id);
            printf( "reserved_A: %i \n",                general_entry->reserved_A);
            printf( "FoE_details: %i \n",               general_entry->FoE_details);
            printf( "EoE_details: %i \n",               general_entry->EoE_details);
            printf( "SoE_channels: %i \n",              general_entry->SoE_channels);
            printf( "DS402_channels: %i \n",            general_entry->DS402_channels);
            printf( "SysmanClass: %i \n",               general_entry->SysmanClass);
            printf( "flags: %i \n",                     general_entry->flags);
            printf( "current_on_ebus: %i \n",           general_entry->current_on_ebus); // mA, negative means feeding current
            printf( "group_info_id_dup: %i \n",         general_entry->group_info_id_dup);
            printf( "reserved_B: %i \n",                general_entry->reserved_B);
            printf( "physical_memory_address: %i \n",   general_entry->physical_memory_address);
            printf( "reserved_C[12]: %i \n",            general_entry->reserved_C[12]);
            printf( "port_0: %04x \n",                  general_entry->port_0);
            printf( "port_1: %04x \n",                  general_entry->port_1);
            printf( "port_2: %04x \n",                  general_entry->port_2);
            printf( "port_3: %04x \n",                  general_entry->port_3);
        }
    }


    void printTopology(std::unordered_map<uint16_t, uint16_t> const& topology_mapping)
    {  
        printf( "\n -*-*-*-*- Topology -*-*-*-*-\n" );
        for (auto const& it : topology_mapping)
        {
            if (it.first != it.second)
            {
                printf( "Slave %04x parent : slave %04x \n", it.first, it.second);
            }
            else
            {
                printf( "Slave %04x parent : master \n", it.first);
            }
        }
    }


    void printDatagramHeader(DatagramHeader const& header)
    {
        printf("Header \n");
        printf("  Command :  %d \n", static_cast<uint8_t>(header.command));
        printf("  index :  %d \n", header.index);
        printf("  length :  %d \n", header.len);
        printf("  circulating %d \n", header.circulating);
        printf("  multiple %d \n", header.multiple);
        printf("  IRQ %d \n", header.IRQ);
    }
}
