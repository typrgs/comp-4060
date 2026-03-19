# CAN Blockchain
Authored by Ty Paragas  
Submitted for April 9th, 2026  


## Compiling and Running
Navigate to the `/app` directory and use the command `make debug` to compile. Flash the microcontroller with the compiled code using the command `make app-install`.


## 1 Design

### 1.1 CAN Driver

#### 1.1.1 A General Driver
When writing the CAN driver, I wanted to try and make it as general as possible, while still including functionality that supported my application. 

Initially, I defined transmit buffer and extended filter structs, each with a 32-bit ID field represented by a `uint32_t` variable. I built the IDs within the CAN driver by passing in the information I wanted to include in to a method defined in the CAN interface. I realized that constructing IDs this way heavily coupled my app with the driver, so I changed the ID variable type in each of the previously mentioned structs to a `uint8_t` array of size 4. This allowed me to build the IDs in the app code instead and pass a completed struct to the driver to update the message RAM.

The driver is also limited in some ways, while overdesigned in others. My implementation only allows the setup of TX buffers and RX FIFOs, and only allows the use of extended message IDs. The choice to limit the driver this way was just to keep it simple and provide essential functionality for communication. One place where it isn't as simple as it could be is that the drivers allows the setup of two RX FIFOs, even though my application only makes use of one, but the addition of this option in the initialization of the driver was trivial so I added it anyways.

#### 1.1.2 Interrupt-driven vs Scheduled Message Processing
I initially implemented the receipt of messages asynchronously, drawing heavy inspiration from how we did message processing in the assignments. This worked for a bit while testing, but I discovered that if I wanted to have decent fault tolerance in my nodes, I needed message processing to be on a schedule, otherwise nodes could get stuck in an invalid state forever if no other messages were sent to it. Considering this, I changed my message processing implementation to a state machine with a scheduled processing task that calls a receive method given in the CAN driver interface and checks if the indicated RX FIFO has any new contents. By implementing message processing as a state machine, I was able to implement timeouts in appropriate places by adding per-state counts that increment on each process of the state machine, allowing a node to return to an idle state if it never receives the remaining bytes of a message.

Doing message processing this way also allowed me to reduce the complexity of the callback function I was providing to the CAN driver on startup. Before, the app would receive a message from the driver in the callback as a parameter and then call a helper function that processes messages of the type received. Receiving messages this way allowed me to process different types of messages "at the same time", but forced me to manage separate state for each message type (since each one represents a different sequence of messages). This was way too messy and introduced a ton of bugs and race conditions into my code that were easily solved by moving everything to a single state machine. The callback itself was reduced to incrementing a counter representing the number of messages in the FIFO.


### 1.2 Messaging Protocol

#### 1.2.1 Broadcast vs Propagation
- because broadcast is possible over CAN bus, it seemed like the go-to choice for sending out blocks
  - didn't allow other nodes to receive it if they weren't ready to receive (e.g. they weren't in the right state to receive a new block somehow)
  - using propagation gives nodes who might have not been ready to receive a new block the first time additional chances to catch up with the rest of the network

#### 1.2.2 Message Design
- explain choices behind types/headers

### 1.3 Digital Signatures
- hmac implementation, private keys in params


### 1.4 The Blockchain
- also wanted to make general, work with different sigature methods
- depends on the CAN interface I wrote, but depends on the specific CAN controller because of message RAM setup
  - hard to do it dynamically just through the CAN init


## Code Organization

### `/app`
contains source code specific to the blockchain application

#### `blockchain.c/.h`
- contains structs, enums, macros, function pointer aliases, and functions specific to the blockchain application
  - includes blockchain init, block verification, and block signing

#### `display.c/.h`
- implements a display driver. is a modified version of the display driver provided in our assignments
  - adds additional fonts (letters + :)

#### `hmac.c/.h`
- implements a keyed-hash message authentication code according to standards defined in RFC 2104.
- depends on the ICM driver and the SHA-256 functionality it provides

#### `morse_map.c/.h`
- implements an interface used to convert a binary string to a morse code string made up of '-' and '.' characters

#### `net.h`
- includes enums for network message/header types and macros for ID indexing

#### `main.c`
- implements the application code
- handles state machines, utils, helpers, and app display

### `/drivers`
contains device drivers


### `/include`
contains interfaces for drivers
