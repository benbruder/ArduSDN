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

const unsigned long DISCOVERY_QUERY_INTERVAL_MS = 25;
const uint32_t DISCOVERY_PING_TIMEOUT_US = 50000;
const uint8_t MAX_DISCOVERED_PORTS = sizeof(SWITCH_IDS) * SWITCH_PORT_COUNT;
const uint8_t MAX_DISCOVERED_HOSTS = sizeof(HOST_IDS);
const uint8_t MAX_FORWARDING_DECISIONS = sizeof(SWITCH_IDS) * sizeof(HOST_IDS);

// One discovered switch port. peer_id is a Nano ID, Uno ID, or AF_UNKNOWN_ID.
struct DiscoveredPort {
  uint8_t switch_id;
  uint8_t port;
  uint8_t peer_id;
  bool online;
  bool blocked;
  bool reported;
};

// Host locations learned from PORT_STATUS_ONLINE ping replies.
struct DiscoveredHost {
  uint8_t host_id;
  uint8_t switch_id;
  uint8_t port;
  bool known;
};

// Runtime route result computed from discovered topology, not preconfigured.
struct ForwardingDecision {
  uint8_t source_switch;
  uint8_t dest_host;
  uint8_t output_port;
  bool valid;
};

DiscoveredPort discoveredPorts[MAX_DISCOVERED_PORTS];
DiscoveredHost discoveredHosts[MAX_DISCOVERED_HOSTS];
ForwardingDecision forwardingDecisions[MAX_FORWARDING_DECISIONS];
uint8_t discoverySwitchIndex = 0;
uint8_t discoveryPort = 1;
unsigned long lastDiscoveryQueryMs = 0;
bool discoveryComplete = false;
bool discoveryQueriesSent = false;
bool stpApplied = false;

// Sends one binary ArduFlow control packet.
void writeArduFlowPacket(HardwareSerial &port, const ArduFlowPacket &message) {
  port.write(reinterpret_cast<const uint8_t *>(&message), sizeof(message));
}

// Logs binary control messages as numbers for Serial Monitor debugging.
void logArduFlowPacket(const __FlashStringHelper *prefix, const ArduFlowPacket &message) {
  Serial.print(prefix);
  Serial.print(F(" src="));
  Serial.print(message.source_id);
  Serial.print(F(" dst="));
  Serial.print(message.dest_id);
  Serial.print(F(" type="));
  Serial.print(message.type);
  Serial.print(F(" port="));
  Serial.print(message.port);
  Serial.print(F(" pkt.src="));
  Serial.print(message.packet.source_id);
  Serial.print(F(" pkt.dst="));
  Serial.print(message.packet.dest_id);
  Serial.print(F(" app="));
  Serial.print(message.packet.app_id);
  Serial.print(F(" setting="));
  Serial.print(message.packet.setting);
  Serial.print(F(" data="));
  Serial.println(message.packet.data);
}

bool isSwitchId(uint8_t deviceId) {
  for (uint8_t i = 0; i < sizeof(SWITCH_IDS); ++i) {
    if (SWITCH_IDS[i] == deviceId) {
      return true;
    }
  }

  return false;
}

