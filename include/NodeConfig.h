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

// Host attachment table. Hosts are fixed in this project.
struct HostAttachment {
  uint8_t host_id;
  uint8_t switch_id;
  uint8_t switch_port;
};

const HostAttachment HOST_ATTACHMENTS[] = {
  {UNO_1_ID, NANO_1_ID, 1},
  {UNO_2_ID, NANO_4_ID, 1},
};

// Controller route table. output_port is the switch port to use toward dest_host.
// Adjust these entries once the physical Nano-to-Nano port map is finalized.
struct ControllerRoute {
  uint8_t source_switch;
  uint8_t dest_host;
  uint8_t output_port;
};

const ControllerRoute CONTROLLER_ROUTES[] = {
  {NANO_1_ID, UNO_2_ID, 2},
  {NANO_2_ID, UNO_1_ID, 1},
  {NANO_2_ID, UNO_2_ID, 2},
  {NANO_3_ID, UNO_1_ID, 1},
  {NANO_3_ID, UNO_2_ID, 2},
  {NANO_4_ID, UNO_1_ID, 2},
};

#endif
