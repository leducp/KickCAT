
#include "Arduino.h"

#include "kickcat/ESC/Lan9252.h"

using namespace kickcat;
Lan9252 esc;



void setup() {
  pinMode(LED_BUILTIN, OUTPUT);

  Serial.begin(9600);

  esc.init();
}

void esc_routine()
{
//    bool watchdog = false;
//    bool operational = false;
//    uint8_t i = 0;
//
//    uint8_t fmmu;
//    esc.readRegister(WDOG_STATUS, &watchdog, 1);
//    esc.readRegister(0x0004, &fmmu, 1);
//    Serial.print("watchdog: ");
//    Serial.println(watchdog, HEX);
//    Serial.print("fmmu: ");
//    Serial.println(fmmu, HEX);

    uint16_t al_status;
    esc.readRegister(AL_STATUS, &al_status, sizeof(al_status));
    Serial.print("Al status ");
    Serial.println(al_status, HEX);
}

void loop() {
  digitalWrite(LED_BUILTIN, HIGH);   // turn the LED on (HIGH is the voltage level)
  delay(1000);                       // wait for a second
  digitalWrite(LED_BUILTIN, LOW);    // turn the LED off by making the voltage LOW
  delay(1000);                       // wait for a second

  Serial.println("HEllo !");
  esc_routine();

}
