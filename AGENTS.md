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
Network Layout (direct connections):
 - Host 1 connected to Nano 1, and
 - Nano 1 connected to Nano 2 and Nano 3, and
 - Nano 2 connected to Nano 3 and Nano 4, and
 - Nano 3 connected to Nano 4, and
 - Nano 4 connected to Host 2

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

Minimum "useful debugging info" to be outputted:
- Startup message
- Whenever STP is run, and its results in human-readable format
- Whenever a signal packet is sent, and the human-readable form of that packet
    - e.g. if a packet is sent to N2 to block port 3, output: "SENT; To: N2; From: M2; Type: BLOCK_PORT; Port: 3; Packet: {a human readable form of the packet, if there is one}"
- Whenever a signal packet is received, and the human-readable form of that packet
- Whenever a port is detected to be down
- Whenever a switch is detected to be down
- Any other useful information for debugging

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

Additionally, there is an output LED on pin A5 of each switch. When receiving a normal packet (i.e. originating from a Host), blink this LED twice in one second. When receiving a signaling packet (i.e. originating from the Mega), blink this LED twice in two seconds (i.e. on for .5, off for .5, twice). Make sure this does not use "delay()" or other functions that pause execution for any amount of time; instead use asynchronous delay methods.

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


struct ArduFlowPacket { // used for communication between controller and switches
    uint8_t source_id;      
    uint8_t dest_id;
    uint8_t type;         // 0 = ACK, 1 = ROUTE_REQ, 20-29 = FLOW_MOD, 30-32 = PORT_STATUS, 40 = BLOCK_PORT, 41 = UNBLOCK_PORT, 100 = SET_SWITCH_ID
    uint8_t port;    // either 1, 2, or 3 depending on the port the packet in question came from
    DataPacket packet;
}

- DataPacket - For a PING: app_id=0 for an outgoing ping (if setting=1 then 'data' is the amount of microseconds the sender should wait for a ping reply) and app_id=1 for a ping reply ('setting' and 'data' are 0)

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

SET_SWITCH_ID:
 - 100: set a switch's ID to the value in 'port'


source_id and dest_id will (when used in a normal packet) will be either 100 for Uno 1 or 200 for Uno 2. When used in a signaling packet (i.e. only in communication between a switch and the controller), the controller has an id of 0, Nano 1 has an id of 10, Nano 2 has an id of 20, Nano 3 has an id of 30, Nano 4 has an id of 40. If the port doesn't matter, it's set to 255, as shown above.

Packet structs must be packed and sent as raw bytes. Implementations must use sizeof(...) and static_assert size checks.


## PlatformIO Build Environments

Expected environments:
- mega2_controller
- mega1_relay
- nano_switch
- uno1_host
- uno2_host

Build commands:
pio run -e mega2_controller
pio run -e mega1_relay
pio run -e nano_switch
pio run -e uno1_host
pio run -e uno2_host

## Important Instruction

Make small, testable changes.
Do not ever rewrite the entire project
After editing code, explain exactly which files changed and how to test them.

## Host IO

Each host device is connected to its switch using its hardware TX/RX pins, using pin 2 as the outbound transmission pin (as elaborated on earlier). It does not need a trigger pin, as it is using hardware serial.

It has a 5461AS 4-digit seven segment display (common cathode) connected to the following pins:
- Digit 1-4 common cathodes: Pin D13-D10, respectively
- Segment A: D8
- Segment B: D6
- Segment C: A5
- Segment DP (Decimal Point): A4
- Segment D: A3
- Segment E: A2
- Segment F: D7
- Segment G: D9
I will be using the SevSeg library to control this.

There are two buttons (simply wired to gnd, will need to use INPUT_PULLUP and debounce logic):
- Button 1: A0
- Button 2: A1

Finally, there are two LEDs:
- LED 1: D4
- LED 2: D5

