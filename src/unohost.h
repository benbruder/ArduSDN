#ifndef UNOHOST_H
#define UNOHOST_H

#include <Arduino.h>
#include <SevSeg.h>
#include <string.h>
#include "NodeConfig.h"
#include "SdnProtocol.h"

namespace {
#ifndef NODE_ID
#define NODE_ID 100
#endif

const uint8_t HOST_ID = static_cast<uint8_t>(NODE_ID);
const uint8_t PEER_HOST_ID = HOST_ID == UNO_1_ID ? UNO_2_ID : UNO_1_ID;
const uint8_t HOST_TX_ENABLE_PIN = 2;
const uint8_t LED_MESSAGE_DISPLAY_PIN = 4;
const uint8_t LED_QUEUE_FULL_PIN = 5;
const uint8_t BUTTON_COMPOSE_PIN = A0;
const uint8_t BUTTON_ACTION_PIN = A1;
const unsigned long HOST_SERIAL_BAUD = SWITCH_PORT_BAUD;
const unsigned long DEBOUNCE_MS = 30;
const unsigned long SENT_DISPLAY_MS = 1000;
const unsigned long QUEUE_FULL_FLASH_MS = 1000;
const unsigned long MESSAGE_LED_INTERVAL_MS = 500;
const unsigned long FAST_INCREMENT_INTERVAL_MS = 250;
const uint8_t RECEIVE_QUEUE_SIZE = 4;

// SevSeg uses bit order .GFEDCBA. These codes let us preserve decimal points
// as queue indicators while showing hex digits and short status text.
const uint8_t SEG_A = 0x01;
const uint8_t SEG_B = 0x02;
const uint8_t SEG_C = 0x04;
const uint8_t SEG_D = 0x08;
const uint8_t SEG_E = 0x10;
const uint8_t SEG_F = 0x20;
const uint8_t SEG_G = 0x40;
const uint8_t SEG_DP = 0x80;

enum DisplayMode {
  DISPLAY_IDLE,
  DISPLAY_COMPOSE,
  DISPLAY_SENT,
  DISPLAY_RECEIVED,
};

struct ButtonState {
  bool stable_pressed;
  bool last_reading;
  bool pressed_event;
  unsigned long last_change_ms;
  unsigned long pressed_since_ms;
};

SevSeg sevseg;
ButtonState composeButton = {false, false, false, 0, 0};
ButtonState actionButton = {false, false, false, 0, 0};

// Serial packets arrive as fixed 8-byte DataPacket frames from the attached switch.
uint8_t rxBytes[sizeof(DataPacket)];
uint8_t rxLength = 0;

// The receive queue backs the decimal-point indicators on the idle/compose display.
uint32_t receiveQueue[RECEIVE_QUEUE_SIZE];
uint8_t receiveQueueLength = 0;

// Display mode controls whether the user is idle, composing, confirming send, or reading.
DisplayMode displayMode = DISPLAY_IDLE;
uint16_t composeValue = 0;
uint16_t displayedMessage = 0;
unsigned long sentDisplayUntilMs = 0;
unsigned long queueFullFlashUntilMs = 0;
unsigned long lastMessageLedToggleMs = 0;
unsigned long lastFastIncrementMs = 0;
bool messageLedState = false;

uint8_t hexSegmentCode(uint8_t value) {
  static const uint8_t codes[] = {
    SEG_A | SEG_B | SEG_C | SEG_D | SEG_E | SEG_F,
    SEG_B | SEG_C,
    SEG_A | SEG_B | SEG_D | SEG_E | SEG_G,
    SEG_A | SEG_B | SEG_C | SEG_D | SEG_G,
    SEG_B | SEG_C | SEG_F | SEG_G,
    SEG_A | SEG_C | SEG_D | SEG_F | SEG_G,
    SEG_A | SEG_C | SEG_D | SEG_E | SEG_F | SEG_G,
    SEG_A | SEG_B | SEG_C,
    SEG_A | SEG_B | SEG_C | SEG_D | SEG_E | SEG_F | SEG_G,
    SEG_A | SEG_B | SEG_C | SEG_D | SEG_F | SEG_G,
    SEG_A | SEG_B | SEG_C | SEG_E | SEG_F | SEG_G,
    SEG_C | SEG_D | SEG_E | SEG_F | SEG_G,
    SEG_A | SEG_D | SEG_E | SEG_F,
    SEG_B | SEG_C | SEG_D | SEG_E | SEG_G,
    SEG_A | SEG_D | SEG_E | SEG_F | SEG_G,
    SEG_A | SEG_E | SEG_F | SEG_G,
  };

  return codes[value & 0x0F];
}

uint8_t queueDecimalPoint(uint8_t digitIndex) {
  return digitIndex < receiveQueueLength ? SEG_DP : 0;
}

void showDashesWithQueue() {
  uint8_t segments[4] = {
    static_cast<uint8_t>(SEG_G | queueDecimalPoint(0)),
    static_cast<uint8_t>(SEG_G | queueDecimalPoint(1)),
    static_cast<uint8_t>(SEG_G | queueDecimalPoint(2)),
    static_cast<uint8_t>(SEG_G | queueDecimalPoint(3)),
  };
  sevseg.setSegments(segments);
}

void showHexWithQueue(uint16_t value) {
  uint8_t segments[4];
  for (uint8_t i = 0; i < 4; ++i) {
    uint8_t shift = (3 - i) * 4;
    segments[i] = hexSegmentCode((value >> shift) & 0x0F) | queueDecimalPoint(i);
  }
  sevseg.setSegments(segments);
}

void showSentWithQueue() {
  uint8_t segments[4] = {
    static_cast<uint8_t>(SEG_A | SEG_C | SEG_D | SEG_F | SEG_G | queueDecimalPoint(0)), // S
    static_cast<uint8_t>(SEG_A | SEG_D | SEG_E | SEG_F | SEG_G | queueDecimalPoint(1)), // E
    static_cast<uint8_t>(SEG_C | SEG_E | SEG_G | queueDecimalPoint(2)),                 // n
    static_cast<uint8_t>(SEG_D | SEG_E | SEG_F | SEG_G | queueDecimalPoint(3)),         // t
  };
  sevseg.setSegments(segments);
}

void refreshDisplayContent() {
  if (displayMode == DISPLAY_COMPOSE) {
    showHexWithQueue(composeValue);
  } else if (displayMode == DISPLAY_SENT) {
    showSentWithQueue();
  } else if (displayMode == DISPLAY_RECEIVED) {
    showHexWithQueue(displayedMessage);
  } else {
    showDashesWithQueue();
  }
}

void updateButton(ButtonState &button, uint8_t pin) {
  bool reading = digitalRead(pin) == LOW;
  unsigned long now = millis();
  button.pressed_event = false;

  if (reading != button.last_reading) {
    button.last_reading = reading;
    button.last_change_ms = now;
  }

  if (now - button.last_change_ms < DEBOUNCE_MS || reading == button.stable_pressed) {
    return;
  }

  button.stable_pressed = reading;
  if (button.stable_pressed) {
    button.pressed_event = true;
    button.pressed_since_ms = now;
    lastFastIncrementMs = now;
  }
}

// Host packets use hardware Serial at the Nano NeoSWSerial port rate.
// D2 is the host-side trigger line wired to the selected Nano port interrupt input.
void writeDataPacket(const DataPacket &packet) {
  digitalWrite(HOST_TX_ENABLE_PIN, HIGH);
  delayMicroseconds(50);
  Serial.write(reinterpret_cast<const uint8_t *>(&packet), sizeof(packet));
  Serial.flush();
  digitalWrite(HOST_TX_ENABLE_PIN, LOW);
}

// Sends the composed 16-bit value inside the project app packet format from AGENTS.md.
void sendComposedValue() {
  DataPacket packet = {
    HOST_ID,
    PEER_HOST_ID,
    67,
    21,
    composeValue,
  };

  writeDataPacket(packet);
  displayMode = DISPLAY_SENT;
  sentDisplayUntilMs = millis() + SENT_DISPLAY_MS;
  refreshDisplayContent();
}

// Stores app messages until the user chooses to display them; full queue flashes LED 2.
void enqueueReceivedValue(uint32_t value) {
  if (receiveQueueLength >= RECEIVE_QUEUE_SIZE) {
    queueFullFlashUntilMs = millis() + QUEUE_FULL_FLASH_MS;
    digitalWrite(LED_QUEUE_FULL_PIN, HIGH);
    return;
  }

  receiveQueue[receiveQueueLength] = value;
  ++receiveQueueLength;
  refreshDisplayContent();
}

// Dequeues the oldest message and truncates to 16 bits for the 4-digit display.
bool popReceivedValue(uint16_t &value) {
  if (receiveQueueLength == 0) {
    return false;
  }

  value = static_cast<uint16_t>(receiveQueue[0] & 0xFFFF);
  for (uint8_t i = 1; i < receiveQueueLength; ++i) {
    receiveQueue[i - 1] = receiveQueue[i];
  }
  --receiveQueueLength;
  return true;
}

// Handles discovery pings from the switch and user data from the peer host.
void handleReceivedPacket(const DataPacket &packet) {
  if (packet.dest_id != HOST_ID && packet.dest_id != AF_UNKNOWN_ID) {
    return;
  }

  if (packet.app_id == DATA_APP_PING_REQUEST) {
    DataPacket reply = {
      HOST_ID,
      packet.source_id,
      DATA_APP_PING_REPLY,
      DATA_SETTING_NONE,
      0,
    };
    writeDataPacket(reply);
    return;
  }

  if (packet.app_id == 67 && packet.setting == 21) {
    enqueueReceivedValue(packet.data);
  }
}

// Non-blocking fixed-size packet reader for the host's hardware serial link.
void pollSerialPackets() {
  while (Serial.available() > 0) {
    rxBytes[rxLength] = static_cast<uint8_t>(Serial.read());
    ++rxLength;

    if (rxLength == sizeof(DataPacket)) {
      DataPacket packet;
      memcpy(&packet, rxBytes, sizeof(packet));
      rxLength = 0;
      handleReceivedPacket(packet);
    }
  }
}

// Long-hold composition moves from the low digit to increasingly significant digits.
uint8_t heldDigitIndex(unsigned long heldMs) {
  if (heldMs >= 7000) {
    return 3;
  }
  if (heldMs >= 5000) {
    return 2;
  }
  if (heldMs >= 3000) {
    return 1;
  }
  return 0;
}

void incrementComposeDigit(uint8_t digitIndex) {
  uint16_t increment = 1;
  for (uint8_t i = 0; i < digitIndex; ++i) {
    increment *= 16;
  }
  composeValue = static_cast<uint16_t>(composeValue + increment);
  refreshDisplayContent();
}

void pollComposeHold() {
  if (displayMode != DISPLAY_COMPOSE || !composeButton.stable_pressed) {
    return;
  }

  unsigned long now = millis();
  unsigned long heldMs = now - composeButton.pressed_since_ms;
  if (heldMs < 1000 || now - lastFastIncrementMs < FAST_INCREMENT_INTERVAL_MS) {
    return;
  }

  lastFastIncrementMs = now;
  incrementComposeDigit(heldDigitIndex(heldMs));
}

// Button 1 starts/edits composition; Button 2 sends or opens the receive queue.
void handleButtonActions() {
  if (displayMode == DISPLAY_RECEIVED &&
      (composeButton.pressed_event || actionButton.pressed_event)) {
    displayMode = DISPLAY_IDLE;
    messageLedState = false;
    digitalWrite(LED_MESSAGE_DISPLAY_PIN, LOW);
    refreshDisplayContent();
    return;
  }

  if (composeButton.pressed_event) {
    if (displayMode != DISPLAY_COMPOSE) {
      composeValue = 0;
      displayMode = DISPLAY_COMPOSE;
      refreshDisplayContent();
    } else {
      incrementComposeDigit(0);
    }
  }

  if (!actionButton.pressed_event) {
    return;
  }

  if (displayMode == DISPLAY_COMPOSE) {
    sendComposedValue();
    return;
  }

  if (displayMode == DISPLAY_IDLE && receiveQueueLength > 0) {
    if (popReceivedValue(displayedMessage)) {
      displayMode = DISPLAY_RECEIVED;
      lastMessageLedToggleMs = millis();
      messageLedState = true;
      digitalWrite(LED_MESSAGE_DISPLAY_PIN, HIGH);
      refreshDisplayContent();
    }
  }
}

// Clears temporary display/LED states without using delay().
void pollTimedStates() {
  unsigned long now = millis();

  if (displayMode == DISPLAY_SENT && static_cast<long>(now - sentDisplayUntilMs) >= 0) {
    displayMode = DISPLAY_IDLE;
    refreshDisplayContent();
  }

  if (queueFullFlashUntilMs != 0 && static_cast<long>(now - queueFullFlashUntilMs) >= 0) {
    queueFullFlashUntilMs = 0;
    digitalWrite(LED_QUEUE_FULL_PIN, LOW);
  }

  if (displayMode == DISPLAY_RECEIVED && now - lastMessageLedToggleMs >= MESSAGE_LED_INTERVAL_MS) {
    lastMessageLedToggleMs = now;
    messageLedState = !messageLedState;
    digitalWrite(LED_MESSAGE_DISPLAY_PIN, messageLedState ? HIGH : LOW);
  }
}
}

