# A3-1

Peers
- CAN tranceiver receives everything
  - including what WE transmit

- Each device has both TX and RX enabled at all times
  - we need to know if WE are transmitting (so we don't accidentally RX our own message)
    - set some flag to indicate "currently transmitting" (to throw away all data RX'd while flag is set)
  
  - all receives are interrupt driven - NO BLOCKING
    - unlike how the display unit would TX a request and block on RX
      - now everyone does RX asynchronously
    - receiving what WE send asynchronously as well
  
  - carrier sensing is based on our state machine
    - make sure we aren't currently receiving a frame before starting carrier sensing
    - there also has to be a timeout (while receiving, before transmitting) when not in a frame
      - ie. we wait state machine to go to a state that isn't for receiving a frame, then do the actual carrier sensing and start TX on network idle timeout

Collisions can occur
- ALL devices do (the exact same) carrier sensing
  - meaning all devices start carrier sensing at the same time when transmitting
    - because everyone sees the network the same, and if two devices want to transmit, they see the same network activity, and thus start at the same time

- (on transmit) need to check each byte received to verify the network
  - shouldn't be timing out (since we just sent something on the network)
    - if we RX timeout while sending, that's an error
    - ie. asking, did I get a byte after sending this byte?

  - should be what I sent
    - if the byte we RX'd and the byte we just TX'd are different, that's an error
    - ie. asking, did I get the byte I just sent?

  - if the answer to either of the above questions is NO, there is a collision
    - ie. can have a wired AND/wired OR
      - ex. if we send 1 and see 0 (AND), peers receive 0
      - ex. if we send 0 and see 1 (OR), peers receive 1
  
- if I see a collision (seeing a wired AND or wired OR), STOP TX IMMEDIATELY


- everyone will does carrier sensing at the same time (and thus start TX again at the same time)

- then note that our ID gets transmitted as part of the packet (as defined in A3 net.h)
  - one ID wins, other IDs lose (they see a collision and stop immediately), and winner continues to TX their message, losers RX asynchronously

- losers perform a backoff and retry
  - ie. stop trying to transmit for a bit (because we know someone else was just transmitting on the network), then try again

  - use a (pretty small, from 1 to 10ms) random time backoff (using TRNG sfu)
    - even with the backoff, a collision can still occur

  - have either a linear or exponential backoff with each consecutive collision
    - ie. multiply backoff time by 1, 2, 3... to do linear
    - ie. multiply backoff time by 2, 4, 8... to do exponential
      - this means that the more collisions you are apart of, the longer you wait in order to let others finish before you try again
    - backoff time should be capped at some reasonable level (not too high so you don't starve yourself from the network)

  - doing the backoff is a good place to use `goto`
    - useful because there are so many cases where you could be in a collision (every byte you transmit).. makes code structure cleaner

  
