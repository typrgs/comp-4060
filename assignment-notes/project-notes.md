# Project-1

Much more than controller-responder (as in assignments)
- does actual collaboration
- uses actual CAN standard via the processor CAN sfu

CAN unit on processor implements the standard for us
- we just set up data structures and memory regions, etc
- similar to how the DMAC is setup

Datasheet assumes that WE understand the CAN standards - need to research this before doing project
- look at the wikipedia page for CAN as a starting point

So basically we want to configure the CAN unit, then just write application code using it

We want want at least 3 devices collaborating (each sending commands and sharing information), with each doing a task, working together to get something done.


# Project-2

We are using the CAN controller, and thus the CAN standard
- needs to use terminating resistors (as in A3)
- must run at 500kbps
  - according to CAN section in datasheet under NBTP register, if the CAN clock running at 8Mhz, the reset value for this register configures the unit for a bitrate of 500kbps

We should use extended addressing (extended ID filtering)
- allows us to use the message structure defined in the assignments
- ie. including a device ID, message type, sensor selection, etc
  - gives us option to filter messages based on this info

We aren't using CAN as in A3
- setup controller buffers so application will update/read specific elements
  - NOT rebuilding entire messages
  - using controller buffer avoids the need to "memcpy" into the message RAM
- ie. setup buffers and use TXBAR register to set a bit and send the corresponding buffer

- allows us to have a task running at a fixed frequency for each TX
- then we have a different task to update the data being transmitted
  - ie. separating data update and data transmit
  - need to consider concurrent data access with these tasks

We don't have to handle collisions ourselves (CAN controller does it for us according to CAN standard)

Need to define where RX'd messages go
- we want to use the RX FIFO, not the dedicated RX buffers
- set it up to only contain 1 element (single element FIFO) to start, can add more later

We have TX buffers, FIFO, or queue available to us in hardware
- simpler to use a dedicated TX buffer

30 different interrupts can occur with the CAN controller
- past issue: bit 17 MRAF (Message RAM Access Failure) gets set when not debugging, and doesn't get set when debugging

-  cause: in normal operation, when CPU sleeps, RAM isn't clocked, thus cannot be accessed. when halted while debugging, Power Manager stays active, thus RAM stays clocked, thus can be accessed

- so when using the CAN controller, CPU cannot go to sleep in order to apply message filters properly (and thus receive properly)
  - so don't put CPU to sleep if we need to receive!
  - don't call __WFI();
   
  - problem: tasks only need a few microseconds to run, so in our main loop, a task could be run repeatedly until a full millisecond has passed and it waits until the next period
  - solution: don't use MOD to check the time, just read the current time and set a timestamp for the next period and check if that timestamp has passed
