#ifndef __APP_ENTRY_H__
#define __APP_ENTRY_H__

#ifdef ARDUINO

// Arduino specific entry functions setup/loop is exposed as app_setup/app_loop
#include <Arduino.h>

#define app_setup setup
#define app_loop loop

#else

// On non-Arduino platforms like Linux or Windows, main will call these
// functions to provide similar entry points as Arduino.
static void app_setup();
static void app_loop();

int main() { app_setup(); while(1) app_loop(); return 0; }

#endif

#endif
