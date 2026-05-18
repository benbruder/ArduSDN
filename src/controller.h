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
const unsigned long SWITCH_ID_ASSIGNMENT_SETTLE_MS = 500;
const uint8_t MAX_DISCOVERED_PORTS = sizeof(SWITCH_IDS) * SWITCH_PORT_COUNT;
const uint8_t MAX_DISCOVERED_HOSTS = sizeof(HOST_IDS);
const uint8_t MAX_FORWARDING_DECISIONS = sizeof(SWITCH_IDS) * sizeof(HOST_IDS);
const uint8_t MAX_PENDING_FLOWS = MAX_FORWARDING_DECISIONS;
const unsigned long HEALTH_CHECK_CYCLE_MS = 5000;
const unsigned long HEALTH_CHECK_QUERY_INTERVAL_MS = HEALTH_CHECK_CYCLE_MS / MAX_DISCOVERED_PORTS;
const unsigned long FLOW_ACK_TIMEOUT_MS = 1000;

// One discovered switch port. peer_id is a Nano ID, Uno ID, or AF_UNKNOWN_ID.
struct DiscoveredPort {
  uint8_t switch_id;
  uint8_t port;
  uint8_t peer_id;
  bool online;
  bool blocked;
  bool block_state_sent;
  bool last_sent_blocked;
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

// Tracks FLOW_MOD messages until the switch confirms installation with ACK.
struct PendingFlow {
  uint8_t switch_id;
  uint8_t output_port;
  DataPacket packet;
  unsigned long sent_ms;
  bool active;
  bool retried;
};

DiscoveredPort discoveredPorts[MAX_DISCOVERED_PORTS];
DiscoveredHost discoveredHosts[MAX_DISCOVERED_HOSTS];
ForwardingDecision forwardingDecisions[MAX_FORWARDING_DECISIONS];
PendingFlow pendingFlows[MAX_PENDING_FLOWS];
uint8_t discoverySwitchIndex = 0;
uint8_t discoveryPort = 1;
unsigned long lastDiscoveryQueryMs = 0;
unsigned long assignmentStartMs = 0;
bool switchIdsAssigned = false;
bool discoveryComplete = false;
bool discoveryQueriesSent = false;
bool stpApplied = false;
uint8_t healthSwitchIndex = 0;
uint8_t healthPort = 1;
unsigned long lastHealthQueryMs = 0;

// Sends one binary ArduFlow control packet.
void writeArduFlowPacket(HardwareSerial &port, const ArduFlowPacket &message) {
  port.write(reinterpret_cast<const uint8_t *>(&message), sizeof(message));
}

HardwareSerial *controllerPortForSwitch(uint8_t switchId);
void logArduFlowPacket(const __FlashStringHelper *direction, const ArduFlowPacket &message);

void sendArduFlowToSwitch(uint8_t switchId, const ArduFlowPacket &message) {
  HardwareSerial *port = controllerPortForSwitch(switchId);
  if (port != nullptr) {
    writeArduFlowPacket(*port, message);
  }
}

void sendSwitchIdAssignment(uint8_t switchId) {
  ArduFlowPacket message = {
    AF_CONTROLLER_ID,
    AF_UNASSIGNED_ID,
    AF_SET_SWITCH_ID,
    switchId,
    {0, 0, 0, 0, 0},
  };

  sendArduFlowToSwitch(switchId, message);
  logArduFlowPacket(F("SENT"), message);
}

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
  } else if (nodeId == UNO_1_ID) {
    Serial.print(F("U1"));
  } else if (nodeId == UNO_2_ID) {
    Serial.print(F("U2"));
  } else if (nodeId == AF_UNKNOWN_ID) {
    Serial.print(F("UNKNOWN"));
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

void printDataPacket(const DataPacket &packet) {
  Serial.print(F("{src: "));
  printNodeName(packet.source_id);
  Serial.print(F("; dst: "));
  printNodeName(packet.dest_id);
  Serial.print(F("; app: "));
  Serial.print(packet.app_id);
  Serial.print(F("; setting: "));
  Serial.print(packet.setting);
  Serial.print(F("; data: "));
  Serial.print(packet.data);
  Serial.print(F("}"));
}

void logPortDown(uint8_t switchId, uint8_t portNumber) {
  Serial.print(F("DOWN; Switch: "));
  printNodeName(switchId);
  Serial.print(F("; Port: "));
  Serial.println(portNumber);
}

bool switchHasOnlinePort(uint8_t switchId) {
  for (uint8_t i = 0; i < MAX_DISCOVERED_PORTS; ++i) {
    if (discoveredPorts[i].reported &&
        discoveredPorts[i].switch_id == switchId &&
        discoveredPorts[i].online) {
      return true;
    }
  }

  return false;
}

void logSwitchDownIfNeeded(uint8_t switchId) {
  if (switchHasOnlinePort(switchId)) {
    return;
  }

  Serial.print(F("DOWN; Switch: "));
  printNodeName(switchId);
  Serial.println(F("; All reported ports offline"));
}

void logArduFlowPacket(const __FlashStringHelper *direction, const ArduFlowPacket &message) {
  Serial.print(direction);
  Serial.print(F("; To: "));
  printNodeName(message.dest_id);
  Serial.print(F("; From: "));
  printNodeName(message.source_id);
  Serial.print(F("; Type: "));
  printTypeName(message.type);
  Serial.print(F("; Port: "));
  if (message.port == AF_UNKNOWN_ID) {
    Serial.print(F("N/A"));
  } else {
    Serial.print(message.port);
  }
  Serial.print(F("; Packet: "));
  printDataPacket(message.packet);
  Serial.println();
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

void clearPendingFlows() {
  for (uint8_t i = 0; i < MAX_PENDING_FLOWS; ++i) {
    pendingFlows[i].active = false;
  }
}

void recordPendingFlow(uint8_t switchId, uint8_t outputPort, const DataPacket &packet) {
  for (uint8_t i = 0; i < MAX_PENDING_FLOWS; ++i) {
    if (!pendingFlows[i].active) {
      pendingFlows[i].switch_id = switchId;
      pendingFlows[i].output_port = outputPort;
      pendingFlows[i].packet = packet;
      pendingFlows[i].sent_ms = millis();
      pendingFlows[i].active = true;
      pendingFlows[i].retried = false;
      return;
    }
  }

  Serial.println(F("Controller: pending FLOW_MOD table full"));
}

void sendFlowDeleteAllToSwitch(uint8_t switchId) {
  ArduFlowPacket deleteAll = {
    AF_CONTROLLER_ID,
    switchId,
    AF_FLOW_DELETE_ALL,
    AF_UNKNOWN_ID,
    {0, 0, 0, 0, 0},
  };

  sendArduFlowToSwitch(switchId, deleteAll);
  logArduFlowPacket(F("SENT"), deleteAll);
}

void sendFlowDeleteAllToAllSwitches() {
  clearPendingFlows();
  for (uint8_t i = 0; i < sizeof(SWITCH_IDS); ++i) {
    sendFlowDeleteAllToSwitch(SWITCH_IDS[i]);
  }
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
      discoveredPorts[i].block_state_sent = false;
      discoveredPorts[i].last_sent_blocked = false;
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

void clearDiscoveredHostOnPort(uint8_t switchId, uint8_t port) {
  for (uint8_t i = 0; i < MAX_DISCOVERED_HOSTS; ++i) {
    if (discoveredHosts[i].known &&
        discoveredHosts[i].switch_id == switchId &&
        discoveredHosts[i].port == port) {
      discoveredHosts[i].known = false;
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
  logArduFlowPacket(F("SENT"), message);
}

void syncPortControl(DiscoveredPort &portRecord) {
  if (portRecord.block_state_sent &&
      portRecord.last_sent_blocked == portRecord.blocked) {
    return;
  }

  sendPortControl(portRecord.switch_id,
                  portRecord.port,
                  portRecord.blocked ? AF_BLOCK_PORT : AF_UNBLOCK_PORT);
  portRecord.block_state_sent = true;
  portRecord.last_sent_blocked = portRecord.blocked;
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

void printTopologySummary() {
  Serial.println(F("=== Controller Topology ==="));
  for (uint8_t i = 0; i < MAX_DISCOVERED_PORTS; ++i) {
    if (!discoveredPorts[i].reported) {
      continue;
    }

    Serial.print(F("SW "));
    Serial.print(discoveredPorts[i].switch_id);
    Serial.print(F(" port "));
    Serial.print(discoveredPorts[i].port);
    Serial.print(F(": "));

    if (!discoveredPorts[i].online) {
      Serial.println(F("offline"));
      continue;
    }

    Serial.print(F("peer "));
    Serial.print(discoveredPorts[i].peer_id);
    if (isSwitchId(discoveredPorts[i].peer_id)) {
      Serial.print(discoveredPorts[i].blocked ? F(" BLOCKED") : F(" FORWARDING"));
    } else if (isHostId(discoveredPorts[i].peer_id)) {
      Serial.print(F(" HOST"));
    } else {
      Serial.print(F(" UNKNOWN"));
    }
    Serial.println();
  }

  Serial.println(F("=== Controller Hosts ==="));
  for (uint8_t i = 0; i < MAX_DISCOVERED_HOSTS; ++i) {
    if (!discoveredHosts[i].known) {
      continue;
    }

    Serial.print(F("Host "));
    Serial.print(discoveredHosts[i].host_id);
    Serial.print(F(" at switch "));
    Serial.print(discoveredHosts[i].switch_id);
    Serial.print(F(" port "));
    Serial.println(discoveredHosts[i].port);
  }

  Serial.println(F("=== Controller Routes ==="));
  for (uint8_t i = 0; i < MAX_FORWARDING_DECISIONS; ++i) {
    if (!forwardingDecisions[i].valid) {
      continue;
    }

    Serial.print(F("From switch "));
    Serial.print(forwardingDecisions[i].source_switch);
    Serial.print(F(" to host "));
    Serial.print(forwardingDecisions[i].dest_host);
    Serial.print(F(" output port "));
    Serial.println(forwardingDecisions[i].output_port);
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
  uint8_t rootIndex = switchIndex(STP_ROOT_SWITCH_ID);
  uint8_t rootSwitch = rootIndex == AF_UNKNOWN_ID ? SWITCH_IDS[0] : STP_ROOT_SWITCH_ID;

  visited[rootIndex == AF_UNKNOWN_ID ? 0 : rootIndex] = true;
  queue[tail] = rootSwitch;
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

    syncPortControl(discoveredPorts[i]);
  }

  stpApplied = true;
  discoveryComplete = true;
  rebuildForwardingDecisions();
  sendFlowDeleteAllToAllSwitches();
  printTopologySummary();
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

  uint8_t outputPort = routeOutputPort(requestingSwitch, missedPacket.dest_id);

  if (controllerPortForSwitch(requestingSwitch) == nullptr || outputPort == AF_NO_PORT) {
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

  sendArduFlowToSwitch(requestingSwitch, flowMod);
  recordPendingFlow(requestingSwitch, outputPort, missedPacket);
  logArduFlowPacket(F("SENT"), flowMod);
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

  for (uint8_t i = 0; i < MAX_PENDING_FLOWS; ++i) {
    if (pendingFlows[i].active &&
        pendingFlows[i].switch_id == message.source_id &&
        pendingFlows[i].output_port == message.port) {
      pendingFlows[i].active = false;
      Serial.println(F("Controller: pending FLOW_MOD acknowledged"));
      return;
    }
  }

  if (isSwitchId(message.source_id) && message.port == message.source_id) {
    Serial.println(F("Controller: switch ID assignment acknowledged"));
    return;
  }

  Serial.println(F("Controller: ACK did not match pending FLOW_MOD"));
}

// PORT_STATUS lets switches notify the controller about usable or failed links.
void handlePortStatus(const ArduFlowPacket &message) {
  Serial.print(F("Controller: PORT_STATUS from "));
  Serial.print(message.source_id);
  Serial.print(F(" port "));
  Serial.print(message.port);
  Serial.print(F(" type "));
  Serial.println(message.type);

  DiscoveredPort *existingPort = findDiscoveredPort(message.source_id, message.port);
  bool wasKnown = existingPort != nullptr;
  bool wasOnline = existingPort != nullptr && existingPort->online;
  uint8_t previousPeer = existingPort != nullptr ? existingPort->peer_id : AF_UNKNOWN_ID;
  DiscoveredPort *discoveredPort = allocateDiscoveredPort(message.source_id, message.port);
  bool nowOnline = message.type == AF_PORT_STATUS_ONLINE;
  uint8_t nowPeer = nowOnline ? message.packet.source_id : AF_UNKNOWN_ID;
  bool isChanged = !wasKnown || wasOnline != nowOnline || previousPeer != nowPeer;

  if (discoveredPort != nullptr) {
    discoveredPort->online = nowOnline;
    discoveredPort->peer_id = nowPeer;
    discoveredPort->blocked = false;
    if (isChanged) {
      discoveredPort->block_state_sent = false;
      discoveredPort->last_sent_blocked = false;
    }
  }

  if (message.type == AF_PORT_STATUS_ONLINE) {
    clearDiscoveredHostOnPort(message.source_id, message.port);
    recordDiscoveredHost(message.packet.source_id, message.source_id, message.port);
  }

  if (message.type == AF_PORT_STATUS_OFFLINE) {
    clearDiscoveredHostOnPort(message.source_id, message.port);
    if (isChanged) {
      logPortDown(message.source_id, message.port);
      logSwitchDownIfNeeded(message.source_id);
    }
  }

  if (stpApplied && isChanged) {
    applySpanningTree();
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
      logArduFlowPacket(F("SENT"), deleteRules);
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
    {AF_CONTROLLER_ID, AF_UNKNOWN_ID, DATA_APP_PING_REQUEST, DATA_SETTING_TIMEOUT_US, PORT_STATUS_PING_TIMEOUT_US},
  };

  writeArduFlowPacket(*port, query);
  logArduFlowPacket(F("SENT"), query);
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
  if (!switchIdsAssigned) {
    if (millis() - assignmentStartMs >= SWITCH_ID_ASSIGNMENT_SETTLE_MS) {
      switchIdsAssigned = true;
      Serial.println(F("Controller: switch ID assignment settle period complete"));
    } else {
      return;
    }
  }

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

void pollHealthCheck() {
  if (!discoveryComplete || !stpApplied) {
    return;
  }

  unsigned long now = millis();
  if (lastHealthQueryMs != 0 && now - lastHealthQueryMs < HEALTH_CHECK_QUERY_INTERVAL_MS) {
    return;
  }

  sendPortStatusQuery(SWITCH_IDS[healthSwitchIndex], healthPort);
  lastHealthQueryMs = now;

  ++healthPort;
  if (healthPort > SWITCH_PORT_COUNT) {
    healthPort = 1;
    ++healthSwitchIndex;
    if (healthSwitchIndex >= sizeof(SWITCH_IDS)) {
      healthSwitchIndex = 0;
    }
  }
}

void pollPendingFlowAcks() {
  unsigned long now = millis();
  for (uint8_t i = 0; i < MAX_PENDING_FLOWS; ++i) {
    if (!pendingFlows[i].active || now - pendingFlows[i].sent_ms < FLOW_ACK_TIMEOUT_MS) {
      continue;
    }

    ArduFlowPacket retry = {
      AF_CONTROLLER_ID,
      pendingFlows[i].switch_id,
      AF_FLOW_ADD_DST_OVERWRITE,
      pendingFlows[i].output_port,
      pendingFlows[i].packet,
    };

    if (!pendingFlows[i].retried) {
      sendArduFlowToSwitch(pendingFlows[i].switch_id, retry);
      pendingFlows[i].sent_ms = now;
      pendingFlows[i].retried = true;
      Serial.println(F("Controller: retrying unacknowledged FLOW_MOD"));
      logArduFlowPacket(F("SENT"), retry);
    } else {
      pendingFlows[i].active = false;
      Serial.println(F("Controller: dropped unacknowledged FLOW_MOD"));
      logArduFlowPacket(F("SENT"), retry);
    }
  }
}

// Dispatches one complete control packet by ArduFlow type.
void handleArduFlowPacket(const ArduFlowPacket &message, const char *inputName) {
  Serial.print(F("Controller input: "));
  Serial.println(inputName);
  logArduFlowPacket(F("RECV"), message);

  if (message.type == AF_ROUTE_REQ) {
    handleRouteRequest(message);
    return;
  }

  if (message.type == AF_ACK) {
    handleAck(message);
    return;
  }

  if (message.type == AF_SET_SWITCH_ID) {
    Serial.println(F("Controller: switch ID assignment acknowledged"));
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
  sendSwitchIdAssignment(NANO_2_ID);
  sendSwitchIdAssignment(NANO_3_ID);
  assignmentStartMs = millis();
}

void roleLoop() {
  pollDiscovery();
  pollHealthCheck();
  pollPendingFlowAcks();

  // Poll every controller link so one idle switch cannot block another.
  for (uint8_t i = 0; i < sizeof(readers) / sizeof(readers[0]); ++i) {
    pollReader(readers[i]);
  }
}

#endif
