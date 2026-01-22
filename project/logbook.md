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
