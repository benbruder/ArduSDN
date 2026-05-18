#ifndef RELAYMEGA_H
#define RELAYMEGA_H

#include <Arduino.h>
#include <string.h>
#include "NodeConfig.h"
#include "SdnProtocol.h"

namespace {
// Relay-facing serial links. Serial0 remains the USB debug monitor.
struct RelayReader {
  HardwareSerial *port;
  const char *name;
  uint8_t bytes[sizeof(ArduFlowPacket)];
  uint8_t length;
};

RelayReader readers[] = {
  {&Serial1, "Mega 2", {0}, 0},
  {&Serial2, "Nano 1", {0}, 0},
  {&Serial3, "Nano 4", {0}, 0},
};

void printNodeName(uint8_t nodeId) {
  if (nodeId == AF_CONTROLLER_ID) {
    Serial.print(F("M2"));
  } else if (nodeId == NANO_1_ID) {
    Serial.print(F("N1"));
  } else if (nodeId == NANO_2_ID) {
    Serial.print(F("N2"));
  } else if (nodeId == NANO_3_ID) {
    Serial.print(F("N3"));
  } else if (nodeId == NANO_4_ID) {
    Serial.print(F("N4"));
  } else {
    Serial.print(nodeId);
  }
}

void printTypeName(uint8_t type) {
  switch (type) {
    case AF_ACK: Serial.print(F("ACK")); break;
    case AF_ROUTE_REQ: Serial.print(F("ROUTE_REQ")); break;
    case AF_FLOW_ADD_SRC_NO_OVERWRITE: Serial.print(F("FLOW_ADD_SRC")); break;
    case AF_FLOW_ADD_SRC_OVERWRITE: Serial.print(F("FLOW_ADD_SRC_OVERWRITE")); break;
    case AF_FLOW_ADD_DST_NO_OVERWRITE: Serial.print(F("FLOW_ADD_DST")); break;
    case AF_FLOW_ADD_DST_OVERWRITE: Serial.print(F("FLOW_ADD_DST_OVERWRITE")); break;
    case AF_FLOW_ADD_SRC_DST_NO_OVERWRITE: Serial.print(F("FLOW_ADD_SRC_DST")); break;
    case AF_FLOW_ADD_SRC_DST_OVERWRITE: Serial.print(F("FLOW_ADD_SRC_DST_OVERWRITE")); break;
    case AF_FLOW_DELETE_OUTPUT_PORT: Serial.print(F("FLOW_DELETE_OUTPUT_PORT")); break;
    case AF_FLOW_DELETE_SRC: Serial.print(F("FLOW_DELETE_SRC")); break;
    case AF_FLOW_DELETE_DST: Serial.print(F("FLOW_DELETE_DST")); break;
    case AF_FLOW_DELETE_ALL: Serial.print(F("FLOW_DELETE_ALL")); break;
    case AF_PORT_STATUS_QUERY: Serial.print(F("PORT_STATUS_QUERY")); break;
    case AF_PORT_STATUS_ONLINE: Serial.print(F("PORT_STATUS_ONLINE")); break;
    case AF_PORT_STATUS_OFFLINE: Serial.print(F("PORT_STATUS_OFFLINE")); break;
    case AF_BLOCK_PORT: Serial.print(F("BLOCK_PORT")); break;
    case AF_UNBLOCK_PORT: Serial.print(F("UNBLOCK_PORT")); break;
    case AF_SET_SWITCH_ID: Serial.print(F("SET_SWITCH_ID")); break;
    default: Serial.print(type); break;
  }
}

void logRelayForward(const char *inputName, const char *outputName, const ArduFlowPacket &message) {
  Serial.print(F("RELAY; In: "));
  Serial.print(inputName);
  Serial.print(F("; Out: "));
  Serial.print(outputName);
  Serial.print(F("; To: "));
  printNodeName(message.dest_id);
  Serial.print(F("; From: "));
  printNodeName(message.source_id);
  Serial.print(F("; Type: "));
  printTypeName(message.type);
  Serial.print(F("; Port: "));
  Serial.println(message.port);
}

HardwareSerial *portForDestination(uint8_t destinationId, const char **name) {
  if (destinationId == AF_CONTROLLER_ID) {
    *name = "Mega 2";
    return &Serial1;
  }
  if (destinationId == NANO_1_ID) {
    *name = "Nano 1";
    return &Serial2;
  }
  if (destinationId == NANO_4_ID) {
    *name = "Nano 4";
    return &Serial3;
  }

  *name = "none";
  return nullptr;
}

void forwardPacket(const ArduFlowPacket &message, const char *inputName) {
  const char *outputName = nullptr;
  HardwareSerial *output = portForDestination(message.dest_id, &outputName);
  if (output == nullptr) {
    Serial.print(F("RELAY DROP; Unknown destination: "));
    printNodeName(message.dest_id);
    Serial.println();
    return;
  }

  output->write(reinterpret_cast<const uint8_t *>(&message), sizeof(message));
  logRelayForward(inputName, outputName, message);
}

void sendSwitchIdAssignment(HardwareSerial &port, const char *outputName, uint8_t switchId) {
  ArduFlowPacket message = {
    AF_CONTROLLER_ID,
    AF_UNASSIGNED_ID,
    AF_SET_SWITCH_ID,
    switchId,
    {0, 0, 0, 0, 0},
  };

  port.write(reinterpret_cast<const uint8_t *>(&message), sizeof(message));
  logRelayForward("Mega 1 local", outputName, message);
}

void pollReader(RelayReader &reader) {
  while (reader.port->available() > 0) {
    reader.bytes[reader.length] = static_cast<uint8_t>(reader.port->read());
    ++reader.length;

    if (reader.length == sizeof(ArduFlowPacket)) {
      ArduFlowPacket message;
      memcpy(&message, reader.bytes, sizeof(message));
      reader.length = 0;
      forwardPacket(message, reader.name);
    }
  }
}
}

void roleSetup() {
  Serial.begin(DEBUG_BAUD);
  Serial1.begin(CONTROL_BAUD);
  Serial2.begin(CONTROL_BAUD);
  Serial3.begin(CONTROL_BAUD);

  Serial.println(F("Mega 1 ArduFlow relay ready"));
  sendSwitchIdAssignment(Serial2, "Nano 1", NANO_1_ID);
  sendSwitchIdAssignment(Serial3, "Nano 4", NANO_4_ID);
}

void roleLoop() {
  for (uint8_t i = 0; i < sizeof(readers) / sizeof(readers[0]); ++i) {
    pollReader(readers[i]);
  }
}

#endif
