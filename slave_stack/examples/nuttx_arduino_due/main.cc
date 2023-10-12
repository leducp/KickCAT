
#include "nuttx/SPI.h"
#include "kickcat/ESC/Lan9252.h"

using namespace kickcat;

void reportError(hresult const& rc)
{
    if (rc != hresult::OK)
    {
        printf("%s\n", toString(rc));
    }
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
    reportError(esc.read(AL_STATUS, &al_status, sizeof(al_status)));
    bool watchdog = false;
    reportError(esc.read(WDOG_STATUS, &watchdog, 1));

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
