
#include "kickcat/EEPROM/XMC4800EEPROM.h"
#include "kickcat/Time.h"
#include "kickcat/ESC/XMC4800.h"

#include "kickcat/Mailbox.h"

#include <cstring>

int main(int, char *[])
{
    using namespace kickcat;

    printf("\n\n\n\n XMC hello relax\n");

    XMC4800 esc;
    XMC4800EEPROM eeprom;

    esc.init();
    eeprom.init();
    uint16_t al_status;
    uint16_t al_control;
    esc.read(reg::AL_STATUS, &al_status, sizeof(al_status));
    esc.read(reg::AL_CONTROL, &al_control, sizeof(al_control));
    printf("al_status %x al_control %x  \n", al_status, al_control);


    constexpr uint32_t pdo_size_in = 28;
    constexpr uint32_t pdo_size_out = 2;
    uint8_t buffer_in[pdo_size_in];
    uint8_t buffer_out[pdo_size_out];

    // init values
    for (uint32_t i=0; i < pdo_size_in; ++i)
    {
        buffer_in[i] = 3*i;
    }
    buffer_out[0] = 0xFF;
    buffer_out[1] = 0xFF;

    SyncManagerConfig process_data_out = SYNC_MANAGER_PI_OUT(2, 0x1600, pdo_size_out); // Process data out (master view), address consistent with eeprom conf.
    SyncManagerConfig process_data_in = SYNC_MANAGER_PI_IN(3, 0x1800, pdo_size_in); // Process data in (master view), address consistent with eeprom conf.

    esc.set_process_data_input(buffer_in, process_data_in);
    esc.set_process_data_output(buffer_out, process_data_out);

    uint8_t esc_config;
    esc.read(reg::ESC_CONFIG, &esc_config, sizeof(esc_config));
    bool is_emulated = esc_config & PDI_EMULATION;
    printf("esc config 0x%x, is emulated %i \n", esc_config, is_emulated);

    uint8_t pdi_config;
    esc.read(reg::PDI_CONFIGURATION, &pdi_config, sizeof(pdi_config));
    printf("pdi config 0x%x \n", pdi_config);


    auto const mbx_out_cfg = SYNC_MANAGER_MBX_OUT(0, 0x1000, 128);
    auto const mbx_in_cfg  = SYNC_MANAGER_MBX_IN (1, 0x1400, 128);
    esc.set_mailbox_config({{mbx_out_cfg, mbx_in_cfg}});
    mailbox::response::Mailbox mbx(&esc, mbx_out_cfg, mbx_in_cfg);
    // mbx.enableCoE();

    std::vector<uint8_t> msg;

    while(true)
    {


        if (esc.al_status() == State::PRE_OP)
        {
            msg.resize(128);
            mbx.replyError(std::move(msg), mailbox::Error::NO_MORE_MEMORY);
            mbx.send();
            // SyncManager sm_recv;
            // esc.read(reg::SYNC_MANAGER, &sm_recv, sizeof(sm_recv));
            // // printf("SM 0  RECV %i: start address %x, length %u, control %x, status %x, activate %x, pdi_control %x \n", 0, sm_recv.start_address, sm_recv.length, sm_recv.control, sm_recv.status, sm_recv.activate, sm_recv.pdi_control);

            SyncManager sm_send;
            esc.read(reg::SYNC_MANAGER + 8, &sm_send, sizeof(sm_send));
            sm_send.pdi_control = 0;
            esc.write(reg::SYNC_MANAGER + 8, &sm_send, sizeof(sm_send));
            printf("SM 1 SEND %i: start address %x, length %u, control %x, status %x, activate %x, pdi_control %x \n",
               1, sm_send.start_address, sm_send.length, sm_send.control, sm_send.status, sm_send.activate, sm_send.pdi_control);

            // if (not (sm_send.status & 0x8))
            // {
            //     int32_t written_bytes = esc.write(start_address_in, &msg, sizeof(msg));
            //     printf("Written bytes %i\n", written_bytes);
            // }
        }

        // sleep(100ms);
        eeprom.process();
        esc.routine();
    //     if (esc.al_status() & State::SAFE_OP)
    //     {
    //         if (buffer_out[1] != 0xFF)
    //         {
    //            esc.set_valid_output_data_received(true);
    //         }
    //     }
    }
    return 0;
}
