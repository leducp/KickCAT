#include "app_stdio.h"

// Including stdarg.h is required for variable argument function
#include <stdarg.h>

// Including stdio.h is required for using printf function
#include <stdio.h>

#ifdef ARDUINO

// Arduino specific implementation of standard input/output

// Including Arduino.h is required for using Serial functions
#include "Arduino.h"

void init_app_stdio()
{
	// initialize serial stream at 9600 bits per second:
	Serial.begin(9600);
}

void app_printf(const char *fmt, ...)
{
	// print to string and then output to Serial
	char buf[128];
	va_list args;
	va_start(args, fmt);
	vsnprintf(buf, sizeof(buf), fmt, args);
	Serial.print(buf);
	va_end(args);
}

#if 0
// Implementation of app_scanf as a macro due to unavailability of vsnscanf
void app_scanf(const char *fmt, ...)
{
	// Scan a string from Serial and then scan the format
	String s = Serial.readStringUntil('\n');
	va_list args;
	va_start(args, fmt);
	vsnscanf(s.c_str(), fmt, args);
	va_end(args);
}
#endif

#else

// Implementation for other platforms (Linux, MAC, Windows etc.)

void init_app_stdio()
{
	// Standard output is already initialized
}

void app_printf(const char *fmt, ...)
{
	// variable arg print to stdio
	va_list args;
	va_start(args, fmt);
	vprintf(fmt, args);
	va_end(args);
}

void app_scanf(const char *fmt, ...)
{
	// variable arg scan from stdio
	va_list args;
	va_start(args, fmt);
	vscanf(fmt, args);
	va_end(args);
}

#endif
