# A2-1

RS-485 -> CAN
- switching physical layer
- keep same data link layer and controller/responder architecture

- using SERCOM0 as with RS-485
  - same clocking
  - same muxing
  - same interrupts
  - same CTRLA setup
  - same status register clearing
  - same data reg
  - same spinning on interrupts

- UART sees no difference

Differences in new physical layer:
- no pins for selecting TX/RX
  - ie. the GPIO pins to switch between TX and RX in RS-485
  - there is a GPIO pins to enable "standard mode" or "power saving mode"
    - we use standard mode, just pull that pin LOW

- use CTRLB RXEN and TXEN bits to configure UART
  - this gives us the behaviour of the RS-485 GPIO pins
  - default to RXEN
  - switch to TXEN as needed
- we are required to use these bits because CAN tranceiver gives UART everything that appears on the bus
  - means you will receive what you are sending
  - so need to disable RX when doing TX

  - this exists because we want this for peer-to-peer networking


