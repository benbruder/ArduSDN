#include <Arduino.h>

#if defined(ROLE_MEGA2_CONTROLLER)
  #include "controller.h"
#elif defined(ROLE_MEGA1_RELAY)
  #include "relaymega.h"
#elif defined(ROLE_NANO_SWITCH)
  #include "nanoswitch.h"
#elif defined(ROLE_UNO_HOST)
  #include "unohost.h"
#else
  #error "No role selected in platformio.ini"
#endif

void setup() {
  roleSetup();
}

void loop() {
  roleLoop();
}