#ifndef SDN_PROTOCOL_H
#define SDN_PROTOCOL_H

#include <Arduino.h>

// Application/data-plane packet exchanged by hosts and forwarded by switches.
struct __attribute__((packed)) DataPacket {
  uint8_t source_id;
  uint8_t dest_id;
  uint8_t app_id;
  uint8_t setting;
  uint32_t data;
};

// Control-plane packet exchanged between switches, relay, and controller.
struct __attribute__((packed)) ArduFlowPacket {
  uint8_t source_id;
  uint8_t dest_id;
  uint8_t type;
  uint8_t port;
  DataPacket packet;
};

static_assert(sizeof(DataPacket) == 8, "DataPacket must stay 8 bytes");
static_assert(sizeof(ArduFlowPacket) == 12, "ArduFlowPacket must stay 12 bytes");

const uint8_t DATA_PACKET_SIZE = sizeof(DataPacket);
const uint8_t ARDUFLOW_PACKET_SIZE = sizeof(ArduFlowPacket);

// DataPacket app/setting values used for discovery pings.
const uint8_t DATA_APP_PING_REQUEST = 0;
const uint8_t DATA_APP_PING_REPLY = 1;
const uint8_t DATA_SETTING_NONE = 0;
const uint8_t DATA_SETTING_TIMEOUT_US = 1;

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
const uint8_t AF_BLOCK_PORT = 40;
const uint8_t AF_UNBLOCK_PORT = 41;

const uint8_t AF_CONTROLLER_ID = 0;
const uint8_t AF_NO_PORT = 0;
const uint8_t AF_UNKNOWN_ID = 255;

#endif