void roleSetup() {
  Serial.begin(HOST_SERIAL_BAUD);
  pinMode(HOST_TX_ENABLE_PIN, OUTPUT);
  pinMode(LED_MESSAGE_DISPLAY_PIN, OUTPUT);
  pinMode(LED_QUEUE_FULL_PIN, OUTPUT);
  pinMode(BUTTON_COMPOSE_PIN, INPUT_PULLUP);
  pinMode(BUTTON_ACTION_PIN, INPUT_PULLUP);
  digitalWrite(HOST_TX_ENABLE_PIN, LOW);
  digitalWrite(LED_MESSAGE_DISPLAY_PIN, LOW);
  digitalWrite(LED_QUEUE_FULL_PIN, LOW);

  byte digitPins[] = {13, 12, 11, 10};
  byte segmentPins[] = {8, 6, A5, A3, A2, 7, 9, A4};
  const bool resistorsOnSegments = true;
  const bool updateWithDelays = false;
  const bool leadingZeros = true;
  const bool disableDecPoint = false;
  sevseg.begin(COMMON_CATHODE, 4, digitPins, segmentPins, resistorsOnSegments,
               updateWithDelays, leadingZeros, disableDecPoint);
  sevseg.setBrightness(80);
  refreshDisplayContent();
}

void roleLoop() {
  sevseg.refreshDisplay();
  updateButton(composeButton, BUTTON_COMPOSE_PIN);
  updateButton(actionButton, BUTTON_ACTION_PIN);
  handleButtonActions();
  pollComposeHold();
  pollSerialPackets();
  pollTimedStates();
}

#endif
