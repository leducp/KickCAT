
#include "Arduino.h"

#include "kickcat/ESC/Lan9252.h"
#include <sys/time.h>

#include "arduino/spi.h"

using namespace kickcat;

std::unique_ptr<AbstractSPI> spi_driver(new spi());
Lan9252 esc(*spi_driver);

extern "C"
{
    int _gettimeofday( struct timeval *tv, void *tzvp )
    {
        uint32_t t = millis();  // get uptime in milliseconds
        tv->tv_sec = t / 1000;  // convert to seconds
        tv->tv_usec = ( t % 1000) * 1000;  // get remaining microseconds
        return 0;
    }
}

void reportError(hresult const& rc)
{
    if (rc != hresult::OK)
    {
        Serial.println(toString(rc));
    }
}

void setup()
{
  pinMode(LED_BUILTIN, OUTPUT);

  Serial.begin(9600);

  reportError(esc.init());



  uint16_t al_status;
  reportError(esc.read(AL_STATUS, &al_status, sizeof(al_status)));
  Serial.print("Al status ");
  Serial.println(al_status, HEX);

  uint16_t station_alias;
  reportError(esc.read(0x0012, &station_alias, sizeof(station_alias)));
  Serial.print("before write station_alias ");
  Serial.println(station_alias, HEX);

  station_alias = 0xCAFE;
  reportError(esc.write(0x0012, &station_alias, sizeof(station_alias)));
  Serial.println("Between read station alias");
  reportError(esc.read(0x0012, &station_alias, sizeof(station_alias)));
  Serial.print("after station_alias ");
  Serial.println(station_alias, HEX);
}

void esc_routine()
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

    if ((al_status & ESM_OP) and watchdog)
    {
        uint8_t test_read[nb_bytes];
        reportError(esc.read(0x1000, &test_read, nb_bytes));
//        for (uint32_t i=0; i < nb_bytes; i++)
//        {
//            Serial.print(test_read[i], HEX);
//            Serial.print(" ");
//        }
//        Serial.println(" received");
    }
}

void loop()
{
    delay(1);
    esc_routine();
}
