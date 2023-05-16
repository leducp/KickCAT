// Including Arduino.h is required for using Serial functions
#include "Arduino.h"

// the setup routine runs once when you press reset:
void setup() {
	// initialize serial communication at 9600 bits per second:
	Serial.begin(9600);

	// print out hello world
	Serial.println("Hello World");

	// Setup to blink the inbuilt LED
#ifdef LED_BUILTIN
	pinMode(LED_BUILTIN, OUTPUT);
#endif
}

// the loop routine runs over and over again forever:
void loop() {
	// Blink the inbuilt LED
#ifdef LED_BUILTIN
  digitalWrite(LED_BUILTIN, HIGH);   // turn the LED on (HIGH is the voltage level)
  delay(1000);                       // wait for a second
  digitalWrite(LED_BUILTIN, LOW);    // turn the LED off by making the voltage LOW
  delay(1000);                       // wait for a second
#endif
}
