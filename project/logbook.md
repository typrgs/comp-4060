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

Feb 1:
  - Add helpers in app for resetting/updating tx/filter buffers
  - Change filter to represent ID as an array rather than as a discrete value
  - Start implementing a blocking "consensus" operation
    - including block verification

Feb 8:
  - Redesign network message protocol by creating a header enum that is placed into the ID of every message

Feb 12:
  - Add TRNG driver
  - Implement network discovery on join

Feb 13:
  - Implement new block sharing
    - Add helpers for sending/receiving blocks
    - Create separate states for sending/receiving new blocks

Feb 17:
  - Implement sending new block for propagation

Feb 18:
  - Redesign communications
    - Had issues with fault tolerance during propagation due to message processing being interrupt driven
    - Switch message processing to a stateful approach so invalid app state is less likely
      - Allowed timeouts while processing
    - CAN callback becomes just an update of a variable

Feb 24:
  - Update block transactions to contain strings rather than ints
  - Begin implementing a state machine/input for morse code
    - this machine is what handles the LED status indicator

March 1:
  - Add display driver and font table

March 5:
  - Implement display in the app

March 7:
  - Implement discovery as a state machine
  - Makes rediscovery easy when local chain is detected to be invalid

March 8:
  - Implement block propagation as a state machine
  - separates sending block messages from normal message processing state
