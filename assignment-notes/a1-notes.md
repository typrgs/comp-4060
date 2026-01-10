# A1-1

## Stack of network abstractions

|-----------|
 Application
|-----------|
 Transport
|-----------|
 Network
|-----------|
 Data link
|-----------|
 Physical
|-----------|

Physical layer is communication over actual hardware - bits moving on wires
Data link layer is the drivers/devices talking to eachother

Application layer is the actual apps working
- This layer just has abstractions for DL and physical layers, just letting them communicate
- Makes calls to its DL layer to send something to another device, DL layer does processing to ensure reliable communication, then DL layer uses physical layer to actually send the message

- This process reversed for receiving - phys layer pushes bytes to DL layer, who processes it (validity checking, etc) and pushes to application layer

Our assignments builds the data link layer that uses the physical layer
Application layer code will be provided, and we want to provide the functionality required by it


## Data Link Layer

Need to recognize the beginning and end of a frame (set of bytes forming message), and errors in the message
- need to have a reliable to way to handle errors

Frame fields:
|sentinel|header fields|data|CRC|sentinel|

Application layer provides the DL layer with the data of the frame, and maybe header fields

CRC is a 16- to 32-bit cyclic redundancy check
- involves computation of a polynomial with certain characteristics that allow us to detect multi-bit errors in what was transmitted


Problem: we have (bit) patterns that can't appear in the data, header, etc
- the sentinel!
- this provides us with a byte that marks the start and end of a frame

Solution: byte stuffing (or bit stuffing)
- ex. sentinel is 01111110 = 0x7E
- on send, stuff an extra 7E after each 7E anywhere in message
- on receive of a 7E, if next byte is 7E, discard the second 7E, otherwise it marks the end of the frame
- to differentiate pre/post frame, add another byte after sentinel
  - 7EA5 (pre) ... 7EA6 (post)
  - so added A5 and A6 after 7E to make it clear

if there is a single 7E in the frame, we see it as an error (a collision)

Bit stuffing:
- replace 5 consecutive 1s with 1111110
- so we never see six 1s in a row anywhere in the middle of the message

Implementation:
- sending is easy: just send another 7E after each 7E in the message, etc
- receiving is harder: we use a state machine
    - this state machine is just for deciding where we are in a frame. there is additional processing for actually saving bytes into a buffer or discarding them based on our state

| State/Input  | 0x7E         | Other    | 0xA5        | 0xA6      |
| Idle         | Frame search | Idle     | Idle        | Idle      |
| Frame search | Frame search | Idle     | Frame start |           |
| Frame start  | Stuffing     | In frame |             |           |
| Stuffing     | In frame     | Idle     | Frame start | Frame end |
| In frame     | Stuffing     | In frame |             |           |
| Frame end    | Frame search | Idle     | Idle        | Idle      |

- At frame end, computed CRC should be 0
  - includes received CRC but not sentinels or stuff bytes


# A1-2

## Physical Layer

RS-485 transceiver
- takes digital signal from our UART and converts it into an electrical signal

- 2-wire differential voltage signal
  - used to indicate different logic values
- half-duplex = 1 direction at a time, ie. can't transmit and receive concurrently
  - idle with receive active
    - enable transmit ONLY WHEN NEEDED
  - need to use a controller-responder architecture
  - also implies we have no collision detection
    - meaning we are limited to 1 controller

- We must try to avoid collisions
  - one way to help avoid them is by using only 1 controller
    - ie. only 1 device can initiate transactions

  - can do carrier sensing
      - check network before starting a transmit
      - receive bytes until idle
        - spin, throw away bytes being received, then start transmitting once network is idle

- RS-485 click has 2 GPIO pins
  - to receive, set both low
  - to transmit, set both high

Talks about application code for assignment


# A1-3

## UART Programming

configuration (in order):
1. generic clock - defines bits per sec for the network
  - assignment driver code goes in driver directory

2. use SERCOM0, corresponds to port 2 (click 2)
  - needs main clock - APBA
  - needs pin muxing PA 8 and 9

3. interrupts for RX, if we have a buffer and callback
  - size passed doesn't include the CRC or stuff bytes (from user POV)
  - data link layer has to deal with navigating around the CRC and stuff bytes to give the actual data to the application layer

  - uses SERCOM0_2_Handler(), SERCOM0_2_IRQN

4. setup the UART for TX and RX (CTRLB register)
  - set RXEN and TXEN

5. setup the UART for async, etc? (CTRLA register)
  - mode: set to internal clock
  - cmode: set to asynchronous
  - dord: transmit LSB first
  - RXPO: SERCOM pad 1
  - TXPO: SERCOM pad 0
  - enable

6. set baud register to a fixed value
  - using asynchronous operating mode
  - note baud rate (bits per second) equation, table 33-2
  - setting baud rate to 0 simplifies to gclk freq / 16
    - so if gclk freq = 32, then we get 2 megabit/sec communications

note: using ->USART_INT.xxx (datasheet section 34), to select USART internal clock mode


to RX:
- spin waiting on RXC interrupt flag (polling)
  - can use timeout code from SPI.c
- read data register to get the byte
- clear STATUS register (writing 0xFF)
- then on receipt of a complete data packet, do the callback routine

to do carrier sensing (before transmitting)
- just spin and pull data if receiving (doing all steps above, but throw away that data because we aren't trying to receive)

to TX:
- wait for data register to be clear
  - spin on DRE flag (data register empty)
- put a byte in DATA register (which will start automatically transmitting)
- spin on TXC until tx completes
- repeat until data packet is sent (with the additional stuff, sentinel, stuff bytes, etc)


CRC:
- use DMAC to perform a running computation
  - DMAC provides us with the ability to do CRC because CRC is a necessary part of doing IO transfers to memory or disk or something, in order to check for errors

1. setup and start a new CRC(-16) (ie. when building/receiving a packet)
  - reset
    - CRCCTRL register in DMAC, set this register to DMAC_CRCCRL_RESETVALUE
  - initial seed: FFFFFFFF
    - put in CRCCHKSUM
  - set source to IO
    - set CRCSRC register to IO interface

2. give it next byte (from the packet being received, DMAC continually computes CRC)
  - put next byte in CRCDATAIN
  - spin on CRCBUSY bit in CRCSTATUS (wait until bit is set)
  - write 1 to CRCBUSY bit to clear it when done

3. once packet fully received, read the sum computed by DMAC
  - computed sum is held in CRCCHKSUM
  - on success, sum = 0