bool isHostId(uint8_t deviceId) {
  for (uint8_t i = 0; i < sizeof(HOST_IDS); ++i) {
    if (HOST_IDS[i] == deviceId) {
      return true;
    }
  }

  return false;
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

DiscoveredPort *findDiscoveredPort(uint8_t switchId, uint8_t port) {
  for (uint8_t i = 0; i < MAX_DISCOVERED_PORTS; ++i) {
    if (discoveredPorts[i].reported &&
        discoveredPorts[i].switch_id == switchId &&
        discoveredPorts[i].port == port) {
      return &discoveredPorts[i];
    }
  }

  return nullptr;
}

DiscoveredPort *allocateDiscoveredPort(uint8_t switchId, uint8_t port) {
  DiscoveredPort *existing = findDiscoveredPort(switchId, port);
  if (existing != nullptr) {
    return existing;
  }

  for (uint8_t i = 0; i < MAX_DISCOVERED_PORTS; ++i) {
    if (!discoveredPorts[i].reported) {
      discoveredPorts[i].switch_id = switchId;
      discoveredPorts[i].port = port;
      discoveredPorts[i].peer_id = AF_UNKNOWN_ID;
      discoveredPorts[i].online = false;
      discoveredPorts[i].blocked = false;
      discoveredPorts[i].reported = true;
      return &discoveredPorts[i];
    }
  }

  return nullptr;
}

void recordDiscoveredHost(uint8_t hostId, uint8_t switchId, uint8_t port) {
  if (!isHostId(hostId)) {
    return;
  }

  for (uint8_t i = 0; i < MAX_DISCOVERED_HOSTS; ++i) {
    if (!discoveredHosts[i].known || discoveredHosts[i].host_id == hostId) {
      discoveredHosts[i].host_id = hostId;
      discoveredHosts[i].switch_id = switchId;
      discoveredHosts[i].port = port;
      discoveredHosts[i].known = true;
      return;
    }
  }
}

DiscoveredHost *findDiscoveredHost(uint8_t hostId) {
  for (uint8_t i = 0; i < MAX_DISCOVERED_HOSTS; ++i) {
    if (discoveredHosts[i].known && discoveredHosts[i].host_id == hostId) {
      return &discoveredHosts[i];
    }
  }

  return nullptr;
}

uint8_t switchIndex(uint8_t switchId) {
  for (uint8_t i = 0; i < sizeof(SWITCH_IDS); ++i) {
    if (SWITCH_IDS[i] == switchId) {
      return i;
    }
  }

  return AF_UNKNOWN_ID;
}

DiscoveredPort *findPeerPort(uint8_t switchId, uint8_t peerId) {
  for (uint8_t i = 0; i < MAX_DISCOVERED_PORTS; ++i) {
    if (discoveredPorts[i].reported &&
        discoveredPorts[i].online &&
        discoveredPorts[i].switch_id == switchId &&
        discoveredPorts[i].peer_id == peerId) {
      return &discoveredPorts[i];
    }
  }

  return nullptr;
}

void sendPortControl(uint8_t switchId, uint8_t portNumber, uint8_t type) {
  HardwareSerial *port = controllerPortForSwitch(switchId);
  if (port == nullptr) {
    return;
  }

  ArduFlowPacket message = {
    AF_CONTROLLER_ID,
    switchId,
    type,
    portNumber,
    {0, 0, 0, 0, 0},
  };

  writeArduFlowPacket(*port, message);
  logArduFlowPacket(type == AF_BLOCK_PORT ? F("Controller sent BLOCK_PORT:")
                                           : F("Controller sent UNBLOCK_PORT:"),
                    message);
}

void setSwitchLinkBlocked(DiscoveredPort &portRecord, bool blocked) {
  portRecord.blocked = blocked;

  DiscoveredPort *reverse = findPeerPort(portRecord.peer_id, portRecord.switch_id);
  if (reverse != nullptr) {
    reverse->blocked = blocked;
  }
}

// Builds a spanning tree over discovered switch links and blocks redundant links.
void applySpanningTree();

uint8_t computeOutputPort(uint8_t sourceSwitch, uint8_t destHost) {
  DiscoveredHost *host = findDiscoveredHost(destHost);
  if (host == nullptr) {
    return AF_NO_PORT;
  }

  if (host->switch_id == sourceSwitch) {
    return host->port;
  }

  bool visited[sizeof(SWITCH_IDS)] = {false};
  uint8_t queue[sizeof(SWITCH_IDS)] = {0};
  uint8_t firstPort[sizeof(SWITCH_IDS)] = {0};
  uint8_t head = 0;
  uint8_t tail = 0;

  queue[tail] = sourceSwitch;
  firstPort[tail] = AF_NO_PORT;
  ++tail;

  while (head < tail) {
    uint8_t current = queue[head];
    uint8_t currentFirstPort = firstPort[head];
    ++head;

    for (uint8_t i = 0; i < sizeof(SWITCH_IDS); ++i) {
      if (SWITCH_IDS[i] == current) {
        if (visited[i]) {
          current = AF_UNKNOWN_ID;
        } else {
          visited[i] = true;
        }
        break;
      }
    }

    if (current == AF_UNKNOWN_ID) {
      continue;
    }

    if (current == host->switch_id) {
      return currentFirstPort;
    }

    for (uint8_t i = 0; i < MAX_DISCOVERED_PORTS; ++i) {
      if (!discoveredPorts[i].reported ||
          !discoveredPorts[i].online ||
          discoveredPorts[i].blocked ||
          discoveredPorts[i].switch_id != current ||
          !isSwitchId(discoveredPorts[i].peer_id)) {
        continue;
      }

      uint8_t neighbor = discoveredPorts[i].peer_id;
      uint8_t nextFirstPort = currentFirstPort;
      if (nextFirstPort == AF_NO_PORT) {
        nextFirstPort = discoveredPorts[i].port;
      }

      if (tail < sizeof(queue)) {
        queue[tail] = neighbor;
        firstPort[tail] = nextFirstPort;
        ++tail;
      }
    }
  }

  return AF_NO_PORT;
}

void rebuildForwardingDecisions() {
  for (uint8_t i = 0; i < MAX_FORWARDING_DECISIONS; ++i) {
    forwardingDecisions[i].valid = false;
  }

  uint8_t index = 0;
  for (uint8_t s = 0; s < sizeof(SWITCH_IDS); ++s) {
    for (uint8_t h = 0; h < sizeof(HOST_IDS); ++h) {
      uint8_t outputPort = computeOutputPort(SWITCH_IDS[s], HOST_IDS[h]);
      if (outputPort != AF_NO_PORT && index < MAX_FORWARDING_DECISIONS) {
        forwardingDecisions[index].source_switch = SWITCH_IDS[s];
        forwardingDecisions[index].dest_host = HOST_IDS[h];
        forwardingDecisions[index].output_port = outputPort;
        forwardingDecisions[index].valid = true;
        ++index;
      }
    }
  }
}

void applySpanningTree() {
  for (uint8_t i = 0; i < MAX_DISCOVERED_PORTS; ++i) {
    if (discoveredPorts[i].reported &&
        discoveredPorts[i].online &&
        isSwitchId(discoveredPorts[i].peer_id)) {
      discoveredPorts[i].blocked = true;
    } else if (discoveredPorts[i].reported) {
      discoveredPorts[i].blocked = false;
    }
  }

  bool visited[sizeof(SWITCH_IDS)] = {false};
  uint8_t queue[sizeof(SWITCH_IDS)] = {0};
  uint8_t head = 0;
  uint8_t tail = 0;

  visited[0] = true;
  queue[tail] = SWITCH_IDS[0];
  ++tail;

  while (head < tail) {
    uint8_t current = queue[head];
    ++head;

    for (uint8_t i = 0; i < MAX_DISCOVERED_PORTS; ++i) {
      if (!discoveredPorts[i].reported ||
          !discoveredPorts[i].online ||
          discoveredPorts[i].switch_id != current ||
          !isSwitchId(discoveredPorts[i].peer_id)) {
        continue;
      }

      uint8_t peerIndex = switchIndex(discoveredPorts[i].peer_id);
      if (peerIndex == AF_UNKNOWN_ID || visited[peerIndex]) {
        continue;
      }

      visited[peerIndex] = true;
      queue[tail] = discoveredPorts[i].peer_id;
      ++tail;
      setSwitchLinkBlocked(discoveredPorts[i], false);
    }
  }

  for (uint8_t i = 0; i < MAX_DISCOVERED_PORTS; ++i) {
    if (!discoveredPorts[i].reported ||
        !discoveredPorts[i].online ||
        !isSwitchId(discoveredPorts[i].peer_id)) {
      continue;
    }

    sendPortControl(discoveredPorts[i].switch_id,
                    discoveredPorts[i].port,
                    discoveredPorts[i].blocked ? AF_BLOCK_PORT : AF_UNBLOCK_PORT);
  }

  stpApplied = true;
  discoveryComplete = true;
  rebuildForwardingDecisions();
  Serial.println(F("Controller: STP applied; routes rebuilt from forwarding ports"));
}

// Looks up a route computed from startup discovery and STP decisions.
uint8_t routeOutputPort(uint8_t sourceSwitch, uint8_t destHost) {
  for (uint8_t i = 0; i < MAX_FORWARDING_DECISIONS; ++i) {
    if (forwardingDecisions[i].valid &&
        forwardingDecisions[i].source_switch == sourceSwitch &&
        forwardingDecisions[i].dest_host == destHost) {
      return forwardingDecisions[i].output_port;
    }
  }

  return AF_NO_PORT;
}

// Sends a FLOW_MOD to install/overwrite a destination-host forwarding rule.
void sendFlowMod(uint8_t requestingSwitch, const DataPacket &missedPacket) {
  if (!discoveryComplete) {
    Serial.println(F("Controller: discovery incomplete; route withheld"));
    return;
  }

  HardwareSerial *port = controllerPortForSwitch(requestingSwitch);
  uint8_t outputPort = routeOutputPort(requestingSwitch, missedPacket.dest_id);

  if (port == nullptr || outputPort == AF_NO_PORT) {
    Serial.println(F("Controller: no route for ROUTE_REQ"));
    return;
  }

  ArduFlowPacket flowMod = {
    AF_CONTROLLER_ID,
    requestingSwitch,
    AF_FLOW_ADD_DST_OVERWRITE,
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

  DiscoveredPort *discoveredPort = allocateDiscoveredPort(message.source_id, message.port);
  if (discoveredPort != nullptr) {
    discoveredPort->online = message.type == AF_PORT_STATUS_ONLINE;
    discoveredPort->peer_id = discoveredPort->online ? message.packet.source_id : AF_UNKNOWN_ID;
    discoveredPort->blocked = false;
  }

  if (message.type == AF_PORT_STATUS_ONLINE) {
    recordDiscoveredHost(message.packet.source_id, message.source_id, message.port);
  }

  if (message.type == AF_PORT_STATUS_OFFLINE) {
    ArduFlowPacket deleteRules = {
      AF_CONTROLLER_ID,
      message.source_id,
      AF_FLOW_DELETE_OUTPUT_PORT,
      message.port,
      {0, 0, 0, 0, 0},
    };

    HardwareSerial *port = controllerPortForSwitch(message.source_id);
    if (port != nullptr) {
      writeArduFlowPacket(*port, deleteRules);
      logArduFlowPacket(F("Controller sent FLOW_DELETE:"), deleteRules);
    }
  }
}

void sendPortStatusQuery(uint8_t switchId, uint8_t portNumber) {
  HardwareSerial *port = controllerPortForSwitch(switchId);
  if (port == nullptr) {
    return;
  }

  ArduFlowPacket query = {
    AF_CONTROLLER_ID,
    switchId,
    AF_PORT_STATUS_QUERY,
    portNumber,
    {AF_CONTROLLER_ID, AF_UNKNOWN_ID, DATA_APP_PING_REQUEST, DATA_SETTING_TIMEOUT_US, DISCOVERY_PING_TIMEOUT_US},
  };

  writeArduFlowPacket(*port, query);
  logArduFlowPacket(F("Controller sent PORT_STATUS_QUERY:"), query);
}

bool allDiscoveryReportsReceived() {
  for (uint8_t s = 0; s < sizeof(SWITCH_IDS); ++s) {
    for (uint8_t port = 1; port <= SWITCH_PORT_COUNT; ++port) {
      if (findDiscoveredPort(SWITCH_IDS[s], port) == nullptr) {
        return false;
      }
    }
  }

  return true;
}

void pollDiscovery() {
  if (discoveryComplete) {
    return;
  }

  if (discoveryQueriesSent) {
    if (allDiscoveryReportsReceived() && !stpApplied) {
      applySpanningTree();
    }
    return;
  }

  unsigned long now = millis();
  if (lastDiscoveryQueryMs != 0 && now - lastDiscoveryQueryMs < DISCOVERY_QUERY_INTERVAL_MS) {
    return;
  }

  if (discoverySwitchIndex >= sizeof(SWITCH_IDS)) {
    discoveryQueriesSent = true;
    Serial.println(F("Controller: startup discovery queries sent; waiting for reports"));
    return;
  }

  sendPortStatusQuery(SWITCH_IDS[discoverySwitchIndex], discoveryPort);
  lastDiscoveryQueryMs = now;

  ++discoveryPort;
  if (discoveryPort > SWITCH_PORT_COUNT) {
    discoveryPort = 1;
    ++discoverySwitchIndex;
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
  pollDiscovery();

  // Poll every controller link so one idle switch cannot block another.
  for (uint8_t i = 0; i < sizeof(readers) / sizeof(readers[0]); ++i) {
    pollReader(readers[i]);
  }
}

#endif
