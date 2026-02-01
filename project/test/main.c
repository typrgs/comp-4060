#include "common.h"
#include "can.h"
#include "heart.h"

#define EXTENDED_FILTER_COUNT 1
#define RX_FIFO_ELEMENT_COUNT 1
#define TX_BUF_ELEMENT_COUNT 1

// setup message RAM for CAN
#define EXTENDED_FILTER_SIZE EXTENDED_FILTER_COUNT * EXTENDED_FILTER_WORDS
#define RX_FIFO_SIZE RX_FIFO_ELEMENT_COUNT * RX_FIFO_ELEMENT_WORDS
#define TX_BUF_SIZE TX_BUF_ELEMENT_COUNT * TX_BUF_ELEMENT_WORDS

uint32_t messageRAM[EXTENDED_FILTER_SIZE + RX_FIFO_SIZE + TX_BUF_SIZE] __ALIGNED(32);

uint32_t *extendedFilterStart = (uint32_t *)&(messageRAM[0]);
uint32_t *rxFifoStart = (uint32_t *)&(messageRAM[EXTENDED_FILTER_SIZE]);
uint32_t *txBufStart = (uint32_t *)&(messageRAM[EXTENDED_FILTER_SIZE + RX_FIFO_SIZE]);

uint8_t rxBuf[RX_FIFO_ELEMENT_DATA_BYTES];


void processMsg(uint8_t len)
{
  dbg_write_u8(rxBuf, len);
}

int main()
{
  for(int i=0; i<1000000; i++);

  // enable interrupts
  __enable_irq();

  CANInit(rxFifoStart, NULL, txBufStart, extendedFilterStart, RX_FIFO_ELEMENT_COUNT, 0, TX_BUF_ELEMENT_COUNT, EXTENDED_FILTER_COUNT, rxBuf, processMsg);

  // setup extended filter
  CANExtFilter filter = {
    0,
    1,
    1,
    STF0M,
    DUAL
  };
  CANUpdateFilter(filter);

  
  // setup TX buffer element
  CANTxBuf buf = {
    0,
    1,
    3,
    0,
    0
  };
  CANUpdateTxBuf(buf);
  
  heartInit();

  // LED output
  PORT_REGS->GROUP[0].PORT_DIRSET = PORT_PA14;
  PORT_REGS->GROUP[0].PORT_OUTSET = PORT_PA14;
  
  uint32_t flashTimestamp = 0;
  
  for(;;)
  {
    uint32_t msCount = elapsedMS();
    
    if(msCount >= flashTimestamp)
    {
      CANSend(1);
      CANUpdateTxBuf(buf);
      PORT_REGS->GROUP[0].PORT_OUTTGL = PORT_PA14;
      flashTimestamp = msCount + 500;
    }
  }

  return 0;
}
