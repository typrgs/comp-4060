# Project Logbook

Jan 12:
- Write CAN driver initial version, only init function implemented

Jan 14:
- Write implementation for CAN send, CAN receive and a simple test application
  - Test application defines message RAM with simple extended filter IDs

Jan 17:
- RX interrupt not firing, trying different init configs to get it to work
  - Set test mode and internal loopback
  - change message RAM config
  - transmit messages without acceptance filtering

Jan 22:
  - RX interrupt working! hardware configuration was incorrect. CAN transceiver needs to use the external UART pads with jumpers to PA12 and PA13. TX and RX pins on transceiver are flipped relative to bus slot, so using external pads allows me to flip them.
    - Acceptance filtering not matching messages for some reason, needs testing
  - Fixed issue with acceptance filtering! was building the elements incorrectly in memory, so I had garbage in those regions
  - Update CAN RX interrupt handler to invoke callback function properly.
  - Added additional code to CAN init function to parameterize the RX FIFO element count and TX Buffer element count

Jan 23:
  - Added code to implement use of RX FIFO 1
  - Added functions for updating filters and tx buffer contents, added enums for filter configs and types

Jan 29:
  - Decided on a project to do: a PoW blockchain
  - Started implementing SHA256 by looking at wikipedia pseudocode, realized the mcu can do SHA256
  - Started implemented ICM driver for SHA256 computation, not currently working

Jan 30:
  - Got ICM driver working and hashing bytes
  - Create structs for tx buf and filter for easier updating
