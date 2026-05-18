#ifndef NANOSWITCH_H
#define NANOSWITCH_H

#include <Arduino.h>
#include <avr/interrupt.h>
#include <avr/wdt.h>
// EnableInterrupt owns only A0-A2 on PORTC/PCINT1. NeoSWSerial owns D8/PORTB and D4/D6/PORTD.
#define EI_NOTEXTERNAL
#define EI_NOTPORTB
#define EI_NOTPORTD
#include <EnableInterrupt.h>
#include <NeoSWSerial.h>
#include <string.h>
#include "NodeConfig.h"
#include "SdnProtocol.h"

// NeoSWSerial is built with NEOSWSERIAL_EXTERNAL_PCINT so it does not claim PCINT1.
// These handlers preserve NeoSWSerial receive interrupts on the actual RX ports.
#if defined(NEOSWSERIAL_EXTERNAL_PCINT)
ISR(PCINT0_vect) {
  NeoSWSerial::rxISR(PINB);
}

ISR(PCINT2_vect) {
  NeoSWSerial::rxISR(PIND);
}
#endif

uint8_t retainedSwitchId __attribute__((section(".noinit")));
uint8_t retainedSwitchMagic __attribute__((section(".noinit")));

namespace {
uint8_t switchId = AF_UNASSIGNED_ID;
const uint8_t RETAINED_SWITCH_MAGIC = 0xA5;
const uint8_t FLOW_TABLE_SIZE = 8;
const uint8_t STATUS_LED_PIN = A5;
const unsigned long NORMAL_PACKET_BLINK_INTERVAL_MS = 250;
const unsigned long SIGNAL_PACKET_BLINK_INTERVAL_MS = 500;
const uint8_t STATUS_LED_TOGGLE_COUNT = 4;
const unsigned long TRIGGER_READ_WINDOW_MS = 25;
const uint8_t DIAGNOSTIC_LED_PIN = LED_BUILTIN;
const unsigned long DIAGNOSTIC_HEARTBEAT_INTERVAL_MS = 2000;
const unsigned long DIAGNOSTIC_BURST_INTERVAL_MS = 100;
const unsigned long DIAGNOSTIC_QUERY_INTERVAL_MS = 125;
const uint8_t DIAGNOSTIC_QUEUE_SIZE = 8;
const uint8_t QUERY_RECEIVED_DIAGNOSTIC_TOGGLES = 2;
const uint8_t PING_SENT_DIAGNOSTIC_TOGGLES = 4;
const uint8_t DATA_RECEIVED_DIAGNOSTIC_TOGGLES = 6;
const uint8_t PING_REPLY_DIAGNOSTIC_TOGGLES = 8;
const uint8_t QUERY_TIMEOUT_DIAGNOSTIC_TOGGLES = 10;
const uint8_t ROUTE_REQ_DIAGNOSTIC_TOGGLES = 12;
const uint8_t ID_RESET_DIAGNOSTIC_TOGGLES = 14;
const uint8_t TABLE_FULL_DIAGNOSTIC_TOGGLES = 16;
const unsigned long SWITCH_ID_RESET_DELAY_MS = 50;
const bool FORCE_PORT_STATUS_TEST_LISTEN = true;

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
bool switchIdResetPending = false;
unsigned long switchIdResetAtMs = 0;
bool statusLedActive = false;
bool statusLedState = false;
uint8_t statusLedTogglesRemaining = 0;
unsigned long statusLedIntervalMs = 0;
unsigned long lastStatusLedToggleMs = 0;
bool diagnosticLedState = false;
bool diagnosticBurstActive = false;
uint8_t diagnosticTogglesRemaining = 0;
unsigned long diagnosticIntervalMs = 0;
unsigned long lastDiagnosticToggleMs = 0;
uint8_t diagnosticQueue[DIAGNOSTIC_QUEUE_SIZE];
uint8_t diagnosticQueueLength = 0;
// Trigger pins choose which NeoSWSerial port should listen next.
// EnableInterrupt sets these flags from A0-A2 without claiming NeoSWSerial's RX vectors.
volatile bool triggerPending[SWITCH_PORT_COUNT] = {false};
uint8_t activeListenPort = AF_NO_PORT;
unsigned long activeListenStartMs = 0;

void markPort1Triggered() {
  triggerPending[0] = true;
}

void markPort2Triggered() {
  triggerPending[1] = true;
}

void markPort3Triggered() {
  triggerPending[2] = true;
}

bool isValidSwitchId(uint8_t candidateId) {
  for (uint8_t i = 0; i < sizeof(SWITCH_IDS); ++i) {
    if (SWITCH_IDS[i] == candidateId) {
      return true;
    }
  }

  return false;
}

uint8_t forcedTestListenPort() {
  if (!FORCE_PORT_STATUS_TEST_LISTEN) {
    return AF_NO_PORT;
  }
  if (switchId == NANO_1_ID) {
    return 2;
  }
  if (switchId == NANO_2_ID) {
    return 1;
  }

  return AF_NO_PORT;
}

void loadRetainedSwitchId() {
  if (retainedSwitchMagic == RETAINED_SWITCH_MAGIC && isValidSwitchId(retainedSwitchId)) {
    switchId = retainedSwitchId;
  } else {
    switchId = AF_UNASSIGNED_ID;
  }
}

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
  delayMicroseconds(PORT_TRIGGER_LEAD_US);
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

void beginDiagnosticBurst(uint8_t toggles, unsigned long intervalMs) {
  diagnosticBurstActive = true;
  diagnosticLedState = true;
  diagnosticTogglesRemaining = toggles;
  diagnosticIntervalMs = intervalMs;
  lastDiagnosticToggleMs = millis();
  digitalWrite(DIAGNOSTIC_LED_PIN, HIGH);
}

// D13 is a close-range diagnostic LED: heartbeat when assigned, queued bursts for events.
void startDiagnosticBurst(uint8_t toggles, unsigned long intervalMs) {
  if (!diagnosticBurstActive) {
    beginDiagnosticBurst(toggles, intervalMs);
    return;
  }

  if (diagnosticQueueLength >= DIAGNOSTIC_QUEUE_SIZE) {
    return;
  }

  diagnosticQueue[diagnosticQueueLength] = toggles;
  ++diagnosticQueueLength;
}

void startNextQueuedDiagnosticBurst() {
  if (diagnosticQueueLength == 0) {
    return;
  }

  uint8_t toggles = diagnosticQueue[0];
  for (uint8_t i = 1; i < diagnosticQueueLength; ++i) {
    diagnosticQueue[i - 1] = diagnosticQueue[i];
  }
  --diagnosticQueueLength;
  beginDiagnosticBurst(toggles, DIAGNOSTIC_BURST_INTERVAL_MS);
}

void pollDiagnosticLed() {
  unsigned long now = millis();

  if (diagnosticBurstActive) {
    if (now - lastDiagnosticToggleMs < diagnosticIntervalMs) {
      return;
    }

    lastDiagnosticToggleMs = now;
    diagnosticLedState = !diagnosticLedState;
    digitalWrite(DIAGNOSTIC_LED_PIN, diagnosticLedState ? HIGH : LOW);

    if (diagnosticTogglesRemaining > 0) {
      --diagnosticTogglesRemaining;
    }

    if (diagnosticTogglesRemaining == 0) {
      diagnosticBurstActive = false;
      diagnosticLedState = false;
      digitalWrite(DIAGNOSTIC_LED_PIN, LOW);
      lastDiagnosticToggleMs = now;
      startNextQueuedDiagnosticBurst();
    }
    return;
  }

  if (pendingQuery.active) {
    if (now - lastDiagnosticToggleMs >= DIAGNOSTIC_QUERY_INTERVAL_MS) {
      lastDiagnosticToggleMs = now;
      diagnosticLedState = !diagnosticLedState;
      digitalWrite(DIAGNOSTIC_LED_PIN, diagnosticLedState ? HIGH : LOW);
    }
    return;
  }

  if (switchId == AF_UNASSIGNED_ID) {
    diagnosticLedState = false;
    digitalWrite(DIAGNOSTIC_LED_PIN, LOW);
    return;
  }

  if (now - lastDiagnosticToggleMs >= DIAGNOSTIC_HEARTBEAT_INTERVAL_MS) {
    lastDiagnosticToggleMs = now;
    diagnosticLedState = !diagnosticLedState;
    digitalWrite(DIAGNOSTIC_LED_PIN, diagnosticLedState ? HIGH : LOW);
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

void scheduleWatchdogReset() {
  switchIdResetPending = true;
  switchIdResetAtMs = millis() + SWITCH_ID_RESET_DELAY_MS;
}

void retainSwitchIdForWatchdogReset(uint8_t assignedId) {
  retainedSwitchId = assignedId;
  retainedSwitchMagic = RETAINED_SWITCH_MAGIC;
}

void pollScheduledReset() {
  if (!switchIdResetPending || static_cast<long>(millis() - switchIdResetAtMs) < 0) {
    return;
  }

  wdt_enable(WDTO_15MS);
  while (true) {
  }
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

// Installs a controller rule and returns whether the switch can now forward the embedded packet.
bool installFlowRule(const ArduFlowPacket &message) {
  FlowRule *target = nullptr;

  for (uint8_t i = 0; i < FLOW_TABLE_SIZE; ++i) {
    if (sameMatch(flowTable[i], message.type, message.packet)) {
      if (!isOverwriteFlowType(message.type)) {
        sendAck(message.port);
        return true;
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
    startDiagnosticBurst(TABLE_FULL_DIAGNOSTIC_TOGGLES, DIAGNOSTIC_BURST_INTERVAL_MS);
    return false;
  }

  target->type = message.type;
  target->source_id = message.packet.source_id;
  target->dest_id = message.packet.dest_id;
  target->output_port = message.port;
  target->used = true;
  sendAck(message.port);
  return true;
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
  startDiagnosticBurst(ROUTE_REQ_DIAGNOSTIC_TOGGLES, DIAGNOSTIC_BURST_INTERVAL_MS);
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
  startDiagnosticBurst(DATA_RECEIVED_DIAGNOSTIC_TOGGLES, DIAGNOSTIC_BURST_INTERVAL_MS);

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
    startDiagnosticBurst(PING_REPLY_DIAGNOSTIC_TOGGLES, DIAGNOSTIC_BURST_INTERVAL_MS);
    sendPortStatus(AF_PORT_STATUS_ONLINE, ingressPort, packet);
    return;
  }

  forwardPacket(ingressPort, packet);
}

bool pollDataPort(uint8_t portNumber) {
  SwitchPort *port = portByNumber(portNumber);
  if (port == nullptr) {
    return false;
  }

  bool packetHandled = false;
  port->serial->listen();
  while (port->serial->available() > 0) {
    port->bytes[port->length] = static_cast<uint8_t>(port->serial->read());
    ++port->length;

    if (port->length == sizeof(DataPacket)) {
      DataPacket packet;
      memcpy(&packet, port->bytes, sizeof(packet));
      port->length = 0;
      handleDataPacket(portNumber, packet);
      packetHandled = true;
    }
  }

  return packetHandled;
}

void pollTriggeredDataPorts() {
  uint8_t forcedPort = forcedTestListenPort();
  if (forcedPort != AF_NO_PORT) {
    pollDataPort(forcedPort);
    return;
  }

  if (activeListenPort == AF_NO_PORT) {
    for (uint8_t i = 0; i < SWITCH_PORT_COUNT; ++i) {
      if (triggerPending[i]) {
        noInterrupts();
        triggerPending[i] = false;
        interrupts();
        activeListenPort = i + 1;
        activeListenStartMs = millis();
        break;
      }
    }
  }

  if (activeListenPort == AF_NO_PORT) {
    return;
  }

  bool packetHandled = pollDataPort(activeListenPort);
  if (packetHandled || millis() - activeListenStartMs >= TRIGGER_READ_WINDOW_MS) {
    activeListenPort = AF_NO_PORT;
  }
}

void startPortStatusQuery(const ArduFlowPacket &message) {
  startDiagnosticBurst(QUERY_RECEIVED_DIAGNOSTIC_TOGGLES, DIAGNOSTIC_BURST_INTERVAL_MS);
  SwitchPort *port = portByNumber(message.port);
  if (port == nullptr) {
    sendPortStatus(AF_PORT_STATUS_OFFLINE, message.port, {0, 0, 0, 0, 0});
    return;
  }

  pendingQuery.active = true;
  pendingQuery.port = message.port;
  pendingQuery.deadline_ms = millis() + (message.packet.data / 1000UL);
  writeDataPacket(*port, message.packet);
  startDiagnosticBurst(PING_SENT_DIAGNOSTIC_TOGGLES, DIAGNOSTIC_BURST_INTERVAL_MS);
}

void handleControllerMessage(const ArduFlowPacket &message) {
  if (message.type == AF_SET_SWITCH_ID &&
      (message.dest_id == AF_UNASSIGNED_ID || message.dest_id == switchId)) {
    switchId = message.port;
    retainSwitchIdForWatchdogReset(switchId);
    startStatusBlink(SIGNAL_PACKET_BLINK_INTERVAL_MS);
    startDiagnosticBurst(ID_RESET_DIAGNOSTIC_TOGGLES, DIAGNOSTIC_BURST_INTERVAL_MS);
    sendAck(switchId);
    scheduleWatchdogReset();
    return;
  }

  if (message.dest_id != switchId) {
    return;
  }

  startStatusBlink(SIGNAL_PACKET_BLINK_INTERVAL_MS);

  if (isFlowAddType(message.type)) {
    if (installFlowRule(message)) {
      forwardPacket(AF_NO_PORT, message.packet);
    }
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
    startDiagnosticBurst(QUERY_TIMEOUT_DIAGNOSTIC_TOGGLES, DIAGNOSTIC_BURST_INTERVAL_MS);
    sendPortStatus(AF_PORT_STATUS_OFFLINE, port, {0, 0, 0, 0, 0});
  }
}
}

void roleSetup() {
  wdt_disable();
  loadRetainedSwitchId();
  Serial.begin(CONTROL_BAUD);
  pinMode(STATUS_LED_PIN, OUTPUT);
  pinMode(DIAGNOSTIC_LED_PIN, OUTPUT);
  digitalWrite(STATUS_LED_PIN, LOW);
  digitalWrite(DIAGNOSTIC_LED_PIN, LOW);

  for (uint8_t i = 0; i < SWITCH_PORT_COUNT; ++i) {
    switchPorts[i].serial->begin(SWITCH_PORT_BAUD);
    pinMode(switchPorts[i].trigger_out, OUTPUT);
    pinMode(switchPorts[i].trigger_in, INPUT);
    digitalWrite(switchPorts[i].trigger_out, LOW);
  }

  enableInterrupt(switchPorts[0].trigger_in, markPort1Triggered, RISING);
  enableInterrupt(switchPorts[1].trigger_in, markPort2Triggered, RISING);
  enableInterrupt(switchPorts[2].trigger_in, markPort3Triggered, RISING);
}

void roleLoop() {
  pollController();
  pollTriggeredDataPorts();
  pollPendingQuery();
  pollStatusLed();
  pollDiagnosticLed();
  pollScheduledReset();
}

#endif
