#ifndef CONTROLLER_H
#define CONTROLLER_H

#include <Arduino.h>
#include <string.h>
#include "NodeConfig.h"
#include "SdnProtocol.h"

namespace {
// Tracks partial reads for one controller-facing serial link.
struct ArduFlowReader {
  HardwareSerial *port;
  const char *name;
  uint8_t bytes[sizeof(ArduFlowPacket)];
  uint8_t length;
};

// Mega 2 reaches N1/N4 through Mega 1, and N2/N3 directly.
ArduFlowReader readers[] = {
  {&Serial1, "Mega1 relay", {0}, 0},
  {&Serial2, "Nano 2", {0}, 0},
  {&Serial3, "Nano 3", {0}, 0},
};

// Sends one binary ArduFlow control packet.
void writeArduFlowPacket(HardwareSerial &port, const ArduFlowPacket &message) {
  port.write(reinterpret_cast<const uint8_t *>(&message), sizeof(message));
}

// Logs binary control messages as numbers for Serial Monitor debugging.
void logArduFlowPacket(const __FlashStringHelper *prefix, const ArduFlowPacket &message) {
  Serial.print(prefix);
  Serial.print(F(" type="));
  Serial.print(message.type);
  Serial.print(F(" src="));
  Serial.print(message.source_id);
  Serial.print(F(" dst="));
  Serial.print(message.dest_id);
  Serial.print(F(" port="));
  Serial.print(message.port);
  Serial.print(F(" pkt.src="));
  Serial.print(message.packet.source_id);
  Serial.print(F(" pkt.dst="));
  Serial.print(message.packet.dest_id);
  Serial.print(F(" data="));
  Serial.println(message.packet.data);
}

// Chooses the Mega 2 hardware serial link that can reach a switch.
HardwareSerial *controllerPortForSwitch(uint8_t switchId) {
  if (switchId == NANO_1_ID || switchId == NANO_4_ID) {
    return &Serial1;
  }
  if (switchId == NANO_2_ID) {
    return &Serial2;
  }
  if (switchId == NANO_3_ID) {
    return &Serial3;
  }

  return nullptr;
}

// Returns the local switch port for a directly attached host.
uint8_t attachedHostPort(uint8_t switchId, uint8_t hostId) {
  for (uint8_t i = 0; i < sizeof(HOST_ATTACHMENTS) / sizeof(HOST_ATTACHMENTS[0]); ++i) {
    if (HOST_ATTACHMENTS[i].switch_id == switchId && HOST_ATTACHMENTS[i].host_id == hostId) {
      return HOST_ATTACHMENTS[i].switch_port;
    }
  }

  return AF_NO_PORT;
}

// Looks up the configured next output port toward a destination host.
uint8_t routeOutputPort(uint8_t sourceSwitch, uint8_t destHost) {
  uint8_t localPort = attachedHostPort(sourceSwitch, destHost);
  if (localPort != AF_NO_PORT) {
    return localPort;
  }

  for (uint8_t i = 0; i < sizeof(CONTROLLER_ROUTES) / sizeof(CONTROLLER_ROUTES[0]); ++i) {
    if (CONTROLLER_ROUTES[i].source_switch == sourceSwitch &&
        CONTROLLER_ROUTES[i].dest_host == destHost) {
      return CONTROLLER_ROUTES[i].output_port;
    }
  }

  return AF_NO_PORT;
}

// Sends a FLOW_MOD to install/overwrite a destination-host forwarding rule.
void sendFlowMod(uint8_t requestingSwitch, const Packet &missedPacket) {
  HardwareSerial *port = controllerPortForSwitch(requestingSwitch);
  uint8_t outputPort = routeOutputPort(requestingSwitch, missedPacket.dest_id);

  if (port == nullptr || outputPort == AF_NO_PORT) {
    Serial.println(F("Controller: no route for ROUTE_REQ"));
    return;
  }

  ArduFlowPacket flowMod = {
    AF_FLOW_ADD_DST_OVERWRITE,
    AF_CONTROLLER_ID,
    requestingSwitch,
    outputPort,
    missedPacket,
  };

  writeArduFlowPacket(*port, flowMod);
  logArduFlowPacket(F("Controller sent FLOW_MOD:"), flowMod);
}

// ROUTE_REQ is the table-miss path: switch asks controller how to forward.
void handleRouteRequest(const ArduFlowPacket &message) {
  if (message.dest_id != AF_CONTROLLER_ID) {
    Serial.println(F("Controller: ROUTE_REQ not addressed to controller"));
    return;
  }

  if (controllerPortForSwitch(message.source_id) == nullptr) {
    Serial.println(F("Controller: ROUTE_REQ from unknown switch"));
    return;
  }

  sendFlowMod(message.source_id, message.packet);
}

// ACK confirms that a switch integrated a controller rule into its flow table.
void handleAck(const ArduFlowPacket &message) {
  Serial.print(F("Controller: ACK from "));
  Serial.print(message.source_id);
  Serial.print(F(" for port "));
  Serial.println(message.port);
}

// PORT_STATUS lets switches notify the controller about usable or failed links.
void handlePortStatus(const ArduFlowPacket &message) {
  Serial.print(F("Controller: PORT_STATUS from "));
  Serial.print(message.source_id);
  Serial.print(F(" port "));
  Serial.print(message.port);
  Serial.print(F(" type "));
  Serial.println(message.type);

  if (message.type == AF_PORT_STATUS_OFFLINE) {
    ArduFlowPacket deleteRules = {
      AF_FLOW_DELETE_OUTPUT_PORT,
      AF_CONTROLLER_ID,
      message.source_id,
      message.port,
      {0, 0, 0},
    };

    HardwareSerial *port = controllerPortForSwitch(message.source_id);
    if (port != nullptr) {
      writeArduFlowPacket(*port, deleteRules);
      logArduFlowPacket(F("Controller sent FLOW_DELETE:"), deleteRules);
    }
  }
}

// Dispatches one complete control packet by ArduFlow type.
void handleArduFlowPacket(const ArduFlowPacket &message, const char *inputName) {
  Serial.print(F("Controller received on "));
  Serial.print(inputName);
  Serial.print(F(":"));
  logArduFlowPacket(F(""), message);

  if (message.type == AF_ROUTE_REQ) {
    handleRouteRequest(message);
    return;
  }

  if (message.type == AF_ACK) {
    handleAck(message);
    return;
  }

  if (message.type >= AF_PORT_STATUS_QUERY && message.type <= AF_PORT_STATUS_OFFLINE) {
    handlePortStatus(message);
    return;
  }

  Serial.println(F("Controller: unsupported control message"));
}

// Non-blocking fixed-size packet reader for each controller link.
void pollReader(ArduFlowReader &reader) {
  while (reader.port->available() > 0) {
    reader.bytes[reader.length] = static_cast<uint8_t>(reader.port->read());
    ++reader.length;

    if (reader.length == sizeof(ArduFlowPacket)) {
      ArduFlowPacket message;
      memcpy(&message, reader.bytes, sizeof(message));
      handleArduFlowPacket(message, reader.name);
      reader.length = 0;
    }
  }
}
}

void roleSetup() {
  Serial.begin(DEBUG_BAUD);
  Serial1.begin(CONTROL_BAUD);
  Serial2.begin(CONTROL_BAUD);
  Serial3.begin(CONTROL_BAUD);

  Serial.println(F("Mega 2 ArduFlow controller ready"));
}

void roleLoop() {
  // Poll every controller link so one idle switch cannot block another.
  for (uint8_t i = 0; i < sizeof(readers) / sizeof(readers[0]); ++i) {
    pollReader(readers[i]);
  }
}

#endif
