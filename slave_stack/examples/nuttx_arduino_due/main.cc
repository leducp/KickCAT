
#include "nuttx/SPI.h"
#include "kickcat/ESC/Lan9252.h"

using namespace kickcat;

void reportError(hresult const& rc)
{
    if (rc != hresult::OK)
    {
        printf("\nERROR: %s code %u\n", toString(rc), rc);
    }
}


SyncManager sm_configs[2];  // Global to tests validation functions, to be passed at init.



bool is_valid_sm(AbstractESC& esc, SyncManager const& sm_ref, uint8_t sm_index)
{
    auto create_sm_address = [](uint16_t reg, uint16_t sm_index)
    {
        return reg + sm_index * 8;
    };

    SyncManager sm_read;

    reportError(esc.read(create_sm_address(0x0800, sm_index), &sm_read, sizeof(sm_read)));

    bool is_valid = (sm_read.start_address == sm_ref.start_address) and
                    (sm_read.length == sm_ref.length) and
                    (sm_read.control == sm_ref.control);


    printf("SM %i: start address %x, length %u, control %x status %x, activate %x \n", sm_index, sm_read.start_address, sm_read.length, sm_read.control, sm_read.status, sm_read.activate);


    return is_valid;
}


void esc_routine(Lan9252& esc)
{
    uint32_t nb_bytes = 32;
    uint8_t test_write[nb_bytes];
    for (uint32_t i=0; i < nb_bytes; ++i)
    {
        test_write[i] = i;
    }
    reportError(esc.write(0x1200, &test_write, nb_bytes));

    uint16_t al_status;
    uint16_t al_control = 0;
    reportError(esc.read(AL_CONTROL, &al_control, sizeof(al_control)));
    reportError(esc.read(AL_STATUS, &al_status, sizeof(al_status)));
    bool watchdog = false;
    reportError(esc.read(WDOG_STATUS, &watchdog, 1));

    if (al_control & ESM_INIT)
    {
        al_status = ESM_INIT;
    }

    // TODO better filter AL status based on error, id and reserved bits.

    // ETG 1000.6 Table 99 – Primitives issued by ESM to Application
    switch (al_status)
    {
        case ESM_INIT:
        {
            uint16_t mailbox_protocol;
            reportError(esc.read(MAILBOX_PROTOCOL, &mailbox_protocol, sizeof(mailbox_protocol)));
            printf("Mailbox protocol %x \n", mailbox_protocol);

            if (mailbox_protocol != MailboxProtocol::None)
            {
                // TODO check mailbox conf SM; "SM_SETTINGS_0_1_MATCH", check SM mailbox settings, SM 0 or 1
            }

            // TODO AL_CONTROL device identification flash led 0x0138 RUN LED Override

            if (al_control & ESM_PRE_OP)
            {
                al_status = ESM_PRE_OP;
            }

            break;
        }

        case ESM_PRE_OP:
        {
            // check process data SM

            if (al_control & ESM_SAFE_OP)
            {
                if (is_valid_sm(esc, sm_configs[0], 0) and is_valid_sm(esc, sm_configs[1], 1))
                {
                    al_status = ESM_SAFE_OP;
                }
                else
                {
                    // set error flag ?
                }
            }
            break;
        }

        case ESM_SAFE_OP:
        {
            if (al_control & ESM_OP)
            {
                al_status = ESM_OP;
            }
            break;
        }

        case ESM_OP:
        {

            is_valid_sm(esc, sm_configs[0], 0);
            is_valid_sm(esc, sm_configs[1], 1);
            break;
        }
        default:
        {
            printf("Unknown or error al_status %x \n", al_status);
        }
    }


    reportError(esc.write(AL_STATUS, &al_status, sizeof(al_status)));
    printf("al_status %x, al_control %x \n", al_status, al_control);

//    // Print received data (slow down the execution)
//    if ((al_status & ESM_OP) and watchdog)
//    {
//        uint8_t test_read[nb_bytes];
//        reportError(esc.read(0x1000, &test_read, nb_bytes));
//        for (uint32_t i=0; i < nb_bytes; i++)
//        {
//            printf("%x ", test_read[i]);
//        }
//        printf(" received\n");
//    }
}

int main(int argc, char *argv[])
{
    SPI spi_driver{};
    Lan9252 esc(spi_driver);

    reportError(esc.init());

    sm_configs[0].start_address = 0x1000;
    sm_configs[0].length = 32;
    sm_configs[0].control = 0x64;


    sm_configs[1].start_address = 0x1200;
    sm_configs[1].length = 32;
    sm_configs[1].control = 0x20;

//    // How to test esc accesses.

//    uint16_t al_status;
//    reportError(esc.read(AL_STATUS, &al_status, sizeof(al_status)));
//    printf("Al status %x \n", al_status);

//    uint16_t station_alias;
//    reportError(esc.read(0x0012, &station_alias, sizeof(station_alias)));
//    printf("before write station_alias %x \n", station_alias);
//
//    station_alias = 0xCAFE;
//    reportError(esc.write(0x0012, &station_alias, sizeof(station_alias)));
//    printf("Between read station alias \n");
//    reportError(esc.read(0x0012, &station_alias, sizeof(station_alias)));
//    printf("after station_alias %x \n", station_alias);

    while(true)
    {
        esc_routine(esc);
        usleep(1000);
    }

    return 0;
}
