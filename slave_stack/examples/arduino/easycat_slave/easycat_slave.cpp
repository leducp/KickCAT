
#include "Arduino.h"

#include "kickcat/ESC/Lan9252.h"


kickcat::Lan9252 esc;

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);

  Serial.begin(9600);

  esc.init();
}

void loop() {
  digitalWrite(LED_BUILTIN, HIGH);   // turn the LED on (HIGH is the voltage level)
  delay(1000);                       // wait for a second
  digitalWrite(LED_BUILTIN, LOW);    // turn the LED off by making the voltage LOW
  delay(1000);                       // wait for a second

  int tasty_test = kickcat::test();
  Serial.println(tasty_test);
  Serial.println("HEllo !");

}
