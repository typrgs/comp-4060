#include "heart.h"
#include "icm.h"

int main()
{
  for(int i=0; i<1000000; i++);

  // enable interrupts
  __enable_irq();
  
  icmInit();
  heartInit();

  uint64_t msg = 1305976;
  uint8_t digest[32];

  // LED output
  PORT_REGS->GROUP[0].PORT_DIRSET = PORT_PA14;
  PORT_REGS->GROUP[0].PORT_OUTSET = PORT_PA14;
  
  uint32_t flashTimestamp = 0;
  
  for(;;)
  {
    uint32_t msCount = elapsedMS();
    
    if(msCount >= flashTimestamp)
    {
      icmSHA256(msg, digest);
      // dbg_write_u8(digest, 32);
      PORT_REGS->GROUP[0].PORT_OUTTGL = PORT_PA14;
      flashTimestamp = msCount + 500;
    }
  }

  return 0;
}
