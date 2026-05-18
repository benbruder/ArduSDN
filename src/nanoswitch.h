#ifndef NANOSWITCH_H
#define NANOSWITCH_H

#include <Arduino.h>
#include <NeoSWSerial.h>
#include <string.h>
#include "NodeConfig.h"
#include "SdnProtocol.h"

namespace {
uint8_t switchId = AF_UNASSIGNED_ID;
const unsigned long SWITCH_PORT_BAUD = 9600;
const uint8_t FLOW_TABLE_SIZE = 8;
const uint8_t STATUS_LED_PIN = A5;
const unsigned long NORMAL_PACKET_BLINK_INTERVAL_MS = 250;
const unsigned long SIGNAL_PACKET_BLINK_INTERVAL_MS = 500;
const uint8_t STATUS_LED_TOGGLE_COUNT = 4;

struct SwitchPort {
  NeoSWSerial *serial;
  uint8_t trigger_out;
  uint8_t trigger_in;
  uint8_t bytes[sizeof(DataPacket)];
  uint8_t length;
  bool blocked;
};

struct FlowRule {
  uint8_t type;
  uint8_t source_id;
  uint8_t dest_id;
  uint8_t output_port;
  bool used;
};

struct PendingPortQuery {
  bool active;
  uint8_t port;
  unsigned long deadline_ms;
};

NeoSWSerial port1(4, 5);
NeoSWSerial port2(6, 7);
NeoSWSerial port3(8, 9);

SwitchPort switchPorts[] = {
  {&port1, 10, A0, {0}, 0, false},
  {&port2, 11, A1, {0}, 0, false},
  {&port3, 12, A2, {0}, 0, false},
};

FlowRule flowTable[FLOW_TABLE_SIZE];
uint8_t controllerBytes[sizeof(ArduFlowPacket)];
uint8_t controllerLength = 0;
PendingPortQuery pendingQuery = {false, AF_NO_PORT, 0};
bool statusLedActive = false;
bool statusLedState = false;
uint8_t statusLedTogglesRemaining = 0;
unsigned long statusLedIntervalMs = 0;
unsigned long lastStatusLedToggleMs = 0;

SwitchPort *portByNumber(uint8_t portNumber) {
  if (portNumber < 1 || portNumber > SWITCH_PORT_COUNT) {
    return nullptr;
  }

  return &switchPorts[portNumber - 1];
}

bool isHostId(uint8_t deviceId) {
  for (uint8_t i = 0; i < sizeof(HOST_IDS); ++i) {
    if (HOST_IDS[i] == deviceId) {
      return true;
    }
  }

  return false;
}

void writeDataPacket(SwitchPort &port, const DataPacket &packet) {
  digitalWrite(port.trigger_out, HIGH);
  delayMicroseconds(50);
  port.serial->write(reinterpret_cast<const uint8_t *>(&packet), sizeof(packet));
  port.serial->flush();
  digitalWrite(port.trigger_out, LOW);
}

void writeArduFlowPacket(const ArduFlowPacket &message) {
  Serial.write(reinterpret_cast<const uint8_t *>(&message), sizeof(message));
}

void startStatusBlink(unsigned long intervalMs) {
  statusLedActive = true;
  statusLedState = true;
  statusLedTogglesRemaining = STATUS_LED_TOGGLE_COUNT;
  statusLedIntervalMs = intervalMs;
  lastStatusLedToggleMs = millis();
  digitalWrite(STATUS_LED_PIN, HIGH);
}

void pollStatusLed() {
  if (!statusLedActive) {
    return;
  }

  unsigned long now = millis();
  if (now - lastStatusLedToggleMs < statusLedIntervalMs) {
    return;
  }

  lastStatusLedToggleMs = now;
  statusLedState = !statusLedState;
  digitalWrite(STATUS_LED_PIN, statusLedState ? HIGH : LOW);

  if (statusLedTogglesRemaining > 0) {
    --statusLedTogglesRemaining;
  }

  if (statusLedTogglesRemaining == 0) {
    statusLedActive = false;
    statusLedState = false;
    digitalWrite(STATUS_LED_PIN, LOW);
  }
}

void sendAck(uint8_t portNumber) {
  ArduFlowPacket ack = {
    switchId,
    AF_CONTROLLER_ID,
    AF_ACK,
    portNumber,
    {0, 0, 0, 0, 0},
  };

  writeArduFlowPacket(ack);
}

bool matchesRule(const FlowRule &rule, const DataPacket &packet) {
  if (!rule.used) {
    return false;
  }

  if ((rule.type == AF_FLOW_ADD_SRC_NO_OVERWRITE ||
       rule.type == AF_FLOW_ADD_SRC_OVERWRITE) &&
      rule.source_id == packet.source_id) {
    return true;
  }

  if ((rule.type == AF_FLOW_ADD_DST_NO_OVERWRITE ||
       rule.type == AF_FLOW_ADD_DST_OVERWRITE) &&
      rule.dest_id == packet.dest_id) {
    return true;
  }

  if ((rule.type == AF_FLOW_ADD_SRC_DST_NO_OVERWRITE ||
       rule.type == AF_FLOW_ADD_SRC_DST_OVERWRITE) &&
      rule.source_id == packet.source_id &&
      rule.dest_id == packet.dest_id) {
    return true;
  }

  return false;
}

FlowRule *findMatchingRule(const DataPacket &packet) {
  for (uint8_t i = 0; i < FLOW_TABLE_SIZE; ++i) {
    if (matchesRule(flowTable[i], packet)) {
      return &flowTable[i];
    }
  }

  return nullptr;
}

bool sameMatch(const FlowRule &rule, uint8_t type, const DataPacket &packet) {
  if (!rule.used || rule.type != type) {
    return false;
  }

  if (type == AF_FLOW_ADD_SRC_NO_OVERWRITE || type == AF_FLOW_ADD_SRC_OVERWRITE) {
    return rule.source_id == packet.source_id;
  }
  if (type == AF_FLOW_ADD_DST_NO_OVERWRITE || type == AF_FLOW_ADD_DST_OVERWRITE) {
    return rule.dest_id == packet.dest_id;
  }
  if (type == AF_FLOW_ADD_SRC_DST_NO_OVERWRITE || type == AF_FLOW_ADD_SRC_DST_OVERWRITE) {
    return rule.source_id == packet.source_id && rule.dest_id == packet.dest_id;
  }

  return false;
}

bool isOverwriteFlowType(uint8_t type) {
  return type == AF_FLOW_ADD_SRC_OVERWRITE ||
         type == AF_FLOW_ADD_DST_OVERWRITE ||
         type == AF_FLOW_ADD_SRC_DST_OVERWRITE;
}

bool isFlowAddType(uint8_t type) {
  return type >= AF_FLOW_ADD_SRC_NO_OVERWRITE &&
         type <= AF_FLOW_ADD_SRC_DST_OVERWRITE;
}

void installFlowRule(const ArduFlowPacket &message) {
  FlowRule *target = nullptr;

  for (uint8_t i = 0; i < FLOW_TABLE_SIZE; ++i) {
    if (sameMatch(flowTable[i], message.type, message.packet)) {
      if (!isOverwriteFlowType(message.type)) {
        sendAck(message.port);
        return;
      }
      target = &flowTable[i];
      break;
    }
  }

  if (target == nullptr) {
    for (uint8_t i = 0; i < FLOW_TABLE_SIZE; ++i) {
      if (!flowTable[i].used) {
        target = &flowTable[i];
        break;
      }
    }
  }

  if (target == nullptr) {
    return;
  }

  target->type = message.type;
  target->source_id = message.packet.source_id;
  target->dest_id = message.packet.dest_id;
  target->output_port = message.port;
  target->used = true;
  sendAck(message.port);
}

void deleteRulesForMessage(const ArduFlowPacket &message) {
  for (uint8_t i = 0; i < FLOW_TABLE_SIZE; ++i) {
    if (!flowTable[i].used) {
      continue;
    }

    bool shouldDelete = false;
    if (message.type == AF_FLOW_DELETE_OUTPUT_PORT) {
      shouldDelete = flowTable[i].output_port == message.port;
    } else if (message.type == AF_FLOW_DELETE_SRC) {
      shouldDelete = flowTable[i].source_id == message.packet.source_id;
    } else if (message.type == AF_FLOW_DELETE_DST) {
      shouldDelete = flowTable[i].dest_id == message.packet.dest_id;
    } else if (message.type == AF_FLOW_DELETE_ALL) {
      shouldDelete = true;
    }

    if (shouldDelete) {
      flowTable[i].used = false;
    }
  }
}

void sendRouteRequest(uint8_t ingressPort, const DataPacket &packet) {
  ArduFlowPacket request = {
    switchId,
    AF_CONTROLLER_ID,
    AF_ROUTE_REQ,
    ingressPort,
    packet,
  };

  writeArduFlowPacket(request);
}

void forwardPacket(uint8_t ingressPort, const DataPacket &packet) {
  FlowRule *rule = findMatchingRule(packet);
  if (rule == nullptr) {
    sendRouteRequest(ingressPort, packet);
    return;
  }

  SwitchPort *outPort = portByNumber(rule->output_port);
  if (outPort == nullptr || outPort->blocked) {
    return;
  }

  writeDataPacket(*outPort, packet);
}

void replyToPing(uint8_t ingressPort, const DataPacket &packet) {
  SwitchPort *port = portByNumber(ingressPort);
  if (port == nullptr) {
    return;
  }

  DataPacket reply = {
    switchId,
    packet.source_id,
    DATA_APP_PING_REPLY,
    DATA_SETTING_NONE,
    0,
  };

  writeDataPacket(*port, reply);
}

void sendPortStatus(uint8_t type, uint8_t portNumber, const DataPacket &packet) {
  ArduFlowPacket status = {
    switchId,
    AF_CONTROLLER_ID,
    type,
    portNumber,
    packet,
  };

  writeArduFlowPacket(status);
}

void handleDataPacket(uint8_t ingressPort, const DataPacket &packet) {
  if (isHostId(packet.source_id)) {
    startStatusBlink(NORMAL_PACKET_BLINK_INTERVAL_MS);
  }

  if (packet.app_id == DATA_APP_PING_REQUEST) {
    replyToPing(ingressPort, packet);
    return;
  }

  if (pendingQuery.active &&
      pendingQuery.port == ingressPort &&
      packet.app_id == DATA_APP_PING_REPLY) {
    pendingQuery.active = false;
    sendPortStatus(AF_PORT_STATUS_ONLINE, ingressPort, packet);
    return;
  }

  forwardPacket(ingressPort, packet);
}

void pollDataPort(uint8_t portNumber) {
  SwitchPort *port = portByNumber(portNumber);
  if (port == nullptr) {
    return;
  }

  port->serial->listen();
  while (port->serial->available() > 0) {
    port->bytes[port->length] = static_cast<uint8_t>(port->serial->read());
    ++port->length;

    if (port->length == sizeof(DataPacket)) {
      DataPacket packet;
      memcpy(&packet, port->bytes, sizeof(packet));
      port->length = 0;
      handleDataPacket(portNumber, packet);
    }
  }
}

void startPortStatusQuery(const ArduFlowPacket &message) {
  SwitchPort *port = portByNumber(message.port);
  if (port == nullptr) {
    sendPortStatus(AF_PORT_STATUS_OFFLINE, message.port, {0, 0, 0, 0, 0});
    return;
  }

  pendingQuery.active = true;
  pendingQuery.port = message.port;
  pendingQuery.deadline_ms = millis() + (message.packet.data / 1000UL);
  writeDataPacket(*port, message.packet);
}

void handleControllerMessage(const ArduFlowPacket &message) {
  if (message.type == AF_SET_SWITCH_ID &&
      (message.dest_id == AF_UNASSIGNED_ID || message.dest_id == switchId)) {
    switchId = message.port;
    startStatusBlink(SIGNAL_PACKET_BLINK_INTERVAL_MS);
    sendAck(switchId);
    return;
  }

  if (message.dest_id != switchId) {
    return;
  }

  startStatusBlink(SIGNAL_PACKET_BLINK_INTERVAL_MS);

  if (isFlowAddType(message.type)) {
    installFlowRule(message);
    return;
  }

  if (message.type >= AF_FLOW_DELETE_OUTPUT_PORT && message.type <= AF_FLOW_DELETE_ALL) {
    deleteRulesForMessage(message);
    return;
  }

  if (message.type == AF_PORT_STATUS_QUERY) {
    startPortStatusQuery(message);
    return;
  }

  if (message.type == AF_BLOCK_PORT || message.type == AF_UNBLOCK_PORT) {
    SwitchPort *port = portByNumber(message.port);
    if (port != nullptr) {
      port->blocked = message.type == AF_BLOCK_PORT;
    }
  }
}

void pollController() {
  while (Serial.available() > 0) {
    controllerBytes[controllerLength] = static_cast<uint8_t>(Serial.read());
    ++controllerLength;

    if (controllerLength == sizeof(ArduFlowPacket)) {
      ArduFlowPacket message;
      memcpy(&message, controllerBytes, sizeof(message));
      controllerLength = 0;
      handleControllerMessage(message);
    }
  }
}

void pollPendingQuery() {
  if (!pendingQuery.active) {
    return;
  }

  if (static_cast<long>(millis() - pendingQuery.deadline_ms) >= 0) {
    uint8_t port = pendingQuery.port;
    pendingQuery.active = false;
    sendPortStatus(AF_PORT_STATUS_OFFLINE, port, {0, 0, 0, 0, 0});
  }
}
}

void roleSetup() {
  Serial.begin(CONTROL_BAUD);
  pinMode(STATUS_LED_PIN, OUTPUT);
  digitalWrite(STATUS_LED_PIN, LOW);

  for (uint8_t i = 0; i < SWITCH_PORT_COUNT; ++i) {
    switchPorts[i].serial->begin(SWITCH_PORT_BAUD);
    pinMode(switchPorts[i].trigger_out, OUTPUT);
    pinMode(switchPorts[i].trigger_in, INPUT);
    digitalWrite(switchPorts[i].trigger_out, LOW);
  }
}

void roleLoop() {
  pollController();
  for (uint8_t port = 1; port <= SWITCH_PORT_COUNT; ++port) {
    pollDataPort(port);
  }
  pollPendingQuery();
  pollStatusLed();
}

#endif
