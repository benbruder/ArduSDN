#ifndef SDN_PROTOCOL_H
#define SDN_PROTOCOL_H

#include <Arduino.h>

// Data-plane packet exchanged by Uno hosts through Nano switches.
struct Packet {
  uint8_t source_id;
  uint8_t dest_id;
  uint16_t data;
};

// Control-plane packet exchanged between switches, relay, and controller.
struct ArduFlowPacket {
  uint8_t type;
  uint8_t source_id;
  uint8_t dest_id;
  uint8_t port;
  Packet packet;
};

// ArduFlow control message types.
const uint8_t AF_ACK = 0;
const uint8_t AF_ROUTE_REQ = 1;
const uint8_t AF_FLOW_ADD_SRC_NO_OVERWRITE = 20;
const uint8_t AF_FLOW_ADD_SRC_OVERWRITE = 21;
const uint8_t AF_FLOW_ADD_DST_NO_OVERWRITE = 22;
const uint8_t AF_FLOW_ADD_DST_OVERWRITE = 23;
const uint8_t AF_FLOW_ADD_SRC_DST_NO_OVERWRITE = 24;
const uint8_t AF_FLOW_ADD_SRC_DST_OVERWRITE = 25;
const uint8_t AF_FLOW_DELETE_OUTPUT_PORT = 26;
const uint8_t AF_FLOW_DELETE_SRC = 27;
const uint8_t AF_FLOW_DELETE_DST = 28;
const uint8_t AF_FLOW_DELETE_ALL = 29;
const uint8_t AF_PORT_STATUS_QUERY = 30;
const uint8_t AF_PORT_STATUS_ONLINE = 31;
const uint8_t AF_PORT_STATUS_OFFLINE = 32;

const uint8_t AF_CONTROLLER_ID = 0;
const uint8_t AF_NO_PORT = 0;

#endif
