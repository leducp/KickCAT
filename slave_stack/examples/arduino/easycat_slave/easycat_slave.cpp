
#include "Arduino.h"

#include "kickcat/ESC/Lan9252.h"

using namespace kickcat;
Lan9252 esc;



void setup() {
  pinMode(LED_BUILTIN, OUTPUT);

  Serial.begin(9600);

  esc.init();

  uint16_t al_status;
  esc.readRegister(AL_STATUS, &al_status, sizeof(al_status));
  Serial.print("Al status ");
  Serial.println(al_status, HEX);

  uint16_t station_alias;
  esc.readRegister(0x0012, &station_alias, sizeof(station_alias));
  Serial.print("before write station_alias ");
  Serial.println(station_alias, HEX);

  station_alias = 0xCAFE;
  esc.writeRegister(0x0012, &station_alias, sizeof(station_alias));
  Serial.println("Between read station alias");
  esc.readRegister(0x0012, &station_alias, sizeof(station_alias));
  Serial.print("after station_alias ");
  Serial.println(station_alias, HEX);

  uint32_t nb_bytes = 32;
  uint8_t test[nb_bytes];
  for (uint32_t i=0; i < nb_bytes; i++)
  {
      test[i] = i;
  }

  esc.writeRegister(0x1200, &test, nb_bytes);

  uint32_t nb_bytes_read = 4;
  uint8_t result[nb_bytes_read];
  esc.readRegister(0x1200, &result, nb_bytes_read);
  for (uint32_t i = 0; i < nb_bytes_read; i++)
  {
      Serial.print("result ");
      Serial.println(result[i], HEX);
  }
  esc.readRegister(0x1208, &result, nb_bytes_read);
  for (uint32_t i = 0; i < nb_bytes_read; i++)
  {
      Serial.print("result ");
      Serial.println(result[i], HEX);
  }
}

void esc_routine()
{
    bool watchdog = false;
    bool operational = false;

//    uint8_t fmmu;
//    esc.readRegister(WDOG_STATUS, &watchdog, 1);
//    esc.readRegister(0x0004, &fmmu, 1);
//    Serial.print("watchdog: ");
//    Serial.println(watchdog, HEX);
//    Serial.print("fmmu: ");
//    Serial.println(fmmu, HEX);


}

void loop() {
  digitalWrite(LED_BUILTIN, HIGH);   // turn the LED on (HIGH is the voltage level)
  delay(1000);                       // wait for a second
  digitalWrite(LED_BUILTIN, LOW);    // turn the LED off by making the voltage LOW
  delay(1000);                       // wait for a second

  Serial.println("HEllo !");
  esc_routine();

}