The default display is "----". The display will change in one of these scenarios:
1. When Button 1 is pressed (except if a received message is currently being displayed, as elaborated in 2.):
 - The display will turn into a hexadeximal number starting at 0000. When Button 1 is pressed again, the display will increment (up to FFFF, and then loop back to 0000). After holding Button 1 for more than 1 second, the least significant digit will increases at a rate of 4 increments per second. If still holding for another 2 seconds, the next most significant digit (the second-to-rightmost digit) will increase at the aforementioned rate. If still holding for another 2 more seconds, the same will happen to the third digit. Finally, if they're still holding for another 2 more seconds the same will happen to the fourth digit.
 - Once the user has selected a number, the user will be able to press Button 2 to send the number as data to the other Host (the packet is sent with app_id as 67 and setting as 21, with the number as 'data'). After sending, the display will show "SEnt" for 1 second, then go back to displaying "----". The state of any DPs are unaffected by any of this process.
2. When a packet is received from the other Host whose app_id == 67 and setting == 21:
 - The data will the be stored in a queue. If there is data in the first queue slot, the DP of Digit 1 will turn on. The same goes for the other 3 DPs for the second, third, and fourth matching packets. The queue can only hold 4 elements, and if a 5th matching packet is received when the queue is full, LED 2 will flash for 1 second, and that packet will then be discarded.
 - When Button 2 is pressed when "----" is displayed and at least 1 message is in the queue (as marked by the DPs), the data will be popped off of the queue (with the DPs to "decrement" accordingly) and shown on the display while LED 1 blinks once per second. When either button is pressed when the received data is being displayed, the message will be cleared from the screen and the default "----" will be displayed once more. However, when Button 2 is pressed when there is a number displayed (i.e. while a message to send is being composed) from using Button 1 (as described in 1.), it will always trigger a send action, even if DPs are lit (i.e. when there are matching packets in the receive queue).
 - Since the display can only show 2 bytes (4 hex digits), any data more than 2 bytes will be truncated so that only 16 least significant bits will (in hex format) be displayed.

 Host-to-switch connection:
- Host Unos do not connect to Nano hardware Serial.
- Host Unos connect to one of the Nano's three NeoSWSerial network ports, the same way another Nano would.
- Uno hardware Serial TX/RX carries DataPacket traffic to/from the selected Nano NeoSWSerial RX/TX pins.
- Uno pin D2 is the host's outbound trigger pin.
- Uno D2 must be wired to the selected Nano port's interrupt input:
  - Nano port 1 interrupt input: A0
  - Nano port 2 interrupt input: A1
  - Nano port 3 interrupt input: A2
- Before the Uno sends a DataPacket, it drives D2 HIGH, waits 50 microseconds, writes the 8-byte DataPacket on Serial, flushes, then drives D2 LOW.
- The Nano uses that interrupt input to select the corresponding NeoSWSerial port and read the incoming host packet.


 #### APPROVED CODEX SUGGESTIONS:

Nano interrupt handling:
- Nano switches must use EnableInterrupt on A0-A2 trigger inputs.
- A0-A2 are on PORTC / PCINT1.
- NeoSWSerial RX pins use D4, D6, and D8, which are on PORTD / PCINT2 and PORTB / PCINT0.
- To avoid PCINT vector conflicts, the nano_switch PlatformIO environment must define:
  - NEOSWSERIAL_EXTERNAL_PCINT
- In nanoswitch.h, define these before including EnableInterrupt:
  - EI_NOTEXTERNAL
  - EI_NOTPORTB
  - EI_NOTPORTD
- nanoswitch.h must provide manual NeoSWSerial handlers for:
  - PCINT0_vect -> NeoSWSerial::rxISR(PINB)
  - PCINT2_vect -> NeoSWSerial::rxISR(PIND)
- EnableInterrupt owns PCINT1 for A0-A2 trigger detection.
- The A0-A2 interrupt pins must trigger the switch to listen on the corresponding NeoSWSerial port; the switch should not blindly rotate through all ports.

