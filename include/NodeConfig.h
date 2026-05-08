#ifndef NODE_CONFIG_H
#define NODE_CONFIG_H

#include <Arduino.h>
#include "SdnProtocol.h"

// Shared baud rates. Serial0 is debug; controller links use hardware serial.
const unsigned long DEBUG_BAUD = 115200;
const unsigned long CONTROL_BAUD = 115200;

// Stable IDs from AGENTS.md.
const uint8_t NANO_1_ID = 10;
const uint8_t NANO_2_ID = 20;
const uint8_t NANO_3_ID = 30;
const uint8_t NANO_4_ID = 40;
const uint8_t UNO_1_ID = 100;
const uint8_t UNO_2_ID = 200;

const uint8_t SWITCH_PORT_COUNT = 3;

// Devices the controller actively scans during startup discovery.
const uint8_t SWITCH_IDS[] = {
  NANO_1_ID,
  NANO_2_ID,
  NANO_3_ID,
  NANO_4_ID,
};

const uint8_t HOST_IDS[] = {
  UNO_1_ID,
  UNO_2_ID,
};

#endif
