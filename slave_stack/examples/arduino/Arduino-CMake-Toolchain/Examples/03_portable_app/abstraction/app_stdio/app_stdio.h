#ifndef __APP_STDIO_H__
#define __APP_STDIO_H__

#include <stdarg.h>

void init_app_stdio();
void app_printf(const char *msg, ...);
void app_scanf(const char *msg, ...);

#ifdef ARDUINO

// Implementation of app_scanf as a macro due to unavailability of vsnscanf
#define app_scanf(fmt, ...) do                     \
	{                                              \
		String s = Serial.readStringUntil('\n');   \
		sscanf(s.c_str(), fmt, ## __VA_ARGS__);    \
	}                                              \
	while(0)

#endif

#endif
