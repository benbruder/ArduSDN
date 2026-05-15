# AGENTS.md

## Project Summary

This is an Arduino/PlatformIO final project demonstrating a simplified SDN-like network on microcontrollers.

The design is not full OpenFlow or enterprise SDN. It is a lightweight educational SDN model:
- Control-plane logic is centralized.
- Data-plane forwarding is handled by Nano-based switches.
- Hosts are simple Uno boards.
- Messages are simple fixed-size packed binary packets sent over serial.

## Hardware Architecture

Mega 2 is the main SDN controller.
Mega 1 is only a serial relay / southbound proxy.
Mega 1 must not make routing decisions.

Switch Connections:
The Nanos communicate with each other at 9600 baud using NeoSWSerial, as follows:
Each Nano uses NeoSWSerial on one of 3 virtual ports (where "Port 0" is its hardware serial):
 - Port 1: RX on D4, TX on D5, Trigger on D10, Interrupt on A0
 - Port 2: RX on D6, TX on D7, Trigger on D11, Interrupt on A1
 - Port 3: RX on D8, TX on D9, Trigger on D12, Interrupt on A2
The "Trigger"s are outputs and the "Interrupt"s are inputs. When sending a packet to the Nano, the sender activates its trigger line (which is wired directly to the receiver's trigger), waits 50 microseconds, then sends the data, turning off the trigger line afterwards.
Regarding how the Ports are connected, the lower-numbered port is associated with a higher priority device, where a host Uno would have the highest priority, followed by the connected Nano with the lowest index (the index of "Nano 2" is 2, etc.).

Controller Connections:
- Mega 2 Serial0: USB serial monitor/debugging.
- Mega 2 Serial1: connected to Mega 1 Serial1.
- Mega 2 Serial2: connected to Nano 2 Serial.
- Mega 2 Serial3: connected to Nano 3 Serial.

- Mega 1 Serial0: USB serial monitor/debugging.
- Mega 1 Serial1: connected to Mega 2 Serial1.
- Mega 1 Serial2: connected to Nano 1 Serial.
- Mega 1 Serial3: connected to Nano 4 Serial.

- Nanos are SDN switches.
- Unos are hosts/end devices.

## File Names

Use these files:
- src/main.cpp
- include/SdnProtocol.h
- include/NodeConfig.h, if needed
- src/controller.h
- src/relaymega.h
- src/nanoswitch.h
- src/unohost.h

Do not use these older names:
- controller_mega2.h
- relay_mega1.h
- switch_nano.h
- host_uno.h

## Firmware Roles

### Mega 2: Controller

File: src/controller.h

Responsibilities:
- Own the global SDN control logic.
- Receive ROUTE_REQ messages from switches.
- Install FLOW_MOD rules.
- Send messages to N1/N4 through Mega 1 over Serial1.
- Send messages to N2 over Serial2.
- Send messages to N3 over Serial3.
- Print useful debugging info to Serial0.
- Implement or simulate STP
Other:
- Controller performs startup port discovery, computes a logical spanning tree, and periodically health-checks ports.
- Controller may send FLOW_DELETE_ALL after topology/STP changes to clear stale switch rules.
- Controller roots STP at STP_ROOT_SWITCH_ID.


### Mega 1: Relay

File: src/relaymega.h

Responsibilities:
- Act only as a serial relay/proxy.
- Forward messages from N1 and N4 to Mega 2.
- Forward messages from Mega 2 to N1 or N4.
- Do not compute paths.
- Do not install rules independently.
- Do not behave as a second controller.

### Nano Switches

File: src/nanoswitch.h

Responsibilities:
- Maintain a tiny flow table.
- Forward packets based on installed flow rules.
- Send ROUTE_REQ to Mega 2 when destination is unknown.
- Accept FLOW_MOD, PORT_STATUS, and BLOCK_PORT/UNBLOCK_PORT messages.
- Avoid Arduino String.
- Use fixed-size buffers.

### Uno Hosts

File: src/unohost.h

Responsibilities:
- Generate simple test traffic.
- Receive and print/debug simple messages.
- Be simple enough for a live class demo.

## Coding Rules

- Use fixed-size char buffers.
- Avoid Arduino String, especially on Nanos and Unos.
- Avoid dynamic memory allocation.
- Avoid delay() in normal loop logic.
- Use non-blocking serial reads.
- Keep message format human-readable at first.
- Prefer simple, reliable code over clever abstractions.
- Each role file must expose:

void roleSetup();
void roleLoop();


## SDN Packets

struct DataPacket {  // Used to send actual data
    uint8_t source_id;
    uint8_t dest_id;   // 255 means unknown/unspecified, such as when pinging a port
    uint8_t app_id;
    uint8_t setting;
    uint32_t data;
}

 - For a PING: app_id=0 for an outgoing ping (if setting=1 then 'data' is the amount of microseconds the sender should wait for a ping reply) and app_id=1 for a ping reply ('setting' and 'data' are 0)

struct ArduFlowPacket { // used for communication between controller and switches
    uint8_t source_id;      
    uint8_t dest_id;
    uint8_t type;         // 0 = ACK, 1 = ROUTE_REQ, 20-29 = FLOW_MOD, 30-32 = PORT_STATUS, 40 = BLOCK_PORT, 41 = UNBLOCK_PORT
    uint8_t port;    // either 1, 2, or 3 depending on the port the packet in question came from
    DataPacket packet;
}

FLOW_MOD elaboration:
 - 20: Add, match 'source_id' of attached packet (do not overwrite if already exists)
 - 21: Add, match 'source_id' of attached packet (overwrite any existing rule)
 - 22: Add, match 'dest_id' of attached packet (do not overwrite if already exists)
 - 23: Add, match 'dest_id' of attached packet (overwrite any existing rule)
 - 24: Add, match BOTH 'source_id' and 'dest_id' of attached packet (do not overwrite if already exists)
 - 25: Add, match BOTH 'source_id' and 'dest_id' of attached packet (overwrite any existing rule)
 - 26: Delete any rule whose action outputs to 'port' (this is used when a disconnection is detected)
 - 27: Delete any rule whose match is the 'source_id' of the attached packet
 - 28: Delete any rule whose match is the 'dest_id' of the attached packet
 - 29: Delete ALL flow rules

PORT_STATUS elaboration:
 - 30: Query status of 'port' ('packet' is the packet the target switch should send to the specified port, which may contain a timeout, as described above)
 - 31: Assert 'port' as online, with 'packet' containing the ping response the sending switch received upon querying the port, in order to inform the controller what port of the switch in question is connected to what device
 - 32: Assert 'port' as offline (packet would be blank)

BLOCK_PORT:
 - 40: Logically block 'port'; data packets must not forward out this port, but control/probe packets may still use it.

UNBLOCK_PORT:
 - 41: Return 'port' to normal forwarding.


source_id and dest_id will (when used in a normal packet) will be either 100 for Uno 1 or 200 for Uno 2. When used in a signaling packet (i.e. only in communication between a switch and the controller), the controller has an id of 0, Nano 1 has an id of 10, Nano 2 has an id of 20, Nano 3 has an id of 30, Nano 4 has an id of 40. If the port doesn't matter, it's set to 255, as shown above.

Packet structs must be packed and sent as raw bytes. Implementations must use sizeof(...) and static_assert size checks.


## PlatformIO Build Environments

Expected environments:
- mega2_controller
- mega1_relay
- nano_switch
- uno_host

Build commands:
pio run -e mega2_controller
pio run -e mega1_relay
pio run -e nano_switch
pio run -e uno_host

## Important Instruction

Make small, testable changes.
Do not ever rewrite the entire project
After editing code, explain exactly which files changed and how to test them.